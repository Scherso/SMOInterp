#pragma once

#include <common.hpp>

/*
 * Effect anchor interpolation. An nn::vfx::EmitterSet is placed by a matrix the game sets on ticks
 * (EmitterSet::SetMatrix / SetPos); particles are emitted from, and local-space particles follow,
 * that placement every time the set is calculated, which is now every rendered frame. Around each
 * EmitterSet::Calculate we write a blend of the last two ticks' placements, like the bones in
 * actors.hpp, so attached effects stay with their (interpolated) models.
 */
namespace smo::effects {
    void BeginTick();

    /* Wraps one emitter set's calculation. Thread-safe across different sets. */
    struct ScopedBlend {
        ScopedBlend(void* emitterSet, float alpha);
        ~ScopedBlend();

        ScopedBlend(const ScopedBlend&) = delete;
        ScopedBlend& operator=(const ScopedBlend&) = delete;

    private:
        void* m_Set = nullptr;
        void* m_Entry = nullptr;
    };

    /* The vfx system reuses emitter set slots; a reinitialized set must not blend from its old effect. */
    void Forget(void* emitterSet);

    void Reset();
}
