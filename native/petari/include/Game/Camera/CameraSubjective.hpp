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

    Vec2f mAngleVel;
    Vec2f mAngle;
    Pos3f mMtx;
};
