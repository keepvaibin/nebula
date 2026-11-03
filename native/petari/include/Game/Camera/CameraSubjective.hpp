#pragma once

#include "Game/Camera/Camera.hpp"

class CameraSubjective : public Camera {
public:
    CameraSubjective(const char* pName = "\216\345\212\317\203\112\203\201\203\211" /* 主観カメラ */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual bool isCollisionOff() const {
        return true;
    }
    virtual CamTranslatorBase* createTranslator();

    TVec2f mAngleVel;
    TVec2f mAngle;
    TPos3f mMtx;
};
