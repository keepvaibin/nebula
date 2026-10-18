#pragma once

#include "Game/Camera/Camera.hpp"

class CameraFixedPoint : public Camera {
public:
    enum CameraType {
        /* 0x0 */ CameraType_ZoneUp,
        /* 0x1 */ CameraType_TurnUp,
        /* 0x2 */ CameraType_PlayerUp,
    };

    CameraFixedPoint(const char* pName = "\222\350\223\137\203\112\203\201\203\211" /* 定点カメラ */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void setParam(const TVec3f&, u32);

    /* 0x4C */ TVec3f mPos;
    /* 0x58 */ u32 mCameraType;
};
