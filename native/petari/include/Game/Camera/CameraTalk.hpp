#pragma once

#include "Game/Camera/Camera.hpp"

class CameraTalk : public Camera {
public:
    CameraTalk(const char* pName = "\211\357\230\142\203\112\203\201\203\211" /* 会話カメラ */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void setParam(const TVec3f&, const TVec3f&, f32, f32);

    /* 0x4C */ TVec3f mPos;
    /* 0x58 */ TVec3f mUp;
    /* 0x64 */ f32 mHeight;
    /* 0x68 */ f32 mDist;
    /* 0x6C */ bool mAxisReversed;
};
