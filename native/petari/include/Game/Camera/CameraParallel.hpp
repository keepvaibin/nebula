#pragma once

#include "Game/Camera/Camera.hpp"

class CameraParallel : public Camera {
public:
    CameraParallel(const char* pName = "平行カメラ");

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual void setParamFromChunk(const CameraParamChunk*);
    virtual bool isEnableToRoundLeft() const;
    virtual bool isEnableToRoundRight() const;
    virtual bool isEnableToReset() const;

    void calcIdealPose();
    void calcRound();

    void setParam(const Vec2f& angle, f32 dist, bool enabled) {
        mAngleX = angle.x;
        mAngleY = angle.y;
        mDist = dist;
        mIsEnableValid = enabled;
    }

    f32 mAngleX;
    f32 mAngleY;
    f32 mDist;
    bool mIsEnableValid;
    bool mIsRounding;
    f32 mRoundTarget;
    f32 mRoundAngle;
};
