#pragma once

/*
 * SMO resolves every NVN entry point at runtime by name, via nvnBootstrapLoader and
 * nvnDeviceGetProcAddress. Intercepting those lookups lets us replace the two calls the game uses
 * to set its present interval: nvnWindowBuilderSetPresentInterval(1) when it creates the window,
 * and nvnWindowSetPresentInterval(1) on scene changes.
 */
namespace smo::nvn {
    /*
     * Eden reads a swap interval >= 5 as a compositor speed of interval / 100, i.e. 0.6 * interval
     * fps (measured: 50 -> 30, 100 -> 60, 120 -> 72, 144 -> 86). Values <= 0 are ignored by the
     * game's NVN driver. So the interval for a target rate is fps * 5 / 3, rounded: 120 -> 200, 144 -> 240.
     */
    int PresentInterval();

    void InstallHooks();
}
