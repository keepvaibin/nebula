#pragma once

#include "Game/Camera/Camera.hpp"

class CameraWaterPlanet : public Camera {
public:
    CameraWaterPlanet(const char* pName = "\220\205\222\206\203\166\203\211\203\154\203\142\203\147\203\112\203\201\203\211" /* 水中プラネットカメラ */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void setParam(f32 distMin, f32 distMax, f32 angleX) {
        mDistMin = distMin;
        mDistMax = distMax;
        mAngleX = angleX;
    }

    f32 mDistMin;
    f32 mDistMax;
    f32 mAngleX;
};
