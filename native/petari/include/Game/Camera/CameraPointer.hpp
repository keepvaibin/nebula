#pragma once

#include "Game/Camera/Camera.hpp"
#include <nebula_geometry.hpp>

// A camera turned by the pointer (CAM_TYPE_DPD in camera files).
class CameraPointer : public Camera {
public:
    enum CameraType {
        CameraType_FixedPos = 0,
        CameraType_UpdateWithTarget = 1,
        CameraType_FollowWorldPos = 2,
    };
    CameraPointer(const char* pName = "DPD操作カメラ");

    virtual void reset() override;
    virtual CameraTargetObj* calc() override;
    virtual void setParamFromChunk(const CameraParamChunk*) override;

    void setParam(s32 cameraType, f32 dist, f32 angleXRange, f32 angleYRange, f32 angleX, f32 speedBlendRate, f32 blendFriction, f32 angleYMin) {
        mCameraType = cameraType;
        mDist = dist;
        mAngleXRange = angleXRange;
        mAngleYRange = angleYRange;
        mAngleX = angleX;
        mSpeedBlendRate = speedBlendRate;
        mBlendFriction = blendFriction;
        mAngleYMin = angleYMin;
    }

    f32 mPointerDistance;
    Vec2f mRealPointPos;
    Vec2f mBlendPointPos;
    Pos3f mMtx;
    bool mResetPointingPos;
    s32 mCameraType;
    f32 mDist;
    f32 mFovy;
    f32 mAngleXRange;
    f32 mAngleYRange;
    f32 mAngleX;
    f32 mSpeedBlendRate;
    f32 mBlendFriction;
    f32 mAngleYMin;
};
