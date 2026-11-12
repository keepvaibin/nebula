#include "Game/Enemy/AnimStampController.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/System/NerveExecutor.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include <nebula_geometry.hpp>

namespace NrvAnimStampController {
    NEW_NERVE(HostNrvAnim, AnimStampController, Anim);
    NEW_NERVE(HostNrvAnimEnd, AnimStampController, AnimEnd);
};  // namespace NrvAnimStampController

AnimStampController::AnimStampController() : NerveExecutor("つぶれアニメコントローラ"), mDir(0, 0, 1) {
    init(8, 0.2f, 1.5f, 0.125f, 4.0f, 0.12, 4.0f);
}

void AnimStampController::init(s32 animStep, f32 baseY, f32 baseXZ, f32 amplitudeY, f32 freqY, f32 amplitudeXZ, f32 freqXZ) {
    mAnimStep = animStep;
    mBaseY = baseY;
    mBaseXZ = baseXZ;
    mAmplitudeY = amplitudeY;
    mFreqY = freqY;
    mAmplitudeXZ = amplitudeXZ;
    mFreqXZ = freqXZ;
    mMtx.identity();
    initNerve(GET_NERVE(AnimStampController, HostNrvAnimEnd));
}

void AnimStampController::startAnim(const Vec3f& rVec) {
    mDir.set(rVec);
    setNerve(GET_NERVE(AnimStampController, HostNrvAnim));
    mMtx.identity();
}

void AnimStampController::exeAnim() {
    Vec3f vib = Vec3f(mBaseXZ, mBaseY, mBaseXZ);
    if (MR::isLessStep(this, mAnimStep)) {
        vib.x = MR::getReduceVibrationValue(getNerveStep(), mAnimStep, mBaseXZ, mAmplitudeXZ, mFreqXZ);
        vib.y = MR::getReduceVibrationValue(getNerveStep(), mAnimStep, mBaseY, mAmplitudeY, mFreqY);
        vib.z = MR::getReduceVibrationValue(getNerveStep(), mAnimStep, mBaseXZ, mAmplitudeXZ, mFreqXZ);
    } else {
        setNerve(GET_NERVE(AnimStampController, HostNrvAnimEnd));
    }

    MR::scaleMtxToDir(&mMtx, mDir, vib);
}

void AnimStampController::exeAnimEnd() {
}

bool AnimStampController::isAnimEnd() const {
    return isNerve(GET_NERVE(AnimStampController, HostNrvAnimEnd));
}
