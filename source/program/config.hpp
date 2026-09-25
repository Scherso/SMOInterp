#pragma once

/*
 * Settings read once from sd:/SMOInterp/config.ini, i.e. the emulator's sdmc folder
 * (e.g. ~/.local/share/yuzu/sdmc/SMOInterp/config.ini):
 *
 *   fps = 120            # match the display's refresh rate
 *   interpolation = on   # off: show each logic tick as-is (no blending, no added latency)
 *
 * A missing file or key keeps the default.
 */
namespace smo::config {
    struct Settings {
        int fps = 120;
        bool interpolation = true;
    };

    /* Loads on first call; the SD card is only reachable once the game's own init has started. */
    const Settings& Get();
}
