#pragma once

#include "Game/Camera/Camera.hpp"

class CameraSlide : public Camera {
public:
    CameraSlide(const char* pName = "\203\130\203\211\203\103\203\150\203\112\203\201\203\211" /* スライドカメラ */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void setParam(const TVec3f& axis, const TVec3f& up, const TVec3f& wPoint, f32 angle, f32 dist) {
        mAxis.set(axis);
        mUp.set(up);
        mWPoint.set(wPoint);
        mAngleA = angle;
        mDist = dist;
    }

    TVec3f mAxis;
    TVec3f mUp;
    TVec3f mWPoint;
    f32 mAngleA;
    f32 mDist;
};
