#include "Game/Sound/SoundControls.hpp"

namespace {
    // Over maxSteps calc() calls; at once when maxSteps is 0.
    void moveTo(f32* pValue, SoundTransition::Ramp* pRamp, f32 newValue, u32 maxSteps) {
        if (maxSteps == 0) {
            *pValue = newValue;
            pRamp->mRemainingSteps = 0;
        } else {
            pRamp->set(newValue, *pValue, maxSteps);
        }
    }
};  // namespace

void SoundMove::moveVolume(f32 newValue, u32 maxSteps) {
    ::moveTo(&mParams.mVolume, &mTransition.mVolume, newValue, maxSteps);
}

void SoundMove::movePitch(f32 newValue, u32 maxSteps) {
    ::moveTo(&mParams.mPitch, &mTransition.mPitch, newValue, maxSteps);
}

void SoundMove::moveFxMix(f32 newValue, u32 maxSteps) {
    ::moveTo(&mParams.mFxMix, &mTransition.mFxMix, newValue, maxSteps);
}
