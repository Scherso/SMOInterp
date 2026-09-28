#pragma once

/*
 * Settings read once from sd:/SMOInterp/config.ini, i.e. the emulator's sdmc folder
 * (e.g. ~/.local/share/yuzu/sdmc/SMOInterp/config.ini):
 *
 *   fps = 120            # match the display's refresh rate
 *   interpolation = on   # on:          blend the last two ticks (smooth, up to one tick of latency)
 *                        # extrapolate: continue past the newest tick (smooth, no added latency,
 *                        #              brief overshoot when motion changes suddenly)
 *                        # off:         show each logic tick as-is (60 Hz motion, no added latency)
 *
 * A missing file or key keeps the default.
 */
namespace smo::config {
    struct Settings {
        int fps = 120;
        enum class Smoothing { Off, Interpolate, Extrapolate };
        Smoothing smoothing = Smoothing::Interpolate;
    };

    /* Loads on first call; the SD card is only reachable once the game's own init has started. */
    const Settings& Get();
}
