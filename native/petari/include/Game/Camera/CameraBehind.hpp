#pragma once

#include "Game/Camera/Camera.hpp"

class CameraBehind : public Camera {
public:
    CameraBehind(const char* pName = "後方カメラ");

    virtual void reset() override;
    virtual CameraTargetObj* calc() override;
    virtual void setParamFromChunk(const CameraParamChunk*) override;

    void calcPosAndUp(f32 accel);

    void setParam(const Vec2f& angle, f32 dist, f32 rate) {
        mAngleX = angle.x;
        mAngleY = angle.y;
        mDist = dist;
        mRate = rate;
    }

    f32 mAngleX;
    f32 mAngleY;
    f32 mDist;
    f32 mRate;
};
