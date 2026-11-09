#pragma once

#include "Game/Camera/Camera.hpp"

class CameraInwardSphere : public Camera {
public:
    CameraInwardSphere(const char* pName = "球内部カメラ");

    virtual void reset() override;
    virtual CameraTargetObj* calc() override;
    virtual void setParamFromChunk(const CameraParamChunk*) override;

    void calcGravityCenter();

    void setParam(f32 minDist, f32 maxDist, f32 distBase) {
        mDistMin = minDist;
        mDistMax = maxDist;
        mDistBase = distBase;
    }

    f32 mDistMax;
    f32 mDistMin;
    f32 mDistBase;
    Vec3f mVelocity;
    Vec3f mPos;
    Vec3f mGravityCenter;
};
