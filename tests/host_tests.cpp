/*
 * Host-side tests for the parts of source/program that don't touch the game: the frame clock, the
 * config parser and the history helpers. Run with `make test` (builds with the host's c++).
 */
#include "clock.hpp"
#include "config_parse.hpp"
#include "history.hpp"
#include "nvn.hpp"
#include "tuning.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {
    int s_Failures = 0;
    int s_Checks = 0;

#define CHECK(cond)                                                                                                    \
    do {                                                                                                               \
        ++s_Checks;                                                                                                    \
        if (!(cond)) {                                                                                                 \
            ++s_Failures;                                                                                              \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                \
        }                                                                                                              \
    } while (0)

    bool Near(float a, float b) {
        return std::fabs(a - b) < 1e-4f;
    }

    using Smoothing = smo::config::Settings::Smoothing;
    using smo::clock::TicksPerLogicStep;
    using smo::clock::TicksPerSecond;

    /* Runs `frames` frames of `ticksPerFrame` each and returns how many game ticks ran. */
    int RunFrames(smo::clock::FrameClock& clock, std::uint64_t& now, std::uint64_t ticksPerFrame, int frames) {
        int ticks = 0;
        for (int i = 0; i < frames; ++i) {
            now += ticksPerFrame;
            clock.Advance(now);
            ticks += clock.runCalcThisFrame;
        }
        return ticks;
    }

    void TestClock() {
        /* 60 Hz: every frame is a game tick, drawn at the newest state. */
        {
            smo::clock::FrameClock clock;
            std::uint64_t now = 1'000'000;
            CHECK(RunFrames(clock, now, TicksPerLogicStep, 600) == 600);
            CHECK(Near(clock.leftover, 0.f));
            CHECK(Near(clock.frameTicks, 1.f));
        }
        /* 120 Hz: ticks alternate, and the in-between frame is drawn halfway. */
        {
            smo::clock::FrameClock clock;
            std::uint64_t now = 1'000'000;
            RunFrames(clock, now, TicksPerLogicStep / 2, 1);
            CHECK(RunFrames(clock, now, TicksPerLogicStep / 2, 1200) == 600);
            now += TicksPerLogicStep / 2;
            clock.Advance(now);
            bool first = clock.runCalcThisFrame;
            float firstLeftover = clock.leftover;
            now += TicksPerLogicStep / 2;
            clock.Advance(now);
            CHECK(first != clock.runCalcThisFrame);
            CHECK(Near(first ? clock.leftover : firstLeftover, 0.5f));
            CHECK(Near(clock.frameTicks, 0.5f));
        }
        /* 144 Hz over one second: still 60 ticks (give or take the partial step at the edges). */
        {
            smo::clock::FrameClock clock;
            std::uint64_t now = 1'000'000;
            int ticks = RunFrames(clock, now, TicksPerSecond / 144, 144);
            CHECK(ticks >= 59 && ticks <= 61);
        }
        /* Below 60 fps: at most one tick per frame, so the game slows down rather than skipping. */
        {
            smo::clock::FrameClock clock;
            std::uint64_t now = 1'000'000;
            CHECK(RunFrames(clock, now, TicksPerSecond / 30, 30) == 30);
            CHECK(Near(clock.frameTicks, 2.f));
        }
        /* A long stall doesn't fast-forward: the debt is capped at two steps. */
        {
            smo::clock::FrameClock clock;
            std::uint64_t now = 1'000'000;
            RunFrames(clock, now, TicksPerLogicStep, 10);
            now += TicksPerSecond * 10;
            clock.Advance(now);
            CHECK(clock.runCalcThisFrame);
            CHECK(clock.accumulator == TicksPerLogicStep);
            CHECK(Near(clock.frameTicks, 2.f));
        }
        /* Blend weight per mode, on an in-between frame at 120 Hz (leftover 0.5). */
        {
            using smo::clock::BlendWeight;
            CHECK(Near(BlendWeight(Smoothing::Interpolate, 0.5f, 1.f), 0.5f));
            CHECK(Near(BlendWeight(Smoothing::Extrapolate, 0.5f, 1.f), 1.5f));
            CHECK(Near(BlendWeight(Smoothing::Extrapolate, 0.5f, 0.5f), 1.25f)); /* max_extrapolation halves the guess */
            CHECK(Near(BlendWeight(Smoothing::Extrapolate, 0.5f, 0.f), 1.f));
            CHECK(Near(BlendWeight(Smoothing::Off, 0.5f, 1.f), 1.f));
        }
    }

    std::vector<std::string> s_Warnings;

    void CollectWarning(const char* format, const char* arg) {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), format, arg);
        s_Warnings.emplace_back(buffer);
    }

    smo::config::Settings ParseText(const char* text) {
        smo::config::Settings settings;
        std::string copy = text;
        s_Warnings.clear();
        smo::config::parse::Parse(copy.data(), settings, CollectWarning);
        return settings;
    }

    void TestConfig() {
        using smo::config::parse::MaxFps;
        using smo::config::parse::MinFps;
        using BoneBlend = smo::config::Settings::BoneBlend;
        auto all = [](const smo::config::Settings& s, Smoothing c, Smoothing m, Smoothing e) {
            return s.camera == c && s.models == m && s.effects == e;
        };
        const auto I = Smoothing::Interpolate, X = Smoothing::Extrapolate, O = Smoothing::Off;

        auto s = ParseText("fps = 144\ninterpolation = extrapolate\n");
        CHECK(s.fps == 144 && all(s, X, X, X) && s_Warnings.empty());

        s = ParseText("fps=90\r\ninterpolation=off\r\n");
        CHECK(s.fps == 90 && all(s, O, O, O) && s_Warnings.empty());

        s = ParseText("  fps   =   120   # my monitor\n# a comment line\n\ninterpolation = on");
        CHECK(s.fps == 120 && all(s, I, I, I) && s_Warnings.empty());

        s = ParseText("interpolation = camera\n");
        CHECK(all(s, X, I, I) && s_Warnings.empty());

        /* Per-system keys win over the preset, whichever comes first in the file. */
        s = ParseText("models = extrapolate\ninterpolation = on\ncamera = off\n");
        CHECK(all(s, O, X, I) && s_Warnings.empty());
        s = ParseText("interpolation = extrapolate\neffects = interpolate\n");
        CHECK(all(s, X, X, I));

        s = ParseText("bone_blend = rotation\nmax_bone_turn = 40.5\nmax_extrapolation = 0.5\n");
        CHECK(s.boneBlend == BoneBlend::Rotation && Near(s.maxBoneTurnDegrees, 40.5f) &&
              Near(s.maxExtrapolation, 0.5f) && s_Warnings.empty());
        s = ParseText("max_bone_turn = 999\nmax_extrapolation = 7\n");
        CHECK(Near(s.maxBoneTurnDegrees, 180.f) && Near(s.maxExtrapolation, 1.f));
        s = ParseText("max_bone_turn = 0\n");
        CHECK(Near(s.maxBoneTurnDegrees, 0.f));
        s = ParseText("bone_blend = sideways\nmax_bone_turn = lots\nmax_extrapolation = 1x\n");
        CHECK(s.boneBlend == BoneBlend::Auto && Near(s.maxBoneTurnDegrees, 25.f) && Near(s.maxExtrapolation, 1.f) &&
              s_Warnings.size() == 3);

        s = ParseText("fps = 1000\n");
        CHECK(s.fps == MaxFps);
        s = ParseText("fps = 30\n");
        CHECK(s.fps == MinFps);

        s = ParseText("fps = abc\n");
        CHECK(s.fps == 120 && s_Warnings.size() == 1 && s_Warnings[0].find("\"abc\"") != std::string::npos);
        s = ParseText("fps = 12x\n");
        CHECK(s.fps == 120 && s_Warnings.size() == 1);
        s = ParseText("fps =\n");
        CHECK(s.fps == 120 && s_Warnings.size() == 1);

        s = ParseText("interpolation = maybe\ncamera = sideways\n");
        CHECK(all(s, I, I, I) && s_Warnings.size() == 2);

        s = ParseText("speed = 2\n");
        CHECK(s_Warnings.size() == 1 && s_Warnings[0].find("unknown key \"speed\"") != std::string::npos);

        s = ParseText("");
        CHECK(s.fps == 120 && all(s, I, I, I) && s.boneBlend == BoneBlend::Auto && s_Warnings.empty());

        /* A later valid line wins; an invalid one keeps what came before. */
        s = ParseText("fps = 144\nfps = nope\n");
        CHECK(s.fps == 144);

        CHECK(!std::strcmp(smo::config::parse::SmoothingName(X), "extrapolate"));
        CHECK(!std::strcmp(smo::config::parse::BoneBlendName(BoneBlend::Rotation), "rotation"));
        using FramePacing = smo::config::Settings::FramePacing;
        s = ParseText("frame_pacing = measured\n");
        CHECK(s.framePacing == FramePacing::Measured && s_Warnings.empty());
        s = ParseText("frame_pacing = wobbly\n");
        CHECK(s.framePacing == FramePacing::Smooth && s_Warnings.size() == 1);
    }

    struct TestEntry {
        std::atomic<uintptr_t> key;
        int value;
    };

    void TestHistory() {
        using namespace smo::history;

        float prev[16] = {}, curr[16] = {};
        curr[TranslationOffset] = 999.f;
        CHECK(!IsTeleport(prev, curr));
        curr[TranslationOffset] = 1001.f;
        CHECK(IsTeleport(prev, curr));
        curr[TranslationOffset] = 600.f;
        curr[TranslationOffset + 1] = 800.f; /* 1000 exactly: not a teleport */
        CHECK(!IsTeleport(prev, curr));

        float a[3] = { 0.f, 10.f, -4.f }, b[3] = { 10.f, 20.f, 4.f }, out[3];
        Blend(out, a, b, 3, 0.5f);
        CHECK(Near(out[0], 5.f) && Near(out[1], 15.f) && Near(out[2], 0.f));
        Blend(out, a, b, 3, 1.5f); /* extrapolation */
        CHECK(Near(out[0], 15.f) && Near(out[1], 25.f) && Near(out[2], 8.f));

        /* Rigid blending: a bone turning 30 degrees per tick about Z, scaled 2x, moving +10 in X. */
        {
            auto make = [](float* m, float degrees, float scale, float tx) {
                float r = degrees * 3.14159265f / 180.f, c = std::cos(r), s = std::sin(r);
                float cols[16] = { c * scale, s * scale, 0, 0, -s * scale, c * scale, 0, 0, 0, 0, scale, 0, tx, 0, 0, 1 };
                std::memcpy(m, cols, sizeof(cols));
            };
            float p[16], c[16], out[16], want[16];
            make(p, 0.f, 2.f, 0.f);
            make(c, 30.f, 2.f, 10.f);

            Quat q;
            float sc[3];
            CHECK(Decompose(c, q, sc) && Near(sc[0], 2.f) && Near(sc[2], 2.f));

            BlendRigid(out, p, c, 0.5f); /* interpolation: 15 degrees */
            make(want, 15.f, 2.f, 5.f);
            bool ok = true;
            for (int i = 0; i < 16; ++i)
                ok &= Near(out[i], want[i]);
            CHECK(ok);

            BlendRigid(out, p, c, 1.5f); /* extrapolation: keeps turning to 45, stays a rotation */
            make(want, 45.f, 2.f, 15.f);
            ok = true;
            for (int i = 0; i < 16; ++i)
                ok &= Near(out[i], want[i]);
            CHECK(ok);

            /* Linear blending of the same matrices shrinks the basis; rigid blending doesn't. */
            Blend(out, p, c, 16, 1.5f);
            float linearLen = std::sqrt(out[0] * out[0] + out[1] * out[1]);
            BlendRigid(out, p, c, 1.5f);
            float rigidLen = std::sqrt(out[0] * out[0] + out[1] * out[1]);
            CHECK(!Near(linearLen, 2.f) && Near(rigidLen, 2.f));

            /* Turn angle: 30 degrees -> cos(15). */
            CHECK(Near(MinHalfTurnCos(p, c, 1), std::cos(15.f * 3.14159265f / 180.f)));

            /* A mirrored basis isn't a rotation: falls back to the linear blend. */
            float mirrored[16];
            make(mirrored, 30.f, 2.f, 10.f);
            mirrored[8 + 2] = -2.f;
            CHECK(!Decompose(mirrored, q, sc));
            BlendRigid(out, p, mirrored, 0.5f);
            Blend(want, p, mirrored, 16, 0.5f);
            ok = true;
            for (int i = 0; i < 16; ++i)
                ok &= Near(out[i], want[i]);
            CHECK(ok);

            /* Rotations past 180 degrees apart go the short way round. */
            make(p, 170.f, 1.f, 0.f);
            make(c, -170.f, 1.f, 0.f);
            BlendRigid(out, p, c, 0.5f);
            make(want, 180.f, 1.f, 0.f);
            CHECK(Near(out[0], want[0]) && Near(out[1], want[1]));
        }

        /* Lookups, inserts and misses. */
        {
            static PointerTable<TestEntry, 1 << 10> table;
            int objects[100];
            for (int& o : objects)
                CHECK(table.Find(&o, true) != nullptr);
            for (int& o : objects)
                CHECK(table.Find(&o, false) == table.Find(&o, true));
            int stranger;
            CHECK(table.Find(&stranger, false) == nullptr);
        }
        /* A full probe window fails instead of looping or overwriting. */
        {
            static PointerTable<TestEntry, 8> tiny;
            static int objects[9];
            for (int i = 0; i < 8; ++i)
                CHECK(tiny.Find(&objects[i], true) != nullptr);
            CHECK(tiny.Find(&objects[8], true) == nullptr);
        }
        /* Concurrent inserts of the same keys from several threads end up in one entry per key. */
        {
            static PointerTable<TestEntry, 1 << 12> shared;
            static int objects[1000];
            TestEntry* seen[8][1000];
            std::vector<std::thread> threads;
            for (int t = 0; t < 8; ++t)
                threads.emplace_back([t, &seen] {
                    for (int i = 0; i < 1000; ++i)
                        seen[t][i] = shared.Find(&objects[(i * 7 + t * 13) % 1000], true);
                });
            for (auto& thread : threads)
                thread.join();
            /* Every object got an entry, and every thread was handed that same entry for it. */
            bool consistent = true;
            for (int i = 0; i < 1000; ++i)
                consistent &= shared.Find(&objects[i], false) != nullptr;
            for (int t = 0; t < 8; ++t)
                for (int i = 0; i < 1000; ++i)
                    consistent &= seen[t][i] == shared.Find(&objects[(i * 7 + t * 13) % 1000], false);
            CHECK(consistent);
        }
    }
}

