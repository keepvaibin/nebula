#pragma once

#include "Game/Camera/Camera.hpp"

class RailRider;

class CameraRailWatch : public Camera {
public:
    CameraRailWatch(const char* pName = "レール注目カメラ");

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual void setParamFromChunk(const CameraParamChunk*);

    void setParam(s32, s32, s32, f32, f32, f32);

    RailRider* mRailRider;
    s32 mDirection;
    s32 mSetDirection;
    f32 mRailCoordOffset;
    f32 mDist;
    f32 mAngleX;
};
