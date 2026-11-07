#pragma once

#include "Game/Camera/Camera.hpp"

class RailRider;

class CameraRailDemo : public Camera {
public:
    CameraRailDemo(const char* pName = "レールデモカメラ");

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void setParam(s32, s32, s32, s32, f32);
    void calcLinear();
    void calcEaseInOut();
    void calcDamp();

    RailRider* mRailRider;
    s32 mCalcType;
    s32 mDemoTime;
    s32 mDemoTimer;
    f32 mDampRatio;
    f32 mCoord;
};
