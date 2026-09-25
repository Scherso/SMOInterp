#include "actors.hpp"

#include "history.hpp"
#include "smo.hpp"

#include <atomic>
#include <cstring>

namespace smo::actors {
    namespace {
        /* One bone world matrix: nn::util MatrixColumnMajor4x3, stored as four float4 columns. */
        constexpr size_t FloatsPerBone = 16;

        /* Per skeleton: prev and curr tick snapshots, plus the matrices to restore after upload. */
        constexpr size_t BuffersPerEntry = 3;
        constexpr size_t ArenaFloats = 4 * 1024 * 1024; /* 16 MiB, ~87k bones */

        struct Entry {
            std::atomic<uintptr_t> key;
            u32 bones;
            u32 tick;
            bool hasPrev;
            float* prev;
            float* curr;
            float* saved;
        };

        history::PointerTable<Entry, 1 << 15> s_Table;
        alignas(16) float s_Arena[ArenaFloats];
        std::atomic<size_t> s_ArenaUsed { 0 };

        /* Written on the main thread between worker batches; read by workers. */
        std::atomic<u32> s_Tick { 1 };

        float* Allocate(size_t floats) {
            size_t offset = s_ArenaUsed.fetch_add(floats, std::memory_order_relaxed);
            if (offset + floats > ArenaFloats)
                return nullptr;
            return &s_Arena[offset];
        }

        /* Record the world matrices as this tick's final pose, the first time a tick uploads them. */
        void Record(Entry& e, const float* world, u32 bones, u32 tick) {
            size_t floats = size_t(bones) * FloatsPerBone;

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

    ScopedBlend::ScopedBlend(void* modelCtrl, float alpha) {
        void* modelObj = modelCtrl ? Field<void*>(modelCtrl, smo::fields::ModelCtrl_modelObj) : nullptr;
        void* skeleton = modelObj ? Field<void*>(modelObj, smo::fields::ModelObj_skeleton) : nullptr;
        if (skeleton == nullptr)
            return;

        float* world = Field<float*>(skeleton, smo::fields::SkeletonObj_worldMtxArray);
        u32 bones = Field<u16>(skeleton, smo::fields::SkeletonObj_boneCount);
        if (world == nullptr || bones == 0)
            return;

        Entry* e = s_Table.Find(skeleton, true);
        if (e == nullptr)
            return;

        u32 tick = s_Tick.load(std::memory_order_acquire);
        Record(*e, world, bones, tick);
        if (e->bones != bones || !e->hasPrev || e->tick != tick)
            return;

        size_t floats = size_t(bones) * FloatsPerBone;
        std::memcpy(e->saved, world, floats * sizeof(float));
        history::Blend(world, e->prev, e->curr, floats, alpha);

        m_Skeleton = skeleton;
        m_Entry = e;
    }

    ScopedBlend::~ScopedBlend() {
        if (m_Entry == nullptr)
            return;
        auto* e = static_cast<Entry*>(m_Entry);
        float* world = Field<float*>(m_Skeleton, smo::fields::SkeletonObj_worldMtxArray);
        std::memcpy(world, e->saved, size_t(e->bones) * FloatsPerBone * sizeof(float));
    }

    void Reset() {
        s_Table.ForEach([](Entry& e) {
            e.key.store(0, std::memory_order_relaxed);
            e.bones = 0;
            e.tick = 0;
            e.hasPrev = false;
            e.prev = e.curr = e.saved = nullptr;
        });
        s_ArenaUsed.store(0, std::memory_order_release);
    }
}