namespace {
    /* Deterministic timer noise: uniform in [-amount, +amount]. */
    struct Noise {
        std::uint64_t state = 0x9e3779b97f4a7c15ull;
        std::int64_t Next(std::int64_t amount) {
            state = state * 6364136223846793005ull + 1442695040888963407ull;
            return static_cast<std::int64_t>((state >> 33) % static_cast<std::uint64_t>(2 * amount + 1)) - amount;
        }
    };

    /*
     * Frames presented on a steady display clock (`frameTicks` apart) whose timestamps are each read
     * with +/- `noise` jitter: how real timer noise behaves, unlike noise in each duration, which
     * would add up. Records ticks, breaks in a strict every-other-frame pattern, and the spread of the
     * blend on in-between frames.
     */
    struct PacingRun {
        int ticks = 0;
        int patternBreaks = 0;
        float inBetweenSpread = 0.f; /* max - min leftover over in-between frames */
    };

    PacingRun RunPaced(bool smooth, int targetFps, std::uint64_t frameTicks, std::int64_t noise, int frames) {
        smo::clock::FrameClock clock;
        Noise rng;
        const std::uint64_t base = 1'000'000;
        PacingRun run;
        bool previous = true;
        float lo = 2.f, hi = -1.f;
        for (int i = 1; i <= frames; ++i) {
            clock.Advance(base + i * frameTicks + rng.Next(noise), targetFps, smooth);
            run.ticks += clock.runCalcThisFrame;
            if (i > 20) {
                run.patternBreaks += clock.runCalcThisFrame == previous;
                if (!clock.runCalcThisFrame) {
                    lo = std::fmin(lo, clock.leftover);
                    hi = std::fmax(hi, clock.leftover);
                }
            }
            previous = clock.runCalcThisFrame;
        }
        run.inBetweenSpread = hi - lo;
        return run;
    }

