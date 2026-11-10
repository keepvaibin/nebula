#include "Game/Enemy/AnimStampController.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/System/NerveExecutor.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_AnimStampController_f1, "AnimStampController.f1", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_AnimStampController_f2, "AnimStampController.f2", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_AnimStampController_f3, "AnimStampController.f3", 0.125f);
NEBULA_TUNABLE_NAMED(f32, cNum_AnimStampController_f4, "AnimStampController.f4", 4.0f);
NEBULA_TUNABLE_NAMED(f64, cNum_AnimStampController_f5, "AnimStampController.f5", 0.12);
NEBULA_TUNABLE_NAMED(f32, cNum_AnimStampController_f6, "AnimStampController.f6", 4.0f);

namespace NrvAnimStampController {
    NEW_NERVE(HostNrvAnim, AnimStampController, Anim);
    NEW_NERVE(HostNrvAnimEnd, AnimStampController, AnimEnd);
};  // namespace NrvAnimStampController

AnimStampController::AnimStampController() : NerveExecutor("つぶれアニメコントローラ"), mDir(0, 0, 1) {
    init(8, cNum_AnimStampController_f1, cNum_AnimStampController_f2, cNum_AnimStampController_f3, cNum_AnimStampController_f4, cNum_AnimStampController_f5, cNum_AnimStampController_f6);
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
