#pragma once

#include "Game/Camera/Camera.hpp"

class CameraFooFighter : public Camera {
public:
    CameraFooFighter(const char* pName = "\203\164\201\133\203\164\203\100\203\103\203\136\201\133" /* フーファイター */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void setParam(f32 distMin, f32 distMax, f32 angleXRateMin) {
        mDistMin = distMin;
        mDistMax = distMax;
        mAngleXRateMin = angleXRateMin;
    }

    f32 mDistMin;
    f32 mDistMax;
    f32 mAngleXRateMin;
    f32 mAngleXRate;
    s32 mCollideCount;
    TVec3f mTargetLastMoveDir;
};
