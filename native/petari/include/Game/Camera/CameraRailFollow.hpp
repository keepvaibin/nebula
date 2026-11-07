#pragma once

#include "Game/Camera/Camera.hpp"

class RailRider;

class CameraRailFollow : public Camera {
public:
    CameraRailFollow(const char* pName = "レールフォローカメラ");

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void setParam(s32, s32, f32, f32, f32, s32);

    f32 getStopLength() {
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
