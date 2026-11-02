#pragma once

#include "Game/Camera/Camera.hpp"

class CameraInnerCylinder : public Camera {
public:
    CameraInnerCylinder(const char* pName = "CameraInnerCylinder");

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void setParam(const TVec3f& rWPoint, const TVec3f& rAxis, const TVec2f& angle, f32 dist, f32 sideDist) {
        mWPoint.set(rWPoint);
        mAxis.set(rAxis);
        mAngleA = angle.x;
        mAngleB = angle.y;
        mDist = dist;
        mSideDist = sideDist;
    }

    TVec3f mWPoint;
    TVec3f mAxis;
    f32 mAngleA;
    f32 mAngleB;
    f32 mDist;
    f32 mSideDist;
    TVec3f mLastWatchPoint;
    bool mIsNotInitialized;
};
