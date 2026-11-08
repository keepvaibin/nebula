#pragma once

#include "Game/Camera/Camera.hpp"

class CameraFooFighterPlanet : public Camera {
public:
    CameraFooFighterPlanet(const char* pName = "フーファイタープラネットカメラ");

    virtual void reset();
    virtual CameraTargetObj* calc();
    virtual void setParamFromChunk(const CameraParamChunk*);

    void goRoundBehind(Vec3f&, Vec3f&, Vec3f&);

    void setParam(f32 distMin, f32 distMax, f32 pitchMax) {
        mDistMin = distMin;
        mDistMax = distMax;
        mPitchMax = pitchMax;
    }

    f32 mDistMin;
    f32 mDistMax;
    f32 mPitchMax;
};
