#pragma once

#include "Game/Camera/Camera.hpp"

class CameraCharmedFix : public Camera {
public:
    CameraCharmedFix(const char* pName = "サンボカメラ");

    virtual void reset() override;
    virtual CameraTargetObj* calc() override;
    virtual void setParamFromChunk(const CameraParamChunk*) override;

    void setParam(const Vec3f& rBasePos, const Vec3f& rUp, const Vec3f& rWPoint) {
        mBasePos.set(rBasePos);
        mUp.set(rUp);
        mWPoint.set(rWPoint);
    }

    Vec3f mBasePos;
    Vec3f mUp;
    Vec3f mWPoint;
};
