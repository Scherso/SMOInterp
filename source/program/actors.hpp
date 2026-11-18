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

    /* Per-skeleton history, defined in actors.cpp. */
    struct Entry;

    /* How bone matrices are blended; chosen once per frame from the config. */
    struct BoneOptions {
        /* Slerp rotations instead of blending matrix components (see history::BlendRigid). */
        bool rotation = false;
        /*
         * When extrapolating (alpha > 1): if any bone turned further than this in the last tick, given
         * as the cosine of half the angle, the model is drawn at its newest pose instead. -1 = no limit.
         */
        float minHalfTurnCos = -1.f;
    };

    /*
     * Wraps one model's GPU upload. Thread-safe across different models. Assumes one skeleton is
     * never uploaded by two threads at once: an Entry's fields other than its key are not atomic.
     */
    struct ScopedBlend {
        ScopedBlend(void* modelCtrl, float alpha, const BoneOptions& options);
        ~ScopedBlend();

        ScopedBlend(const ScopedBlend&) = delete;
        ScopedBlend& operator=(const ScopedBlend&) = delete;

    private:
        void* m_Skeleton = nullptr;
        Entry* m_Entry = nullptr;
    };

    /* Drops all history. Only call while no model uploads can be running (scene teardown). */
    void Reset();
}
