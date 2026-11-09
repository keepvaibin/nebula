#pragma once

#include "Game/Camera/Camera.hpp"

class CameraFooFighter : public Camera {
public:
    CameraFooFighter(const char* pName = "フーファイター");

    virtual void reset() override;
    virtual CameraTargetObj* calc() override;
    virtual void setParamFromChunk(const CameraParamChunk*) override;

    void setParam(f32 distMin, f32 distMax, f32 angleXRateMin) {
        mDistMin = distMin;
        mDistMax = distMax;
        mAngleXRateMin = angleXRateMin;
    }

    f32 mDistMin;
    f32 mDistMax;
    f32 mAngleXRateMin;
    f32 mAngleXRate;
    s32 mCollideCount;
    Vec3f mTargetLastMoveDir;
};
