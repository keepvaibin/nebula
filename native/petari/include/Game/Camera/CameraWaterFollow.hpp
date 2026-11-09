#pragma once

#include "Game/Camera/Camera.hpp"

class CameraWaterFollow : public Camera {
public:
    CameraWaterFollow(const char* pName = "水中フォロー");

    virtual void reset() override;
    virtual CameraTargetObj* calc() override;
    virtual bool isEnableToReset() const override {
        return true;
    }
    virtual void setParamFromChunk(const CameraParamChunk*) override;

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
    Vec3f mLastMoveDir;
    bool mIsRounding;
    s32 mRoundingFrame;
    Vec3f mSide;
};