    void TestPacing() {
        using smo::clock::TicksPerSecond;
        const std::uint64_t frame120 = TicksPerSecond / 120;
        const std::int64_t noise = static_cast<std::int64_t>(frame120 / 10); /* +/-10% timestamp jitter */

        /* Smooth pacing at 120 Hz: a tick every other frame, and the in-between blend doesn't wobble. */
        PacingRun smooth = RunPaced(true, 120, frame120, noise, 2400);
        CHECK(smooth.ticks >= 1199 && smooth.ticks <= 1201);
        CHECK(smooth.patternBreaks == 0);
        CHECK(smooth.inBetweenSpread < 0.05f);

        /* Measured pacing with the same jitter: the blend wobbles by a lot more. That's what smoothing fixes. */
        PacingRun measured = RunPaced(false, 120, frame120, noise, 2400);
        CHECK(measured.ticks >= 1199 && measured.ticks <= 1201);
        CHECK(measured.inBetweenSpread > 4 * smooth.inBetweenSpread);

        /* A display slightly faster than configured (120.04 Hz): game time still follows real time. */
        const std::uint64_t frame12004 = TicksPerSecond * 100 / 12004;
        const int frames = 120 * 60;
        PacingRun fast = RunPaced(true, 120, frame12004, 0, frames);
        double realTicks = static_cast<double>(frames) * static_cast<double>(frame12004) /
                           static_cast<double>(smo::clock::TicksPerLogicStep);
        CHECK(std::fabs(fast.ticks - realTicks) <= 2.0);

        /* A sustained slowdown (100 fps against fps = 120) isn't smoothed away: the game keeps real speed. */
        PacingRun slow = RunPaced(true, 120, TicksPerSecond / 100, 0, 1000);
        CHECK(slow.ticks >= 599 && slow.ticks <= 601);

        /* 144 Hz is 12 frames per 5 ticks: with jitter, the tick pattern and the blends repeat every 12 frames. */
        {
            smo::clock::FrameClock clock;
            Noise rng;
            const std::uint64_t frame144 = TicksPerSecond / 144;
            std::vector<bool> ran;
            std::vector<float> leftover;
            for (int i = 1; i <= 1440; ++i) {
                clock.Advance(1'000'000 + i * frame144 + rng.Next(static_cast<std::int64_t>(frame144 / 10)), 144, true);
                ran.push_back(clock.runCalcThisFrame);
                leftover.push_back(clock.leftover);
            }
            int ticks = 0, mismatches = 0;
            float worst = 0.f;
            for (size_t i = 0; i < ran.size(); ++i) {
                ticks += ran[i];
                if (i >= 24) {
                    mismatches += ran[i] != ran[i - 12];
                    worst = std::fmax(worst, std::fabs(leftover[i] - leftover[i - 12]));
                }
            }
            CHECK(ticks >= 599 && ticks <= 601);
            CHECK(mismatches == 0);
            CHECK(worst < 0.05f);
        }

        /* After a 5 s loading stall, pacing re-centres: the pattern holds and in-between frames return to ~0.75. */
        {
            smo::clock::FrameClock clock;
            Noise rng;
            std::uint64_t t = 1'000'000;
            int breaks = 0;
            bool previous = true;
            float lo = 2.f, hi = -1.f;
            for (int i = 1; i <= 3000; ++i) {
                t += frame120;
                if (i == 1000)
                    t += TicksPerSecond * 5;
                clock.Advance(t + rng.Next(noise), 120, true);
                if (i > 1010) {
                    breaks += clock.runCalcThisFrame == previous;
                    if (!clock.runCalcThisFrame) {
                        lo = std::fmin(lo, clock.leftover);
                        hi = std::fmax(hi, clock.leftover);
                    }
                }
                previous = clock.runCalcThisFrame;
            }
            CHECK(breaks == 0);
            CHECK(lo > 0.6f && hi < 0.9f);
        }

        /* A stall still caps the debt with smooth pacing on. */
        {
            smo::clock::FrameClock clock;
            std::uint64_t now = 1'000'000;
            for (int i = 0; i < 10; ++i)
                clock.Advance(now += frame120, 120, true);
            clock.Advance(now += TicksPerSecond * 5, 120, true);
            CHECK(clock.runCalcThisFrame && clock.accumulator == smo::clock::TicksPerLogicStep);
        }
    }

