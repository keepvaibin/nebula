#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

struct SkeletalFishRailInfo {
    LiveActor* mRail;
    f32 mCoord;
};

class SkeletalFishRailControl {
public:
    SkeletalFishRailControl();

    void setRailActor(LiveActor*, LiveActor*, bool);
    void update();
    void getRailInfo(SkeletalFishRailInfo*, f32);
    void getPos(Vec3f*, f32);
    void getMtx(Pos3f*, f32);

    u32 mState;
    LiveActor* mJoinRail;
    LiveActor* mRail;
    f32 mRailCoord;
    f32 mJoinCoord;
    f32 mSpeed;
};