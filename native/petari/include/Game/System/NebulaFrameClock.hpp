#pragma once

// The game's frames (B-10): the main loop sleeps until the host says the next is due and does the frame's work itself: the
// frame count, what to show (the game's picture or black, committed by show() and taking effect at the next frame), the
// host told.

#include <nebula_game_api.h>

namespace NebulaFrameClock {
    // The game's frame: the size it draws in, its viewport's units.
    const u16 cFrameWidth = 640;
    const u16 cFrameHeight = 456;

    // The game has its frame: two frames on, and two black ones, the picture shows.
    void startPicture();
    // Frames shown since start.
    u32 count();
    // The main thread sleeps until the next frame and begins it.
    void waitFrame();
    // What the screen shows from the next frame: the game's picture (true) or black.
    void show(bool picture);

    // Screen saver, as the console counts it: frames since the controllers' last input (resetIdle) reach the dimming
    // time while dimming is enabled (and the system setting allows it). The game then lowers its volume.
    bool enableDimming(bool enable);
    u32 framesUntilDimming();
    void resetIdle();

    // The picture's shape the game draws for, latched at each frame from the host's output shape: the screen width,
    // the camera's aspect and the layout canvas follow it (wide == 0: the Wii's 16:9).
    const NebulaDisplay& display();
}  // namespace NebulaFrameClock
