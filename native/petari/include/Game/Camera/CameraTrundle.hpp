#pragma once

#include "Game/Camera/Camera.hpp"

class CameraTrundle : public Camera {
public:
    CameraTrundle(const char* pName = "\203\147\203\211\203\223\203\150\203\213\203\112\203\201\203\211" /* トランドルカメラ */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void setParam(const TVec3f& wPoint, const TVec3f& axis, f32 dist, f32 angleX, f32 angleY, f32 dotMax) {
        mWPoint.set(wPoint);
        mAxis.set(axis);
        mDist = dist;
        mAngleX = angleX;
        mAngleY = angleY;
        mDotMax = dotMax;
    }

    TVec3f mWPoint;
    TVec3f mAxis;
    f32 mDist;
    f32 mAngleX;
    f32 mAngleY;
    f32 mDotMax;
};
