#pragma once

#include <cstddef>
#include <numbers>

/*
 * The mod's own judgement calls and fixed capacities, in one place. Facts about the game (addresses,
 * field offsets, sizes) live in smo.hpp instead; facts about formats and hardware stay next to the
 * code that reads them.
 *
 * Self-contained so the host tests can use it.
 */
namespace smo::tuning {
    constexpr float DegreesToRadians(float degrees) {
        return degrees * std::numbers::pi_v<float> / 180.f;
    }

    /* SMO's world units are centimetres. */
    constexpr float UnitsPerMetre = 100.f;

    /*
     * Snapping instead of blending. A transform moving further than TeleportDistance in one tick
     * was warped, not moved (10 m in 1/60 s is 600 m/s); a camera turning more than CameraCutDegrees
     * in one tick was cut (1800 degrees per second, far faster than anyone turns the camera).
     */
    constexpr float TeleportDistance = 10.f * UnitsPerMetre;
    constexpr float CameraCutDegrees = 30.f;

    /*
     * Fixed capacities: the mod allocates nothing at runtime. Hitting any of these logs a one-time
     * "limit:" warning and the excess objects are drawn unblended (or, for effects, not replayed).
     */
    constexpr size_t SkeletonTableSize = 1 << 15;       /* skeletons tracked at once */
    constexpr size_t EmitterSetTableSize = 1 << 13;     /* emitter sets tracked at once */
    constexpr size_t TableMaxProbe = 64;                /* slots searched per lookup */
    constexpr size_t BoneArenaBytes = 16 * 1024 * 1024; /* bone history for every tracked skeleton */
    constexpr size_t MaxCameraViews = 4;                /* split-screen views blended */
    constexpr size_t MaxEffectSystems = 4;              /* effect systems replayed per tick */
    constexpr size_t MaxEffectGroupCalls = 64;          /* effect group updates replayed per tick */
    constexpr size_t MaxConfigBytes = 1024;             /* config.ini is read up to this size */

    /* How often the "render … fps, logic … steps/s" line is logged. */
    constexpr unsigned ReportIntervalSeconds = 2;
}
