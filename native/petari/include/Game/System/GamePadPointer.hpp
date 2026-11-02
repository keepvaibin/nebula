#pragma once

#include "Game/System/GamePad.hpp"
#include <JSystem/JGeometry.hpp>

class GamePadPointer {
public:
    GamePadPointer(const GamePad*);

    void reset();
    void update();
    void getPointingPos(TVec2f*) const;
    void getHorizonVec(TVec2f*) const;
    void getPastPointingPos(TVec2f*, s32) const;
    s32 getEnablePastCount() const;
    void getPointingPosBasedOnScreen(TVec2f*) const;

    const GamePad* mPad;
    TVec2f* mPointingPosArray;
    TVec2f* mHorizonArray;
    s32 mMaxPastCount;
    u32 _10;
    f32 mPosPlayRadius;
    f32 mPosSensitivity;
    f32 mHoriPlayRadius;
    f32 mHoriSensitivity;
    f32 mDistPlayRadius;
    f32 mDistSensitivity;
    s32 mLostSensorTime;
    f32 mDistDisplay;
    s8 mDpdValidFg;
    s32 mValidInScreenTime;
    s32 mInValidOutScreenTime;
    s32 mEnablePastCount;
    bool mIsPointInScreen;
    bool mIsPointerMoved;
};
