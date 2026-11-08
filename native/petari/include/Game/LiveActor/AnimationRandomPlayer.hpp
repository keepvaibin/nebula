#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/System/NerveExecutor.hpp"

class AnimationRandomPlayer : public NerveExecutor {
public:
    AnimationRandomPlayer(const LiveActor*, const char*, const char*, s32, f32);

    void updateStartStep();
    void exeWait();
    void exePlay();

    const LiveActor* mActor;
    const char* mPlayAnim;
    const char* mWaitAnim;
    s32 mWaitSteps;
    s32 mMeanWait;
    f32 mWaitSpread;
};
