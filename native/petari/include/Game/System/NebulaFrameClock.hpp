#pragma once

// The game's frames: the main loop sleeps until the host says the next is due and does the frame's work itself: the
// frame count, what to show (the game's picture or black, committed by show() and taking effect at the next frame), the
// host told.

#include <nebula_game_api.h>

namespace NebulaFrameClock {
    // The game's 2D coordinates are this tall, and MR::getScreenWidth wide; nothing renders at this size. View rectangles are fractions of it.
    const u16 cScreenLines = NEBULA_AUTHORED_LINES;
    // The same height as a float, and the width the layout files are authored for.
    const f32 cScreenHeight = static_cast< f32 >(NEBULA_AUTHORED_LINES);
    const f32 cLayoutWidth = 608.0f;
    // A whole frame in the texels effects' offsets were authored in (an effect target's share of the frame is sized in
    // them): this many across, cScreenHeight down.
    const f32 cEffectTexelsAcross = 640.0f;
    // getScreenWidth at 16:9.
    const s32 cWideScreenWidth = 832;
    // The game's fixed step: its simulation runs this many steps a second, whatever the display's rate (the host
    // interpolates between them).
    const u32 cStepsPerSecond = NEBULA_STEPS_PER_SECOND;
    // Seconds of play as steps.
    constexpr s32 stepsOfSeconds(s32 seconds) {
        return seconds * static_cast< s32 >(NEBULA_STEPS_PER_SECOND);
    }
    // Milliseconds of play as steps, to the nearest step: a duration that is not a whole number of seconds.
    constexpr s32 stepsOfMilliseconds(s32 milliseconds) {
        return (milliseconds * static_cast< s32 >(NEBULA_STEPS_PER_SECOND) + 500) / 1000;
    }
    // Steps as seconds of play (a fraction).
    constexpr f32 secondsOfSteps(s32 steps) {
        return static_cast< f32 >(steps) / static_cast< f32 >(NEBULA_STEPS_PER_SECOND);
    }
    // Durations the game's data and code write as plain step counts (a nerve's isStep(60), a named sWaitTime = 60) are
    // counts of steps: they name how many steps the behaviour was authored to take, and stay that many at any display
    // rate. Only a conversion between steps and seconds (a once-a-second tick, steps / 60) goes through the two helpers.

    // Frames shown since start.
    u32 count();
    // The game thread sleeps until the next frame and begins it.
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
