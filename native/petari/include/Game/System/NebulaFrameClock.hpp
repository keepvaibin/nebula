#pragma once

// The host's frame clock (B-01: the game no longer calls the console's video interface). The host owns the display:
// it calls nebula_game_vi_retrace once per frame it shows (any thread), and the clock delivers it to the game on the
// simulation thread: the frame count, the per-frame hooks, what to show (the game's picture or black, committed by
// show() and taking effect at the next frame), the host told what it shows, and waitFrame's sleepers woken.

#include <revolution/types.h>

#include <nebula_game_api.h>

namespace NebulaFrameClock {
    typedef void (*Hook)(u32 frame);

    // Starts the clock (idempotent).
    void init();
    // Frames shown since start.
    u32 count();
    // Sleeps the calling game thread until the next frame.
    void waitFrame();
    // What the screen shows from the next frame: the game's picture (true) or black.
    void show(bool picture);
    // The hooks run at each frame before and after it is shown; each returns the hook it replaces.
    Hook setPreHook(Hook hook);
    Hook setPostHook(Hook hook);

    // Screen saver, as the console counts it: frames since the controllers' last input (resetIdle) reach the dimming
    // time while dimming is enabled (and the system setting allows it). The game then lowers its volume.
    bool enableDimming(bool enable);
    u32 framesUntilDimming();
    void resetIdle();

    // The picture's shape the game draws for, latched at each frame from the host's output shape: the screen width,
    // the camera's aspect and the layout canvas follow it (wide == 0: the Wii's 16:9).
    const NebulaDisplay& display();
}  // namespace NebulaFrameClock
