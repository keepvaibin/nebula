#pragma once

#include "Game/Camera/Camera.hpp"

class CameraFixedPoint : public Camera {
public:
    enum CameraType {
        CameraType_ZoneUp,
        CameraType_TurnUp,
        CameraType_PlayerUp,
    };

    CameraFixedPoint(const char* pName = "\222\350\223\137\203\112\203\201\203\211" /* 定点カメラ */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void setParam(const TVec3f&, u32);

    TVec3f mPos;
    u32 mCameraType;
};
