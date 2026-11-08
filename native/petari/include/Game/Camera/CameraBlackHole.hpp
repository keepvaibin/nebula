#pragma once

#include "Game/Camera/Camera.hpp"

class CameraBlackHole : public Camera {
public:
    CameraBlackHole(const char* pName = "ブラックホールカメラ");

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual void setParamFromChunk(const CameraParamChunk*);

    void setParam(const Vec3f rWPoint, const Vec3f rBasePos) {
        mWPoint.set(rWPoint);
        mBasePos.set(rBasePos);
    }

    f32 mFovy;
    f32 mRoll;
    Vec3f mWPoint;
    Vec3f mBasePos;
};
