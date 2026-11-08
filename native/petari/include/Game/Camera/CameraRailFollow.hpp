#pragma once

#include "Game/Camera/Camera.hpp"

class RailRider;

class CameraRailFollow : public Camera {
public:
    CameraRailFollow(const char* pName = "レールフォローカメラ");

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual void setParamFromChunk(const CameraParamChunk*);

    void setParam(s32 zoneID, s32 railID, f32 stopMargin, f32 railSpeedMax, f32 accel, s32 marginScale);

    f32 getStopLength() const {
        return mRailSpeed * mRailSpeed / (mRailAccel * 2.0f);
    }

    RailRider* mRailRider;
    f32 mRailCoord;
    f32 mRailSpeed;
    f32 mTargetMargin;
    f32 mRailSpeedMax;
    f32 mRailAccel;
    s32 mMarginScale;
};
