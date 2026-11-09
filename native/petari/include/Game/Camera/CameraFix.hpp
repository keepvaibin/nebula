#pragma once

#include "Game/Camera/Camera.hpp"

class CameraTargetMtx;

class CameraFix : public Camera {
public:
    CameraFix(const char* pName = "完全固定カメラ");

    virtual void reset() override;
    virtual CameraTargetObj* calc() override;
    virtual void setParamFromChunk(const CameraParamChunk*) override;

    void setParam(const Vec3f&, const Vec3f&, const Vec3f&);

    Vec3f mWPoint;
    Vec3f mAxis;
    Vec3f mUp;
    CameraTargetMtx* mTarget;
};
