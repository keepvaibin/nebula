#include "Game/MapObj/WaveFloatingForce.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_updateOnPlayer_f1, "updateOnPlayer.f1", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnPlayer_f2, "updateOnPlayer.f2", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnPlayer_f3, "updateOnPlayer.f3", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnPlayer_f4, "updateOnPlayer.f4", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnPlayer_f5, "updateOnPlayer.f5", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnPlayer_f6, "updateOnPlayer.f6", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnPlayer_f7, "updateOnPlayer.f7", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnPlayer_f8, "updateOnPlayer.f8", 3.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateOnPlayer_f9, "updateOnPlayer.f9", 2.0f);

WaveFloatingForce::WaveFloatingForce(const LiveActor* pActor, s32 stepCount, f32 amplitude) : mActor(pActor), mStepCount(stepCount), mAmplitude(amplitude), mStep() {
    mStep = 2 * MR::pi() * MR::getRandom();
}

void WaveFloatingForce::update() {
    if (MR::isOnPlayer(mActor)) {
        updateOnPlayer();
    } else {
        updateNormal();
    }
}

f32 WaveFloatingForce::getCurrentValue() const {
    return mAmplitude * MR::sin(mStep);
}

void WaveFloatingForce::updateNormal() {
    mStep += 2 * MR::pi() / mStepCount;
    mStep = MR::repeat(mStep, 0.0f, 2 * MR::pi());
}

void WaveFloatingForce::updateOnPlayer() {
    f32 stepSize = 2 * MR::pi() / mStepCount;
    if (MR::pi() / cNum_updateOnPlayer_f1 < mStep && mStep < cNum_updateOnPlayer_f2 / cNum_updateOnPlayer_f3 * MR::pi()) {
        mStep += stepSize;
    } else {
        mStep -= stepSize;
    }

    mStep = MR::repeat(mStep, 0.0f, cNum_updateOnPlayer_f4 * MR::pi());

    if (MR::isNearZero(MR::abs(mStep) - cNum_updateOnPlayer_f5 / cNum_updateOnPlayer_f6 * MR::pi(), cNum_updateOnPlayer_f7 * stepSize)) {
        mStep = cNum_updateOnPlayer_f8 / cNum_updateOnPlayer_f9 * MR::pi();
    }
}
