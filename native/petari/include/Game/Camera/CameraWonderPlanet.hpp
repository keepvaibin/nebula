#pragma once

#include "Game/Camera/Camera.hpp"

class CameraWonderPlanet : public Camera {
public:
    CameraWonderPlanet(const char* pName = "ワンダープラネットカメラ");

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual bool isEnableToReset() const {
        return true;
    }
    virtual void setParamFromChunk(const CameraParamChunk*);

    void updateCameraReset();
    void rotateCamera();
    void checkReset();

    void setParam(f32 distMin, f32 distMax, f32 angleX) {
        mDistMin = distMin;
        mDistMax = distMax;
        mAngleX = angleX;
    }

    f32 mDistMin;
    f32 mDistMax;
    f32 mAngleX;
    bool mIsRotating;
    s32 mRotateFrame;
};
