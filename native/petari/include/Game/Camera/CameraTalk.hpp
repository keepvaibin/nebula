#pragma once

#include "Game/Camera/Camera.hpp"

class CameraTalk : public Camera {
public:
    CameraTalk(const char* pName = "会話カメラ");

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void setParam(const Vec3f&, const Vec3f&, f32, f32);

    Vec3f mPos;
    Vec3f mUp;
    f32 mHeight;
    f32 mDist;
    bool mAxisReversed;
};
