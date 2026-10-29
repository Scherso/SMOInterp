#pragma once

/*
 * SMO resolves every NVN entry point at runtime by name, via nvnBootstrapLoader and
 * nvnDeviceGetProcAddress. Intercepting those lookups lets us replace the two calls the game uses
 * to set its present interval: nvnWindowBuilderSetPresentInterval(1) when it creates the window,
 * and nvnWindowSetPresentInterval(1) on scene changes.
 */
namespace smo::nvn {
    /*
     * The Switch emulator's compositor (yuzu's hardware_composer.cpp) reads a swap interval >= 5 as a
     * speed scale of interval / 100 on its 60 Hz base, so it presents at 60 * interval / 100 fps
     * (measured: 50 -> 30, 100 -> 60, 120 -> 72, 144 -> 86). Values <= 0 are ignored by the game's
     * NVN driver.
     */
    constexpr int CompositorBaseRate = 60;
    constexpr int CompositorSpeedScaleDivisor = 100;

    /* The interval that presents at `fps`, rounded to the nearest: 120 -> 200, 144 -> 240. */
    constexpr int PresentIntervalFor(int fps) {
        return (fps * CompositorSpeedScaleDivisor + CompositorBaseRate / 2) / CompositorBaseRate;
    }
    static_assert(PresentIntervalFor(120) == 200 && PresentIntervalFor(144) == 240);

    /* PresentIntervalFor the configured fps. */
    int PresentInterval();

    void InstallHooks();
}
