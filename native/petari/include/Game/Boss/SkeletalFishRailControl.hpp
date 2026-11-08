#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

struct SkeletalFishRailInfo {
    LiveActor* mRail;
    f32 mCoord;
};

class SkeletalFishRailControl {
public:
    enum State {
        State_OnJoinRail,
        State_Joining,
        State_OnRail
    };

    SkeletalFishRailControl();

    void setRailActor(LiveActor* pRail, LiveActor* pJoinRail, bool isResetRail);
    void update();
    void getRailInfo(SkeletalFishRailInfo*, f32 behind);
    void getPos(Vec3f*, f32 behind);
    void getMtx(Pos3f*, f32 behind);

    State mState;
    LiveActor* mJoinRail;
    LiveActor* mRail;
    f32 mRailCoord;
    f32 mJoinCoord;
    f32 mSpeed;
};