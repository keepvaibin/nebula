#pragma once

#include "Game/Camera/Camera.hpp"

class CameraInnerCylinder : public Camera {
public:
    CameraInnerCylinder(const char* pName = "CameraInnerCylinder");

    virtual void reset() override;
    virtual CameraTargetObj* calc() override;
    virtual void setParamFromChunk(const CameraParamChunk*) override;

    void setParam(const Vec3f& rWPoint, const Vec3f& rAxis, const Vec2f& angle, f32 dist, f32 sideDist) {
        mWPoint.set(rWPoint);
        mAxis.set(rAxis);
        mAngleA = angle.x;
        mAngleB = angle.y;
        mDist = dist;
        mSideDist = sideDist;
    }

    Vec3f mWPoint;
    Vec3f mAxis;
    f32 mAngleA;
    f32 mAngleB;
    f32 mDist;
    f32 mSideDist;
    Vec3f mLastWatchPoint;
    bool mIsNotInitialized;
};
