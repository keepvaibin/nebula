#pragma once

#include "Game/Camera/Camera.hpp"

class CameraDead : public Camera {
public:
    enum CameraType {
        CameraType_FixedPos = 0,
    };

    CameraDead(const char* pName = "通常死亡カメラ");

    virtual void reset() override;
    virtual CameraTargetObj* calc() override;
    virtual bool isInterpolationOff() const override {
        return true;
    }
    virtual void setParamFromChunk(const CameraParamChunk*) override;

    void setParam(f32 dist, s32 deadTime, s32 cameraType) {
        mDist = dist;
        mDeadTime = deadTime;
        mCameraType = cameraType;
    }

    f32 mFovy;
    f32 mDist;
    s32 mDeadTime;
    s32 mCameraType;
    s32 mDeadFrame;
};
