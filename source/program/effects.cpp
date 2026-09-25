#include "effects.hpp"

#include "history.hpp"
#include "smo.hpp"

#include <atomic>
#include <cstring>

namespace smo::effects {
    namespace {
        /*
         * nn::vfx::EmitterSet placement (from SetMatrix/SetPos): the SRT matrix at 0x60 and its
         * scale-free RT copy at 0xa0, each four float4 rows, translation in the last row.
         */
        constexpr ptrdiff_t PlacementBegin = 0x60;
        constexpr size_t PlacementFloats = 32;
        constexpr ptrdiff_t MatrixDirty = 0x18;

        struct Entry {
            std::atomic<uintptr_t> key;
            u32 tick;
            bool hasPrev;
            float prev[PlacementFloats];
            float curr[PlacementFloats];
            float saved[PlacementFloats];
            u32 savedDirty;
        };

        history::PointerTable<Entry, 1 << 13> s_Table;
        std::atomic<u32> s_Tick { 1 };
    }

    void BeginTick() {
        s_Tick.fetch_add(1, std::memory_order_release);
    }

    ScopedBlend::ScopedBlend(void* emitterSet, float alpha) {
        Entry* e = s_Table.Find(emitterSet, true);
        if (e == nullptr)
            return;

        float* placement = &Field<float>(emitterSet, PlacementBegin);
        u32 tick = s_Tick.load(std::memory_order_acquire);

        /* The first calculation of a tick records the placement the game set for it. */
        if (e->tick != tick) {
            bool continuous = e->tick != 0 && e->tick + 1 == tick;
            std::memcpy(e->prev, e->curr, sizeof(e->curr));
            std::memcpy(e->curr, placement, sizeof(e->curr));
            e->hasPrev = continuous && !history::IsTeleport(e->prev, e->curr);
            e->tick = tick;
        }
        if (!e->hasPrev)
            return;

        std::memcpy(e->saved, placement, sizeof(e->saved));
        e->savedDirty = Field<u32>(emitterSet, MatrixDirty);
        history::Blend(placement, e->prev, e->curr, PlacementFloats, alpha);
        /* Make the emitters rebuild their matrices from the blended placement. */
        Field<u32>(emitterSet, MatrixDirty) = 1;

        m_Set = emitterSet;
        m_Entry = e;
    }

    ScopedBlend::~ScopedBlend() {
        if (m_Entry == nullptr)
            return;
        auto* e = static_cast<Entry*>(m_Entry);
        std::memcpy(&Field<float>(m_Set, PlacementBegin), e->saved, sizeof(e->saved));
        Field<u32>(m_Set, MatrixDirty) = e->savedDirty;
    }

    void Forget(void* emitterSet) {
        if (Entry* e = s_Table.Find(emitterSet, false)) {
            e->tick = 0;
            e->hasPrev = false;
        }
    }

    void Reset() {
        s_Table.ForEach([](Entry& e) {
            e.key.store(0, std::memory_order_relaxed);
            e.tick = 0;
            e.hasPrev = false;
        });
    }
}
