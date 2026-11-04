#pragma once

#include "Game/System/PlayerInput.hpp"
#include <JSystem/JGeometry.hpp>

// Where the player points, this frame.
class InputPointer {
public:
    InputPointer(const PlayerInput*);

    void update();
    void getPointingPos(TVec2f*) const;
    void getHorizonVec(TVec2f*) const;
    void getPastPointingPos(TVec2f*, s32) const;
    s32 getEnablePastCount() const;
    void getPointingPosBasedOnScreen(TVec2f*) const;

    const PlayerInput* mPad;
    TVec2f mPos;
    TVec2f mHorizon;
    f32 mDistDisplay;
    bool mIsPointInScreen;
};
