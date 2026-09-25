#pragma once

#include <common.hpp>

#include <atomic>
#include <cmath>

/*
 * Shared by actors.cpp and effects.cpp: both keep the last two ticks of a transform per game object,
 * looked up by the object's address from the engine's worker threads, and blend between them.
 */
namespace smo::history {
    /* A transform (4x3 or 4x4 floats) has its translation at floats 12..14. */
    constexpr size_t TranslationOffset = 12;

    /* Moving further than this in one tick is a warp, not motion. */
    constexpr float TeleportDistance = 1000.f; /* world units (cm) */

    inline bool IsTeleport(const float* prev, const float* curr) {
        float dx = curr[TranslationOffset + 0] - prev[TranslationOffset + 0];
        float dy = curr[TranslationOffset + 1] - prev[TranslationOffset + 1];
        float dz = curr[TranslationOffset + 2] - prev[TranslationOffset + 2];
        return std::sqrt(dx * dx + dy * dy + dz * dz) > TeleportDistance;
    }

    /* out = prev + (curr - prev) * alpha, element-wise. */
    inline void Blend(float* out, const float* prev, const float* curr, size_t count, float alpha) {
        for (size_t i = 0; i < count; ++i)
            out[i] = prev[i] + (curr[i] - prev[i]) * alpha;
    }

    /*
     * Fixed-size open-addressing map from an object's address to an Entry, safe for concurrent
     * lookups and inserts. Entries are never removed one by one: Entry needs a
     * std::atomic<uintptr_t> key, and the owner clears the keys (with ForEach) when nothing can be
     * looking anything up.
     */
    template<typename Entry, size_t Size>
    class PointerTable {
        static_assert((Size & (Size - 1)) == 0, "Size must be a power of two");

    public:
        /* Returns nullptr if the object isn't present (and !insert) or its probe window is full. */
        Entry* Find(const void* object, bool insert) {
            uintptr_t key = reinterpret_cast<uintptr_t>(object);
            size_t start = Hash(key);
            for (size_t i = 0; i < MaxProbe; ++i) {
                Entry& e = m_Entries[(start + i) & (Size - 1)];
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

        template<typename Fn>
        void ForEach(Fn fn) {
            for (auto& e : m_Entries)
                fn(e);
        }

    private:
        static constexpr size_t MaxProbe = 64;

        static size_t Hash(uintptr_t key) {
            key ^= key >> 33;
            key *= 0xff51afd7ed558ccdull;
            key ^= key >> 33;
            return size_t(key);
        }

        Entry m_Entries[Size];
    };
}
