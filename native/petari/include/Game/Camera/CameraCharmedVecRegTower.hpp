#pragma once

#include "Game/Camera/Camera.hpp"

class CameraCharmedVecRegTower : public Camera {
public:
    CameraCharmedVecRegTower(const char* pName = "CameraCharmedVecRegTower");

    virtual void reset() override;
    virtual CameraTargetObj* calc() override;
    virtual void setParamFromChunk(const CameraParamChunk*) override;

    void setParam(const char* pName, const Vec3f& rWPoint, const Vec3f& rAxis, f32 angleX, f32 angleYMax, f32 dist, bool arrange) {
        mName = pName;
        mWPoint.set(rWPoint);
        mAxis.set(rAxis);
        mAngleX = angleX;
        mAngleYMax = angleYMax;
        mDist = dist;
        mArrange = arrange;
    }

    Vec3f mWPoint;
    Vec3f mAxis;
    f32 mAngleX;
    f32 mAngleYMax;
    f32 mDist;
    bool mArrange;
    const char* mName;
};
