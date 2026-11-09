#pragma once

#include "Game/Camera/Camera.hpp"

class CameraWaterPlanet : public Camera {
public:
    CameraWaterPlanet(const char* pName = "水中プラネットカメラ");

    virtual void reset() override;
    virtual CameraTargetObj* calc() override;
    virtual void setParamFromChunk(const CameraParamChunk*) override;

    void setParam(f32 distMin, f32 distMax, f32 angleX) {
        mDistMin = distMin;
        mDistMax = distMax;
        mAngleX = angleX;
    }

    f32 mDistMin;
    f32 mDistMax;
    f32 mAngleX;
};
