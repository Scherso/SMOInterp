#include "actors.hpp"

#include "history.hpp"
#include "smo.hpp"

#include <atomic>
#include <cstring>

namespace smo::actors {
    struct Entry {
        std::atomic<uintptr_t> key;
        u32 bones;
        u32 tick;
        bool hasPrev;
        /* MinHalfTurnCos of prev -> curr, computed for turnTick when extrapolation needs it. */
        u32 turnTick;
        float minHalfTurnCos;
        float* prev;
        float* curr;
        float* saved;
    };

    namespace {
        /* One bone world matrix: nn::util MatrixColumnMajor4x3, stored as four float4 columns. */
        constexpr size_t FloatsPerBone = history::TransformFloats;

        /* Per skeleton: prev and curr tick snapshots, plus the matrices to restore after upload. */
        constexpr size_t BuffersPerEntry = 3;
        /* 16 MiB by default; at 48 floats (3 buffers) per bone that is ~87k bones across all loaded models. */
        constexpr size_t ArenaFloats = tuning::BoneArenaBytes / sizeof(float);

        history::PointerTable<Entry, tuning::SkeletonTableSize> s_Table;
        /* Aligned like the float4 columns it holds, so whole bones can be copied with vector loads. */
        alignas(16) float s_Arena[ArenaFloats];
        std::atomic<size_t> s_ArenaUsed { 0 };

        /* Written on the main thread between worker batches; read by workers. */
        std::atomic<u32> s_Tick { 1 };

        float* Allocate(size_t floats) {
            size_t offset = s_ArenaUsed.fetch_add(floats, std::memory_order_relaxed);
            if (offset + floats > ArenaFloats) {
                SMO_WARN_ONCE("limit: bone arena full (%zu floats)", ArenaFloats);
                return nullptr;
            }
            return &s_Arena[offset];
        }

        /* Record the world matrices as this tick's final pose, the first time a tick uploads them. */
        void Record(Entry& e, const float* world, u32 bones, u32 tick) {
            size_t floats = static_cast<size_t>(bones) * FloatsPerBone;

            if (e.bones != bones) {
                float* storage = Allocate(floats * BuffersPerEntry);
                if (storage == nullptr) {
                    e.bones = 0;
                    return;
                }
                e.prev = storage;
                e.curr = storage + floats;
                e.saved = storage + floats * 2;
                e.bones = bones;
                e.tick = 0;
                e.turnTick = 0;
            }

            if (e.tick == tick)
                return;

            bool continuous = e.tick != 0 && e.tick + 1 == tick;
            float* recycled = e.prev;
            e.prev = e.curr;
            e.curr = recycled;
            std::memcpy(e.curr, world, floats * sizeof(float));
            e.hasPrev = continuous && !history::IsTeleport(e.prev, e.curr);
            e.tick = tick;
        }
    }

    void BeginTick() {
        s_Tick.fetch_add(1, std::memory_order_release);
    }

    ScopedBlend::ScopedBlend(void* modelCtrl, float alpha, const BoneOptions& options) {
        void* modelObj = modelCtrl ? Field<void*>(modelCtrl, smo::fields::ModelCtrl_modelObj) : nullptr;
        void* skeleton = modelObj ? Field<void*>(modelObj, smo::fields::ModelObj_skeleton) : nullptr;
        if (skeleton == nullptr)
            return;

        float* world = Field<float*>(skeleton, smo::fields::SkeletonObj_worldMtxArray);
        u32 bones = Field<u16>(skeleton, smo::fields::SkeletonObj_boneCount);
        if (world == nullptr || bones == 0)
            return;

        Entry* e = s_Table.Find(skeleton, true);
        if (e == nullptr) {
            SMO_WARN_ONCE("limit: skeleton table full");
            return;
        }

        u32 tick = s_Tick.load(std::memory_order_acquire);
        Record(*e, world, bones, tick);
        if (e->bones != bones || !e->hasPrev || e->tick != tick)
            return;

        /* A bone that turned sharply (a roll starting, say) would be flung past its real pose: show the newest one. */
        if (alpha > 1.f && options.minHalfTurnCos > -1.f) {
            if (e->turnTick != tick) {
                e->minHalfTurnCos = history::MinHalfTurnCos(e->prev, e->curr, bones);
                e->turnTick = tick;
            }
            if (e->minHalfTurnCos < options.minHalfTurnCos)
                alpha = 1.f;
        }

        /* Weight 1 is the newest tick, which is what the skeleton already holds: nothing to blend or restore. */
        if (alpha == 1.f)
            return;

        size_t floats = static_cast<size_t>(bones) * FloatsPerBone;
        std::memcpy(e->saved, world, floats * sizeof(float));
        if (options.rotation) {
            for (size_t i = 0; i < floats; i += FloatsPerBone)
                history::BlendRigid(world + i, e->prev + i, e->curr + i, alpha);
        } else {
            history::Blend(world, e->prev, e->curr, floats, alpha);
        }

        m_Skeleton = skeleton;
        m_Entry = e;
    }

    ScopedBlend::~ScopedBlend() {
        if (m_Entry == nullptr)
            return;
        Entry* e = m_Entry;
        float* world = Field<float*>(m_Skeleton, smo::fields::SkeletonObj_worldMtxArray);
        std::memcpy(world, e->saved, static_cast<size_t>(e->bones) * FloatsPerBone * sizeof(float));
    }

    void Reset() {
        s_Table.ForEach([](Entry& e) {
            e.key.store(0, std::memory_order_relaxed);
            e.bones = 0;
            e.tick = 0;
            e.hasPrev = false;
            e.turnTick = 0;
            e.prev = e.curr = e.saved = nullptr;
        });
        s_ArenaUsed.store(0, std::memory_order_release);
    }
}
