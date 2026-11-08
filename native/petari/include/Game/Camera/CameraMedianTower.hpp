#pragma once

#include "Game/Camera/Camera.hpp"

class CameraMedianTower : public Camera {
public:
    CameraMedianTower(const char* pName = "中点塔カメラ");

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual void setParamFromChunk(const CameraParamChunk*);

    void calcFovyPos(Vec3f*, const Vec3f&, const Vec3f&, const Vec3f&);
    void limitAngularVelocity(Vec3f*, Vec3f*, const Vec3f&, const Vec3f&, const Vec3f&);
    void calcDistance(Vec3f*, const Vec3f&);
    void updateReset(const Vec3f&, const Vec3f&, const Vec3f&);

    void setParam(const char* pName, const Vec3f& wPoint, const Vec3f& axis, f32 angle, f32 dist, f32 maxDist, f32 partialRate) {
        mString = pName;
        mWPoint.set(wPoint);
        mAxis.set(axis);
        mAngleA = angle;
        mDist = dist;
        mMaxFocusDist = maxDist;
        mPartialRate = partialRate;
    }

    const char* mString;
    Vec3f mWPoint;
    Vec3f mAxis;
    f32 mAngleA;
    f32 mDist;
    f32 mMaxFocusDist;
    f32 mPartialRate;
    Vec3f mBaseFront;
    bool mIsResetting;
    s32 mInterpolateTime;
};
