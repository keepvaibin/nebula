#pragma once

#include "Game/Camera/Camera.hpp"

class CameraWaterFollow : public Camera {
public:
    CameraWaterFollow(const char* pName = "\220\205\222\206\203\164\203\110\203\215\201\133" /* 水中フォロー */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual bool isEnableToReset() const {
        return true;
    }
    virtual CamTranslatorBase* createTranslator();

    void setParam(f32 distMin, f32 distMax, f32 blendRateMin) {
        mDistMin = distMin;
        mDistMax = distMax;
        mSideBlendRateMin = blendRateMin;
    }

    f32 mDistMin;
    f32 mDistMax;
    f32 mSideBlendRateMin;
    f32 mSideBlendRate;
    s32 mCollideCount;
    TVec3f mLastMoveDir;
    bool mIsRounding;
    s32 mRoundingFrame;
    TVec3f mSide;
};
