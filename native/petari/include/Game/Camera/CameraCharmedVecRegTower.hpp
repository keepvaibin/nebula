#pragma once

#include "Game/Camera/Camera.hpp"

class CameraCharmedVecRegTower : public Camera {
public:
    CameraCharmedVecRegTower(const char* pName = "CameraCharmedVecRegTower");

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void setParam(const char* pName, const TVec3f& rWPoint, const TVec3f& rAxis, f32 angleX, f32 angleYMax, f32 dist, bool arrange) {
        mName = pName;
        mWPoint.set(rWPoint);
        mAxis.set(rAxis);
        mAngleX = angleX;
        mAngleYMax = angleYMax;
        mDist = dist;
        mArrange = arrange;
    }

    TVec3f mWPoint;
    TVec3f mAxis;
    f32 mAngleX;
    f32 mAngleYMax;
    f32 mDist;
    bool mArrange;
    const char* mName;
};
