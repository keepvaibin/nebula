#pragma once

#include "Game/Camera/Camera.hpp"

class CameraFixedPoint : public Camera {
public:
    enum CameraType {
        CameraType_ZoneUp,
        CameraType_TurnUp,
        CameraType_PlayerUp,
    };

    CameraFixedPoint(const char* pName = "定点カメラ");

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual void setParamFromChunk(const CameraParamChunk*);

    void setParam(const Vec3f&, u32);

    Vec3f mPos;
    u32 mCameraType;
};
