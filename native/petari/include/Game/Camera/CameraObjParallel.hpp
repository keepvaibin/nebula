#pragma once

#include "Game/Camera/Camera.hpp"

class CameraObjParallel : public Camera {
public:
    CameraObjParallel(const char* pName = "オブジェ並行カメラ");

    virtual CameraTargetObj* calc();
    virtual void setParamFromChunk(const CameraParamChunk*);

    void setParam(f32 dist, f32 angleX, f32 angleY) {
        mDist = dist;
        mAngleX = angleX;
        mAngleY = angleY;
    }

    f32 mAngleX;
    f32 mAngleY;
    f32 mDist;
};
