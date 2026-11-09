#pragma once

#include "Game/Camera/Camera.hpp"

class CameraTripodPlanet : public Camera {
public:
    CameraTripodPlanet(const char* pName = "三脚惑星カメラ");

    virtual CameraTargetObj* calc() override;
    virtual void setParamFromChunk(const CameraParamChunk*) override;

    void setParam(const Vec3f& wPoint, const Vec3f& axis, const Vec2f& angle, f32 dist) {
        mWPoint.set(wPoint);
        mAxis.set(axis);
        mAngleX = angle.x;
        mAngleY = angle.y;
        mDist = dist;
    }

    Vec3f mAxis;
    Vec3f mWPoint;
    f32 mAngleX;
    f32 mAngleY;
    f32 mDist;
};
