#pragma once

#include "Game/Camera/Camera.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class CameraDPD : public Camera {
public:
    enum CameraType {
        CameraType_FixedPos = 0,
        CameraType_UpdateWithTarget = 1,
        CameraType_FollowWorldPos = 2,
    };
    CameraDPD(const char* pName = "DPD\221\200\215\354\203\112\203\201\203\211" /* DPD操作カメラ */);

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual CamTranslatorBase* createTranslator();

    void setParam(s32 cameraType, f32 dist, f32 angleXRange, f32 angleYRange, f32 angleX, f32 speedBlendRate, f32 blendFriction, bool isVertical,
                  f32 angleYMin) {
        mCameraType = cameraType;
        mDist = dist;
        mAngleXRange = angleXRange;
        mAngleYRange = angleYRange;
        mAngleX = angleX;
        mSpeedBlendRate = speedBlendRate;
        mBlendFriction = blendFriction;
        mIsVertical = isVertical;
        mAngleYMin = angleYMin;
    }

    f32 mPointerDistance;
    TVec2f mRealPointPos;
    TVec2f mBlendPointPos;
    TPos3f mMtx;
    bool mResetPointingPos;
    s32 mCameraType;
    f32 mDist;
    f32 mFovy;
    f32 mAngleXRange;
    f32 mAngleYRange;
    f32 mAngleX;
    f32 mSpeedBlendRate;
    f32 mBlendFriction;
    bool mIsVertical;
    f32 mAngleYMin;
};
