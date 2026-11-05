#pragma once

#include "Game/System/PlayerInput.hpp"
#include <nebula_geometry.hpp>

// Where the player points, this frame.
class InputPointer {
public:
    InputPointer(const PlayerInput*);

    void update();
    void getPointingPos(Vec2f*) const;
    void getHorizonVec(Vec2f*) const;
    void getPastPointingPos(Vec2f*, s32) const;
    s32 getEnablePastCount() const;
    void getPointingPosBasedOnScreen(Vec2f*) const;

    const PlayerInput* mPad;
    Vec2f mPos;
    Vec2f mHorizon;
    f32 mDistDisplay;
    bool mIsPointInScreen;
};
