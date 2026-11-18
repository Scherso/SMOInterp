#pragma once

/*
 * Settings read once from sd:/SMOInterp/config.ini, i.e. the emulator's sdmc folder
 * (e.g. ~/.local/share/yuzu/sdmc/SMOInterp/config.ini):
 *
 *   fps = 120                  # match the display's refresh rate
 *   interpolation = on         # preset for camera / models / effects:
 *                              #   on:          all interpolate (smooth, up to one tick of latency)
 *                              #   camera:      camera extrapolates, the rest interpolates
 *                              #   extrapolate: all extrapolate (experimental)
 *                              #   off:         show each tick as-is (60 Hz motion)
 *   camera = interpolate       # per-system overrides of the preset:
 *   models = interpolate       #   interpolate | extrapolate | off
 *   effects = interpolate
 *   bone_blend = auto          # auto (rotation when extrapolating) | linear | rotation
 *   max_bone_turn = 25         # degrees per tick; faster-turning models aren't extrapolated (0 = no limit)
 *   max_extrapolation = 1.0    # how far ahead extrapolation guesses, in ticks (0 to 1)
 *   frame_pacing = smooth      # smooth (frame times locked to fps, drift paid back slowly) | measured
 *
 * Per-system keys override the preset wherever they appear in the file. A missing file or key
 * keeps the default.
 */
namespace smo::config {
    struct Settings {
        /* How one system draws in-between frames. */
        enum class Smoothing { Off, Interpolate, Extrapolate };
        /* How bone matrices are blended: component-wise, or as rotation + scale + translation. */
        enum class BoneBlend { Auto, Linear, Rotation };
        /* How frame times feed the clock: smoothed to the display rate, or as measured. */
        enum class FramePacing { Smooth, Measured };

        int fps = 120;
        Smoothing camera = Smoothing::Interpolate;
        Smoothing models = Smoothing::Interpolate;
        Smoothing effects = Smoothing::Interpolate;
        BoneBlend boneBlend = BoneBlend::Auto;
        float maxBoneTurnDegrees = 25.f;
        float maxExtrapolation = 1.f;
        FramePacing framePacing = FramePacing::Smooth;
    };

    /* Loads on first call; the SD card is only reachable once the game's own init has started. */
    const Settings& Get();
}
