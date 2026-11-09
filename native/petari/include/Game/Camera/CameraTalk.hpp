#pragma once

#include "Game/Camera/Camera.hpp"

class CameraTalk : public Camera {
public:
    CameraTalk(const char* pName = "会話カメラ");

    virtual void reset() override;
    virtual CameraTargetObj* calc() override;
    virtual void setParamFromChunk(const CameraParamChunk*) override;

    void setParam(const Vec3f& pos, const Vec3f& up, f32 height, f32 dist);

    Vec3f mPos;
    Vec3f mUp;
    f32 mHeight;
    f32 mDist;
};
