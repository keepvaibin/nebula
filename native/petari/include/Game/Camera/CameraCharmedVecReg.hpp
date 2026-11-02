#pragma once

#include "Game/Camera/Camera.hpp"

class CameraCharmedVecReg : public Camera {
public:
    CameraCharmedVecReg(const char* pName = "\203\170\203\116\203\147\203\213\203\214\203\127\203\130\203\136\222\215\226\332\203\112\203\201\203\211" /* ベクトルレジスタ注目カメラ */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

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
