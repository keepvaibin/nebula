#pragma once

#include "Game/Camera/Camera.hpp"

class CameraInwardSphere : public Camera {
public:
    CameraInwardSphere(const char* pName = "\213\205\223\340\225\224\203\112\203\201\203\211" /* 球内部カメラ */);
    virtual ~CameraInwardSphere();

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void calcGravityCenter();

    void setParam(f32 minDist, f32 maxDist, f32 distBase) {
        mDistMin = minDist;
        mDistMax = maxDist;
        mDistBase = distBase;
    }

    f32 mDistMax;
    f32 mDistMin;
    f32 mDistBase;
    TVec3f mVelocity;
    TVec3f mPos;
    TVec3f mGravityCenter;
};
