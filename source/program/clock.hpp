#pragma once

#include "config.hpp"

#include <algorithm>
#include <cstdint>

/*
 * Fixed-timestep accumulator. Each rendered frame adds its real elapsed time; the game tick
 * (GameSystem::movement) only runs when a whole logic step has accumulated. The leftover fraction
 * is how far the display has moved on from the newest logic state.
 *
 * Self-contained (no exlaunch or SDK types) so the host tests can drive it with synthetic ticks.
 */
namespace smo::clock {
    /* The Switch system tick runs at 19.2 MHz. */
    constexpr std::uint64_t TicksPerSecond = 19'200'000;
    /* Game logic is authored for exactly 60 steps per second. */
    constexpr std::uint64_t LogicStepsPerSecond = 60;
    constexpr std::uint64_t TicksPerLogicStep = TicksPerSecond / LogicStepsPerSecond;
    /*
     * After a stall (loading, a breakpoint) at most this many steps of debt are kept, so the game
     * never fast-forwards; frame times are clamped to the same so effects don't jump either.
     */
    constexpr std::uint64_t MaxStepDebt = 2;

    /*
     * Smooth frame pacing. The emulator presents at a fixed rate, but each frame's timestamp is read
     * with some jitter; fed straight into the accumulator, that noise moves both when ticks land and
     * how far each frame is blended. Instead, every frame counts as exactly one display frame
     * (TicksPerSecond / fps, remainder carried so it's exact over time), and the difference from the
     * measured time ("drift") is paid back gradually, 1/PacingDriftGain per frame:
     *
     *  - Jitter cancels out (a late timestamp makes the next frame look short), so drift stays small.
     *  - A real rate mismatch (fps set higher than the display runs) makes drift grow; past
     *    1/PacingDriftLimitDivisor of a frame it's handed over at once, so game speed stays right.
     *  - A hitch (one frame more than 1/PacingHitchDivisor off) uses measured time straight away.
     *
     * When pacing starts, and again after every hitch, the accumulator's phase is set so tick
     * boundaries fall between frames (at 120 Hz, frames are drawn at 0.25 and 0.75 of a tick), never
     * on one, where tiny corrections would flip which frame ticks. Whole ticks still owed are kept.
     */
    constexpr std::int64_t PacingDriftGain = 16;
    constexpr std::int64_t PacingDriftLimitDivisor = 4;
    constexpr std::int64_t PacingHitchDivisor = 2;

    struct FrameClock {
        std::uint64_t lastFrameTick = 0;
        std::uint64_t accumulator = 0;
        /* Whether this rendered frame runs a game tick. */
        bool runCalcThisFrame = true;
        /* How far this frame is past the newest tick, in ticks, from 0 to 1. */
        float leftover = 1.f;
        /* This frame's duration in ticks (~0.5 at 120 Hz); effects advance by this much per frame. */
        float frameTicks = 1.f;
        /* Smooth pacing: measured time not yet handed to the accumulator (may be negative). */
        std::int64_t drift = 0;
        /* Smooth pacing: TicksPerSecond / fps leaves a remainder; carried here (in 1/fps ticks). */
        std::uint64_t expectedRemainder = 0;
        /* Smooth pacing: the phase has been set since pacing started or since the last hitch. */
        bool locked = false;

        /*
         * One rendered frame at system tick `now`. `targetFps` is the configured display rate;
         * `smooth` picks smooth pacing over plain measured time.
         */
        void Advance(std::uint64_t now, int targetFps = 0, bool smooth = false) {
            if (lastFrameTick == 0)
                lastFrameTick = now - TicksPerLogicStep;

            std::uint64_t measured = now - lastFrameTick;
            std::uint64_t elapsed = measured;
            if (smooth && targetFps > 0)
                elapsed = Pace(measured, static_cast<std::uint64_t>(targetFps));
            else
                locked = false;
            frameTicks = std::min(static_cast<float>(elapsed) / static_cast<float>(TicksPerLogicStep),
                                  static_cast<float>(MaxStepDebt));

            /* Cap the debt so a long stall doesn't fast-forward. */
            accumulator = std::min(accumulator + elapsed, MaxStepDebt * TicksPerLogicStep);
            lastFrameTick = now;

            /* At most one logic step per rendered frame: below 60 fps the game slows down, as in vanilla. */
            runCalcThisFrame = accumulator >= TicksPerLogicStep;
            if (runCalcThisFrame)
                accumulator -= TicksPerLogicStep;

            leftover = std::min(static_cast<float>(accumulator) / static_cast<float>(TicksPerLogicStep), 1.f);
        }

    private:
        static constexpr std::uint64_t Gcd(std::uint64_t a, std::uint64_t b) {
            while (b != 0) {
                std::uint64_t t = a % b;
                a = b;
                b = t;
            }
            return a;
        }

        /* Smooth pacing: how much time this frame counts for. */
        std::uint64_t Pace(std::uint64_t measured, std::uint64_t fps) {
            std::uint64_t numerator = TicksPerSecond + expectedRemainder;
            std::uint64_t expected = numerator / fps;
            expectedRemainder = numerator % fps;
            auto frame = static_cast<std::int64_t>(expected);

            if (!locked) {
                /*
                 * fps / LogicStepsPerSecond frames per tick, as p / q in lowest terms: tick boundaries
                 * fall at multiples of 1/q of a frame, so a phase of half that keeps every boundary as
                 * far from a frame as it can be.
                 */
                std::uint64_t q = LogicStepsPerSecond / Gcd(fps, LogicStepsPerSecond);
                std::uint64_t owed = accumulator / TicksPerLogicStep * TicksPerLogicStep;
                accumulator = owed + TicksPerSecond / (fps * 2 * q);
                drift = 0;
                locked = true;
            }

            std::int64_t error = static_cast<std::int64_t>(measured) - frame;
            if (error > frame / PacingHitchDivisor || error < -frame / PacingHitchDivisor) {
                /* A hitch or stall: real time, including anything still owed; re-phase on the next frame. */
                std::int64_t used = static_cast<std::int64_t>(measured) + drift;
                drift = 0;
                locked = false;
                return used > 0 ? static_cast<std::uint64_t>(used) : 0;
            }

            drift += error;
            if (drift > frame / PacingDriftLimitDivisor || drift < -frame / PacingDriftLimitDivisor) {
                /* The display isn't keeping to fps: hand the drift over now rather than slowly. */
                std::int64_t used = frame + drift;
                drift = 0;
                return used > 0 ? static_cast<std::uint64_t>(used) : 0;
            }
            std::int64_t payback = drift / PacingDriftGain;
            drift -= payback;
            return static_cast<std::uint64_t>(frame + payback);
        }
    };

    /*
     * The weight one system draws with, from the previous tick (0) to the newest (1).
     * Interpolation uses [0, 1], one tick behind; extrapolation uses [1, 1 + maxExtrapolation],
     * continuing past the newest tick along its last step.
     */
    inline float BlendWeight(config::Settings::Smoothing mode, float leftover, float maxExtrapolation) {
        switch (mode) {
        case config::Settings::Smoothing::Off:
            return 1.f;
        case config::Settings::Smoothing::Interpolate:
            return leftover;
        case config::Settings::Smoothing::Extrapolate:
            return 1.f + leftover * maxExtrapolation;
        }
        return 1.f;
    }
}
