#pragma once

#include "Game/Camera/Camera.hpp"

class CameraFrontAndBack : public Camera {
public:
    CameraFrontAndBack(const char* pName = "表裏カメラ");

    virtual CameraTargetObj* calc();
    virtual void setParamFromChunk(const CameraParamChunk*);

    void calcBaseQuat(Quat4f*, const Vec3f&);
    void calcDipQuat(Quat4f*, const Vec3f&, const Vec3f&);

    void setParam(const Vec3f& wPoint, const Vec3f& axis, f32 rotateRate, f32 maxDipAngle, f32 dist) {
        mWPoint = wPoint;
        mAxis = axis;
        mRotateRate = rotateRate;
        mMaxDipAngle = maxDipAngle;
        mDist = dist;
    }

    Vec3f mWPoint;
    Vec3f mAxis;
    f32 mRotateRate;
    f32 mMaxDipAngle;
    f32 mDist;
};
