#pragma once

#include "Game/Camera/Camera.hpp"

class CameraCharmedVecReg : public Camera {
public:
    CameraCharmedVecReg(const char* pName = "ベクトルレジスタ注目カメラ");

    virtual void reset() override;
    virtual CameraTargetObj* calc() override;
    virtual void setParamFromChunk(const CameraParamChunk*) override;

    void setParam(const char* pName, f32 dist, f32 angleX, f32 angleYMax, f32 angleYMin, f32 distRatio, f32 angleRate) {
        mName = pName;
        mDist = dist;
        if (mDist < 300.0f) {
            mDist = 300.0f;
        }
        mAngleX = angleX;
        mAngleYMin = angleYMin;
        mAngleYMax = angleYMax;
        mCamDistRatio = distRatio;
        mCamAngleRate = angleRate;
    }

    const char* mName;
    f32 mDist;
    f32 mAngleX;
    f32 mAngleYMin;
    f32 mAngleYMax;
    f32 mCamDistRatio;
    f32 mCamAngleRate;
};
