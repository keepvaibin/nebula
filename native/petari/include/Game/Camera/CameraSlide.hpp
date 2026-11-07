#pragma once

#include "Game/Camera/Camera.hpp"

class CameraSlide : public Camera {
public:
    CameraSlide(const char* pName = "スライドカメラ");

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void setParam(const Vec3f& axis, const Vec3f& up, const Vec3f& wPoint, f32 angle, f32 dist) {
        mAxis.set(axis);
        mUp.set(up);
        mWPoint.set(wPoint);
        mAngleA = angle;
        mDist = dist;
    }

    Vec3f mAxis;
    Vec3f mUp;
    Vec3f mWPoint;
    f32 mAngleA;
    f32 mDist;
};