    void TestConstants() {
        /* The present interval, now written in terms of the compositor's formula, matches the old (fps * 5 + 1) / 3. */
        bool same = true;
        for (int fps = smo::config::parse::MinFps; fps <= smo::config::parse::MaxFps; ++fps)
            same &= smo::nvn::PresentIntervalFor(fps) == (fps * 5 + 1) / 3;
        CHECK(same);
        CHECK(smo::nvn::PresentIntervalFor(120) == 200 && smo::nvn::PresentIntervalFor(144) == 240);

        /* Named layout and tuning constants equal the literals they replaced. */
        CHECK(smo::history::TransformFloats == 16 && smo::history::TranslationOffset == 12);
        CHECK(smo::clock::TicksPerLogicStep == 320000 && smo::clock::MaxStepDebt == 2);
        CHECK(Near(smo::tuning::TeleportDistance, 1000.f));
        CHECK(Near(std::cos(smo::tuning::DegreesToRadians(smo::tuning::CameraCutDegrees)), 0.8660254f));
        CHECK(smo::tuning::BoneArenaBytes / sizeof(float) == 4 * 1024 * 1024);
    }
}

int main() {
    TestClock();
    TestConfig();
    TestHistory();
    TestConstants();
    TestPacing();
    std::printf("%d checks, %d failed\n", s_Checks, s_Failures);
    return s_Failures == 0 ? 0 : 1;
}
