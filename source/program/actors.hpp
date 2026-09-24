#pragma once

#include <common.hpp>

/*
 * Actor (model) interpolation. Every model's bone world matrices live in its
 * nn::g3d::SkeletonObj, computed on game ticks and uploaded to the GPU once per rendered frame by
 * al::ModelCtrl::updateModelDrawBuffer / updateGpuBuffer (which run on the engine's worker
 * threads). Around each upload we record the tick's final matrices, write a blend of the last two
 * ticks into the skeleton, let the upload happen, and put the game's own matrices back.
 */
namespace smo::actors {
    /* Called on the main thread when a logic tick starts; advances the history. */
    void BeginTick();

    /* Wraps one model's GPU upload. Thread-safe across different models. */
    struct ScopedBlend {
        ScopedBlend(void* modelCtrl, float alpha);
        ~ScopedBlend();

        ScopedBlend(const ScopedBlend&) = delete;
        ScopedBlend& operator=(const ScopedBlend&) = delete;

    private:
        void* m_Skeleton = nullptr;
        void* m_Entry = nullptr;
    };

    /* Drops all history. Only call while no model uploads can be running (scene teardown). */
    void Reset();
}
