#include "Game/LiveActor/AnimationRandomPlayer.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/NerveUtil.hpp"

namespace NrvAnimationRandomPlayer {
    NEW_NERVE(HostTypeWait, AnimationRandomPlayer, Wait);
    NEW_NERVE(HostTypePlay, AnimationRandomPlayer, Play);
};  // namespace NrvAnimationRandomPlayer

AnimationRandomPlayer::AnimationRandomPlayer(const LiveActor* pActor, const char* pPlayAnim, const char* pWaitAnim, s32 meanWait, f32 waitSpread)
    : NerveExecutor(pPlayAnim) {
    mActor = pActor;
    mPlayAnim = pPlayAnim;
    mWaitAnim = pWaitAnim;
    mMeanWait = meanWait;
    mWaitSpread = waitSpread;
    initNerve(GET_NERVE(AnimationRandomPlayer, HostTypeWait));
    updateStartStep();
}

// The wait: meanWait, give or take waitSpread of it.
void AnimationRandomPlayer::updateStartStep() {
    s32 spread = mMeanWait * mWaitSpread;
    mWaitSteps = MR::getRandom(mMeanWait - spread, mMeanWait + spread);
}

void AnimationRandomPlayer::exeWait() {
    if (MR::isFirstStep(this)) {
        if (mWaitAnim) {
            MR::startAllAnim(mActor, mWaitAnim);
        }
    }

    if (MR::isStep(this, mWaitSteps)) {
        setNerve(GET_NERVE(AnimationRandomPlayer, HostTypePlay));
    }
}

void AnimationRandomPlayer::exePlay() {
    if (MR::isFirstStep(this)) {
        MR::startAllAnim(mActor, mPlayAnim);
    }

    if (MR::isAnyAnimOneTimeAndStopped(mActor, mPlayAnim)) {
        updateStartStep();
        MR::setAllAnimFrame(mActor, mPlayAnim, 0.0f);
        setNerve(GET_NERVE(AnimationRandomPlayer, HostTypeWait));
    }
}
