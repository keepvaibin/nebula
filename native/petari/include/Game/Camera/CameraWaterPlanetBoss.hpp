#pragma once

#include "Game/Camera/Camera.hpp"

class CameraWaterPlanetBoss : public Camera {
public:
    CameraWaterPlanetBoss(const char* pName = "\220\205\222\206\203\166\203\211\203\154\203\142\203\147\203\173\203\130" /* 水中プラネットボス */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual bool isEnableToReset() const {
        return true;
    }
    virtual CamTranslatorBase* createTranslator();

    void setParam(f32 distMin, f32 distMax, f32 blendRateMin, bool blendUpEnabled, const TVec3f wPoint, f32 dotFrontMin, f32 dotFrontMax,
                  f32 angleXMin, f32 angleXMax) {
        mDistMin = distMin;
        mDistMax = distMax;
        mSideBlendRateMin = blendRateMin;
        mIsBlendUpEnabled = blendUpEnabled;
        mWPoint.set(wPoint);
        mDotFrontMin = dotFrontMin;
        mDotFrontMax = dotFrontMax;
        mAngleXMin = angleXMin;
        mAngleXMax = angleXMax;
    }

    f32 mDistMin;
    f32 mDistMax;
    f32 mSideBlendRateMin;
    f32 mSideBlendRate;
    s32 mCollideCount;
    TVec3f mLastMoveDir;
    bool mIsBlendUpEnabled;
    TVec3f mWPoint;
    f32 mDotFrontMin;
    f32 mAngleXMin;
    f32 mDotFrontMax;
    f32 mAngleXMax;
    bool mIsRounding;
    s32 mRoundingFrame;
    TVec3f mSide;
};
