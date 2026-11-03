#pragma once

// The game's frames (B-10): the main loop sleeps until the host says the next is due and does the frame's work itself: the
// frame count, what to show (the game's picture or black, committed by show() and taking effect at the next frame), the
// host told.

#include <nebula_game_api.h>

namespace NebulaFrameClock {
    // The game's screen units: viewports, 2D and effect passes count in them; J3DNativeView turns them into fractions of
    // the frame.
    const u16 cScreenUnitsAcross = 640;
    const u16 cScreenUnitsDown = 456;
    // The same height as a float, and the width the layout files are authored for.
    const f32 cScreenHeight = 456.0f;
    const f32 cLayoutWidth = 608.0f;
    // getScreenWidth at 16:9.
    const s32 cWideScreenWidth = 832;

    // The game has its frame: two frames on, and two black ones, the picture shows.
    void startPicture();
    // Frames shown since start.
    u32 count();
    // The main thread sleeps until the next frame and begins it.
    void waitFrame();
    // The same in two steps, for a frame that updates between them.
    void sleepUntilFrame();
    void beginFrame();
    // What the screen shows from the next frame: the game's picture (true) or black.
    void show(bool picture);


    // The picture's shape the game draws for, latched at each frame from the host's output shape: the screen width,
    // the camera's aspect and the layout canvas follow it (wide == 0: 16:9).
    const NebulaDisplay& display();
}  // namespace NebulaFrameClock
