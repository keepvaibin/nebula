#pragma once

#include <nebula_geometry.hpp>

class PlayerInput;

class InputStick {
public:
    // mHold, mTrigger and mRelease bits.
    enum Direction {
        Direction_Up = 0x1,
        Direction_Down = 0x2,
        Direction_Right = 0x4,
        Direction_Left = 0x8,
    };

    InputStick(const PlayerInput*);

    void update();

    const PlayerInput* mPad;
    Vec2f mStick;
    f32 mSpeed;
    u32 mHold;
    u32 mTrigger;
    u32 mRelease;
    // As PlayerInputReader's: directions taken or let go in frames with no world step, for the next step (J22-3).
    u32 mCarriedTrigger;
    u32 mCarriedRelease;
};
