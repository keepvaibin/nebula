#pragma once

#include "Game/Camera/Camera.hpp"

class CameraFrontAndBack : public Camera {
public:
    CameraFrontAndBack(const char* pName = "\225\134\227\240\203\112\203\201\203\211" /* 表裏カメラ */);
    virtual ~CameraFrontAndBack();

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void calcBaseQuat(TQuat4f*, const TVec3f&);
    void calcDipQuat(TQuat4f*, const TVec3f&, const TVec3f&);

    void setParam(const TVec3f& wPoint, const TVec3f& axis, f32 rotateRate, f32 maxDipAngle, f32 dist) {
        mWPoint = wPoint;
        mAxis = axis;
        mRotateRate = rotateRate;
        mMaxDipAngle = maxDipAngle;
        mDist = dist;
    }

    TVec3f mWPoint;
    TVec3f mAxis;
    f32 mRotateRate;
    f32 mMaxDipAngle;
    f32 mDist;
};
