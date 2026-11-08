#pragma once

#include "Game/Camera/Camera.hpp"

class CameraSubjective : public Camera {
public:
    CameraSubjective(const char* pName = "主観カメラ");

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual bool isCollisionOff() const {
        return true;
    }

    Vec2f mAngleVel;
    Vec2f mAngle;
    Pos3f mMtx;
};
