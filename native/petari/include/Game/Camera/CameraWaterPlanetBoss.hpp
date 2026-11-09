#pragma once

#include "Game/Camera/Camera.hpp"

class CameraWaterPlanetBoss : public Camera {
public:
    CameraWaterPlanetBoss(const char* pName = "水中プラネットボス");

    virtual void reset() override;
    virtual CameraTargetObj* calc() override;
    virtual bool isEnableToReset() const override {
        return true;
    }
    virtual void setParamFromChunk(const CameraParamChunk*) override;

    void setParam(f32 distMin, f32 distMax, f32 blendRateMin, bool blendUpEnabled, const Vec3f wPoint, f32 dotFrontMin, f32 dotFrontMax,
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
    Vec3f mLastMoveDir;
    bool mIsBlendUpEnabled;
    Vec3f mWPoint;
    f32 mDotFrontMin;
    f32 mAngleXMin;
    f32 mDotFrontMax;
    f32 mAngleXMax;
    bool mIsRounding;
    s32 mRoundingFrame;
    Vec3f mSide;
};
