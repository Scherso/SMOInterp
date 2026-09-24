#pragma once

/*
 * Camera interpolation. The renderer reads each view's sead::LookAtCamera; every rendered frame we
 * write a blend of the last two logic ticks' cameras into it just before the scene's
 * preDrawGraphics copies it for drawing, and put the true camera back before the next tick's
 * simulation so the game never sees a blended value.
 */
namespace smo::camera {
    /* Record the camera the simulation just produced as the newest tick. */
    void Capture(const void* sceneCameraInfo);

    /* Write prev + (curr - prev) * alpha into every tracked view. */
    void Apply(const void* sceneCameraInfo, float alpha);

    /* Undo Apply, restoring the simulation's own camera values. */
    void Restore();

    /* Forget all history, e.g. when the owning scene is destroyed. */
    void Reset();
}
