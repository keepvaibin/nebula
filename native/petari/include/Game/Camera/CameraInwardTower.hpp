#pragma once

#include "Game/Camera/Camera.hpp"

class CameraInwardTower : public Camera {
public:
    CameraInwardTower(const char* pName = "塔内部カメラ");

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual bool isEnableToReset() const {
        return true;
    }
    virtual void setParamFromChunk(const CameraParamChunk*);

    void calcIdealPose();

    void setParam(const Vec3f& rWPoint, const Vec3f& rAxis, const Vec2f& angle, f32 dist) {
        mWPoint.set(rWPoint);
        mAxis.set(rAxis);
        mAngleX = angle.x;
        mAngleMax = angle.y;
        mDist = dist;
    }

    Vec3f mWPoint;
    Vec3f mAxis;
    f32 mAngleX;
    f32 mAngleMax;
    f32 mDist;
    Vec3f mPos;
    f32 mAngleY;
    bool mIsResetting;
};
