#pragma once

#include "Game/Camera/Camera.hpp"

class CameraSubjective : public Camera {
public:
    CameraSubjective(const char* pName = "主観カメラ");

    virtual void reset() override;
    virtual CameraTargetObj* calc() override;
    virtual bool isCollisionOff() const override {
        return true;
    }

    Vec2f mAngleVel;
    Vec2f mAngle;
    Pos3f mMtx;
};
