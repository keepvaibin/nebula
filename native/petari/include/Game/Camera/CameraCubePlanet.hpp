#pragma once

#include "Game/Camera/Camera.hpp"

class CameraCubePlanet : public Camera {
public:
    CameraCubePlanet(const char* pName = "\203\114\203\205\201\133\203\165\230\146\220\257\203\112\203\201\203\211" /* キューブ惑星カメラ */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void setParam(f32 dist, f32 angleX, f32 angleY) {
        mDist = dist;
        mAngleX = angleX;
        mAngleY = angleY;
    }

    f32 mDist;
    f32 mAngleX;
    f32 mAngleY;
    TVec3f mUp;
};
