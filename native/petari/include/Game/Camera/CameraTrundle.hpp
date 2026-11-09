#pragma once

#include "Game/Camera/Camera.hpp"

class CameraTrundle : public Camera {
public:
    CameraTrundle(const char* pName = "トランドルカメラ");

    virtual CameraTargetObj* calc() override;
    virtual void setParamFromChunk(const CameraParamChunk*) override;

    void setParam(const Vec3f& wPoint, const Vec3f& axis, f32 dist, f32 angleX, f32 angleY, f32 dotMax) {
        mWPoint.set(wPoint);
        mAxis.set(axis);
        mDist = dist;
        mAngleX = angleX;
        mAngleY = angleY;
        mDotMax = dotMax;
    }

    Vec3f mWPoint;
    Vec3f mAxis;
    f32 mDist;
    f32 mAngleX;
    f32 mAngleY;
    f32 mDotMax;
};
