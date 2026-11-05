#pragma once

#include "Game/Camera/Camera.hpp"

class CameraTalk : public Camera {
public:
    CameraTalk(const char* pName = "\211\357\230\142\203\112\203\201\203\211" /* 会話カメラ */);

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
