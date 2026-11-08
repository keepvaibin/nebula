#pragma once

#include "Game/Camera/Camera.hpp"

class RailRider;

class CameraRailDemo : public Camera {
public:
    CameraRailDemo(const char* pName = "レールデモカメラ");

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual void setParamFromChunk(const CameraParamChunk*);

    void setParam(s32 zoneID, s32 railID, s32 calcType, s32 demoTime, f32 dampRatio);
    void calcLinear();
    void calcEaseInOut();
    void calcDamp();
    f32 calcTimeRate() const;
    void placeOnRail(f32 coord);

    RailRider* mRailRider;
    s32 mCalcType;
    s32 mDemoTime;
    s32 mDemoTimer;
    f32 mDampRatio;
    f32 mCoord;
};
