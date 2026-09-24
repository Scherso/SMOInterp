#include "effects.hpp"

#include "smo.hpp"

#include <atomic>
#include <cmath>
#include <cstring>

namespace smo::effects {
    namespace {
        /* nn::vfx::EmitterSet placement (from SetMatrix/SetPos): the SRT matrix at 0x60 and its
         * scale-free RT copy at 0xa0, each four float4 rows, translation in the last row. */
        constexpr ptrdiff_t PlacementBegin = 0x60;
        constexpr size_t PlacementFloats = 32;
        constexpr size_t TranslationOffset = 12;
        constexpr ptrdiff_t MatrixDirty = 0x18;

        /* A set moving further than this in one tick was re-placed, not carried along. */
        constexpr float TeleportDistance = 1000.f; /* world units (cm) */

        constexpr size_t TableSize = 1 << 13;
        constexpr size_t MaxProbe = 64;

        struct Entry {
            std::atomic<uintptr_t> key;
            u32 tick;
            bool hasPrev;
            float prev[PlacementFloats];
            float curr[PlacementFloats];
            float saved[PlacementFloats];
            u32 savedDirty;
        };

        Entry s_Table[TableSize];
        std::atomic<u32> s_Tick { 1 };

        size_t Hash(uintptr_t key) {
            key ^= key >> 33;
            key *= 0xff51afd7ed558ccdull;
            key ^= key >> 33;
            return size_t(key);
        }

        Entry* Find(uintptr_t key, bool insert) {
            size_t start = Hash(key);
            for (size_t i = 0; i < MaxProbe; ++i) {
                Entry& e = s_Table[(start + i) & (TableSize - 1)];
                uintptr_t found = e.key.load(std::memory_order_acquire);
                if (found == key)
                    return &e;
                if (found == 0) {
                    if (!insert)
                        return nullptr;
                    uintptr_t expected = 0;
                    if (e.key.compare_exchange_strong(expected, key, std::memory_order_acq_rel) || expected == key)
                        return &e;
                }
            }
            return nullptr;
        }

        bool IsTeleport(const float* prev, const float* curr) {
            float dx = curr[TranslationOffset + 0] - prev[TranslationOffset + 0];
            float dy = curr[TranslationOffset + 1] - prev[TranslationOffset + 1];
            float dz = curr[TranslationOffset + 2] - prev[TranslationOffset + 2];
            return std::sqrt(dx * dx + dy * dy + dz * dz) > TeleportDistance;
        }
    }

    void BeginTick() {
        s_Tick.fetch_add(1, std::memory_order_release);
    }

    ScopedBlend::ScopedBlend(void* emitterSet, float alpha) {
        Entry* e = Find(reinterpret_cast<uintptr_t>(emitterSet), true);
        if (e == nullptr)
            return;

        float* placement = &Field<float>(emitterSet, PlacementBegin);
        u32 tick = s_Tick.load(std::memory_order_acquire);

        /* The first calculation of a tick records the placement the game set for it. */
        if (e->tick != tick) {
            bool continuous = e->tick != 0 && e->tick + 1 == tick;
            std::memcpy(e->prev, e->curr, sizeof(e->curr));
            std::memcpy(e->curr, placement, sizeof(e->curr));
            e->hasPrev = continuous && !IsTeleport(e->prev, e->curr);
            e->tick = tick;
        }
        if (!e->hasPrev)
            return;

        std::memcpy(e->saved, placement, sizeof(e->saved));
        e->savedDirty = Field<u32>(emitterSet, MatrixDirty);
        for (size_t i = 0; i < PlacementFloats; ++i)
            placement[i] = e->prev[i] + (e->curr[i] - e->prev[i]) * alpha;
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
        if (Entry* e = Find(reinterpret_cast<uintptr_t>(emitterSet), false)) {
            e->tick = 0;
            e->hasPrev = false;
        }
    }

    void Reset() {
        for (auto& e : s_Table) {
            e.key.store(0, std::memory_order_relaxed);
            e.tick = 0;
            e.hasPrev = false;
        }
    }
}
