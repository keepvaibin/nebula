#pragma once

#include "Game/Camera/Camera.hpp"

class CameraRaceFollow : public Camera {
public:
    CameraRaceFollow(const char* pName = "CameraRaceFollow");

    virtual void reset() override;
    virtual CameraTargetObj* calc() override;
    virtual void setParamFromChunk(const CameraParamChunk*) override;

    void goRound();

    void setParam(f32 distMin, f32 distMax, f32 angle, bool forcePitch, f32 roundSpeed) {
        mDistMin = distMin;
        mDistMax = distMax;
        mAngleX = angle;
        mIsForcePitch = forcePitch;
        mRoundAngleSpeed = roundSpeed;
    }

    f32 mDistMin;
    f32 mDistMax;
    f32 mAngleX;
    bool mIsForcePitch;
    f32 mRoundAngleSpeed;
};
