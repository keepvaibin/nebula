#pragma once

#include "Game/Camera/Camera.hpp"

class CameraMtxRegParallel : public Camera {
public:
    CameraMtxRegParallel(const char* pName = "マトリクスレジスタ並行カメラ");

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual void setParamFromChunk(const CameraParamChunk*);

    void setParam(const char* str, f32 dist, const Vec2f& angle, const Vec3f& wPoint) {
        mString = str;
        mAngleX = angle.x;
        mAngleY = angle.y;
        mDist = dist;
        mWPoint = wPoint;
    }

    void calcIdealPose();

    const char* mString;
    f32 mAngleX;
    f32 mAngleY;
    f32 mDist;
    Vec3f mWPoint;
};
