#pragma once

#include "Game/Camera/Camera.hpp"

class RailRider;

class CameraRailWatch : public Camera {
public:
    CameraRailWatch(const char* pName = "\203\214\201\133\203\213\222\215\226\332\203\112\203\201\203\211" /* レール注目カメラ */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void setParam(s32, s32, s32, f32, f32, f32);

    RailRider* mRailRider;
    s32 mDirection;
    s32 mSetDirection;
    f32 mRailCoordOffset;
    f32 mDist;
    f32 mAngleX;
};
