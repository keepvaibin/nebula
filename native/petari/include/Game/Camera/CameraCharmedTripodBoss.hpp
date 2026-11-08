#pragma once

#include "Game/Camera/Camera.hpp"

class CameraCharmedTripodBoss : public Camera {
public:
    CameraCharmedTripodBoss(const char* pName = "三脚ボスジョイント注視カメラ");

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual void setParamFromChunk(const CameraParamChunk*);

    void setParam(s32, Vec3f, const Vec3f&, const Vec2f&);

    s32 mJointId;
    Vec3f mUp;
    Vec3f mWPoint;
    f32 mAngleX;
    f32 mAngleY;
};
