#pragma once

#include "Game/Camera/Camera.hpp"
#include "Game/LiveActor/RailRider.hpp"

class CameraTwistedPassage : public Camera {
public:
    CameraTwistedPassage(const char* pName = "ねじれ回廊カメラ");

    virtual void reset() override;
    virtual CameraTargetObj* calc() override;
    virtual void setParamFromChunk(const CameraParamChunk*) override;

    void setParam(s32, s32, f32, f32);
    void initStartPos();
    f32 calcNearestCoord(const Vec3f&) const;
    f32 calcNearestCoordBetweenTwoRailCoord(f32, f32, const Vec3f&) const;

    RailRider* mRailRider;
    f32 mDistMin;
    f32 mDistMax;
    bool mIsReversed;
};
