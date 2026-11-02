#pragma once

#include "Game/Camera/Camera.hpp"

class RailRider;

class CameraRailDemo : public Camera {
public:
    CameraRailDemo(const char* pName = "\203\214\201\133\203\213\203\146\203\202\203\112\203\201\203\211" /* レールデモカメラ */);

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
