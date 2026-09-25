#include "camera.hpp"

#include "smo.hpp"

#include <cmath>
#include <cstring>

namespace smo::camera {
    namespace {
        struct Vec3 {
            float x, y, z;

            Vec3 operator-(const Vec3& o) const { return { x - o.x, y - o.y, z - o.z }; }
            float Dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
            float Length() const { return std::sqrt(Dot(*this)); }

            Vec3 Normalized() const {
                float len = Length();
                return len > 0.f ? Vec3 { x / len, y / len, z / len } : *this;
            }

            static Vec3 Lerp(const Vec3& a, const Vec3& b, float t) {
                return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
            }
        };

        /* sead::LookAtCamera: vtable, Matrix34f view matrix, then pos / at / up (see doUpdateMatrix). */
        constexpr ptrdiff_t CameraStateBegin = 0x08;
        constexpr ptrdiff_t CameraPos = 0x38;
        constexpr ptrdiff_t CameraAt = 0x44;
        constexpr ptrdiff_t CameraUp = 0x50;
        constexpr size_t CameraStateSize = 0x5c - CameraStateBegin;

        /* A jump bigger than this between ticks is a cut (warp, cutscene edit), not motion. */
        constexpr float CutDistance = 1000.f;        /* world units (cm) */
        constexpr float CutMinDirectionDot = 0.866f; /* ~30 degrees of turn in one tick */

        /*
         * al::Projection wraps a sead::PerspectiveProjection (near/far/fovy/aspect at 0x98/0x9c/0xa0/0xb0)
         * plus derived frustum values and matrices; setProj + calcMtx rebuild all of it.
         */
        constexpr ptrdiff_t ProjNear = 0x98;
        constexpr ptrdiff_t ProjFar = 0x9c;
        constexpr ptrdiff_t ProjFovy = 0xa0;
        constexpr ptrdiff_t ProjAspect = 0xb0;
        constexpr size_t ProjectionSize = 0x1f0;

        constexpr int MaxViews = 4;

        struct Pose {
            Vec3 pos, at, up;
            float fovy;
        };

        struct ViewSlot {
            void* camera = nullptr;
            void* projection = nullptr;
            bool valid = false;
            Pose prev {}, curr {};
            /* The simulation's own matrix + pos/at/up, restored after rendering. */
            u8 trueState[CameraStateSize] {};
            /* The simulation's own projection, saved only on frames where the fovy is being blended. */
            bool projectionSaved = false;
            u8 trueProjection[ProjectionSize] {};
        };

        ViewSlot s_Views[MaxViews];
        bool s_Applied = false;

        using DoUpdateMatrixFn = void (*)(const void* camera, void* outMatrix);
        using SetProjFn = void (*)(void* projection, float near, float far, float fovy, float aspect);
        using CalcMtxFn = void (*)(void* projection);

        Pose ReadPose(void* camera, void* projection) {
            return { Field<Vec3>(camera, CameraPos), Field<Vec3>(camera, CameraAt), Field<Vec3>(camera, CameraUp),
                     projection ? Field<float>(projection, ProjFovy) : 0.f };
        }

        bool IsCut(const Pose& a, const Pose& b) {
            if ((b.pos - a.pos).Length() > CutDistance)
                return true;
            Vec3 dirA = (a.at - a.pos).Normalized();
            Vec3 dirB = (b.at - b.pos).Normalized();
            return dirA.Dot(dirB) < CutMinDirectionDot;
        }

        /*
         * al::SceneCameraInfo { s32 viewNumMax; CameraViewInfo** views; },
         * al::CameraViewInfo { s32 index; bool isValid; ...; const sead::LookAtCamera& lookAtCam @ 0x8 }.
         */
        template<typename Fn>
        void ForEachView(const void* sceneCameraInfo, Fn fn) {
            if (sceneCameraInfo == nullptr)
                return;
            int count = Field<s32>(sceneCameraInfo, smo::fields::SceneCameraInfo_viewNumMax);
            void** views = Field<void**>(sceneCameraInfo, smo::fields::SceneCameraInfo_views);
            if (views == nullptr)
                return;

            for (int i = 0; i < count && i < MaxViews; ++i) {
                void* view = views[i];
                if (view == nullptr || !Field<bool>(view, smo::fields::CameraViewInfo_isValid))
                    continue;
                void* camera = Field<void*>(view, smo::fields::CameraViewInfo_lookAtCam);
                void* projection = Field<void*>(view, smo::fields::CameraViewInfo_projection);
                if (camera != nullptr)
                    fn(s_Views[i], camera, projection);
            }
        }
    }

    void Capture(const void* sceneCameraInfo) {
        ForEachView(sceneCameraInfo, [](ViewSlot& slot, void* camera, void* projection) {
            Pose now = ReadPose(camera, projection);
            if (!slot.valid || slot.camera != camera || slot.projection != projection || IsCut(slot.curr, now)) {
                slot.prev = now;
            } else {
                slot.prev = slot.curr;
            }
            slot.curr = now;
            slot.camera = camera;
            slot.projection = projection;
            slot.valid = true;
        });
    }

    void Apply(const void* sceneCameraInfo, float alpha) {
        auto doUpdateMatrix = MainFunc<DoUpdateMatrixFn>(smo::offsets::LookAtCamera_doUpdateMatrix);

        ForEachView(sceneCameraInfo, [&](ViewSlot& slot, void* camera, void* projection) {
            if (!slot.valid || slot.camera != camera || slot.projection != projection)
                return;

            /* Only the first Apply since the last Restore sees the simulation's own values. */
            if (!s_Applied)
                std::memcpy(slot.trueState, &Field<u8>(camera, CameraStateBegin), CameraStateSize);

            Field<Vec3>(camera, CameraPos) = Vec3::Lerp(slot.prev.pos, slot.curr.pos, alpha);
            Field<Vec3>(camera, CameraAt) = Vec3::Lerp(slot.prev.at, slot.curr.at, alpha);
            Field<Vec3>(camera, CameraUp) = Vec3::Lerp(slot.prev.up, slot.curr.up, alpha).Normalized();
            doUpdateMatrix(camera, &Field<u8>(camera, CameraStateBegin));

            /* Zooms: most ticks leave the fovy alone, and then the projection is never touched. */
            if (projection != nullptr && slot.prev.fovy != slot.curr.fovy) {
                if (!slot.projectionSaved) {
                    std::memcpy(slot.trueProjection, projection, ProjectionSize);
                    slot.projectionSaved = true;
                }
                float fovy = slot.prev.fovy + (slot.curr.fovy - slot.prev.fovy) * alpha;
                MainFunc<SetProjFn>(smo::offsets::Projection_setProj)(projection, Field<float>(projection, ProjNear),
                                                                      Field<float>(projection, ProjFar), fovy,
                                                                      Field<float>(projection, ProjAspect));
                MainFunc<CalcMtxFn>(smo::offsets::Projection_calcMtx)(projection);
            }
        });
        s_Applied = true;
    }

    void Restore() {
        if (!s_Applied)
            return;
        for (auto& slot : s_Views) {
            if (slot.valid && slot.camera != nullptr)
                std::memcpy(&Field<u8>(slot.camera, CameraStateBegin), slot.trueState, CameraStateSize);
            if (slot.projectionSaved && slot.projection != nullptr)
                std::memcpy(slot.projection, slot.trueProjection, ProjectionSize);
            slot.projectionSaved = false;
        }
        s_Applied = false;
    }

    void Reset() {
        Restore();
        for (auto& slot : s_Views)
            slot = ViewSlot {};
    }
}
