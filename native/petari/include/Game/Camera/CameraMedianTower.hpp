#pragma once

#include "Game/Camera/Camera.hpp"

class CameraMedianTower : public Camera {
public:
    CameraMedianTower(const char* pName = "\222\206\223\137\223\203\203\112\203\201\203\211" /* 中点塔カメラ */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void calcFovyPos(TVec3f*, const TVec3f&, const TVec3f&, const TVec3f&);
    void limitAngularVelocity(TVec3f*, TVec3f*, const TVec3f&, const TVec3f&, const TVec3f&);
    void calcDistance(TVec3f*, const TVec3f&);
    void updateReset(const TVec3f&, const TVec3f&, const TVec3f&);

    void setParam(const char* pName, const TVec3f& wPoint, const TVec3f& axis, f32 angle, f32 dist, f32 maxDist, f32 partialRate) {
        mString = pName;
        mWPoint.set(wPoint);
        mAxis.set(axis);
        mAngleA = angle;
        mDist = dist;
        mMaxFocusDist = maxDist;
        mPartialRate = partialRate;
    }

    const char* mString;
    TVec3f mWPoint;
    TVec3f mAxis;
    f32 mAngleA;
    f32 mDist;
    f32 mMaxFocusDist;
    f32 mPartialRate;
    TVec3f mBaseFront;
    bool mIsResetting;
    s32 mInterpolateTime;
};
