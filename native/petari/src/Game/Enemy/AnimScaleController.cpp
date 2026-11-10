#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_AnimScaleDefaultParam_f1, "AnimScaleDefaultParam.f1", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_AnimScaleDefaultParam_f2, "AnimScaleDefaultParam.f2", 0.85f);
NEBULA_TUNABLE_NAMED(f32, cNum_AnimScaleDefaultParam_f3, "AnimScaleDefaultParam.f3", 8.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_AnimScaleDefaultParam_f4, "AnimScaleDefaultParam.f4", 0.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_AnimScaleDefaultParam_f5, "AnimScaleDefaultParam.f5", 0.08f);
NEBULA_TUNABLE_NAMED(f32, cNum_AnimScaleDefaultParam_f6, "AnimScaleDefaultParam.f6", 2.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_AnimScaleParam_f1, "AnimScaleParam.f1", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_AnimScaleParam_f2, "AnimScaleParam.f2", 0.91f);
NEBULA_TUNABLE_NAMED(f32, cNum_AnimScaleParam_f3, "AnimScaleParam.f3", 0.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_AnimScaleParam_f4, "AnimScaleParam.f4", 1.8f);
NEBULA_TUNABLE_NAMED(f32, cNum_AnimScaleParam_f5, "AnimScaleParam.f5", 12.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_AnimScaleParam_f6, "AnimScaleParam.f6", 0.55f);
NEBULA_TUNABLE_NAMED(f32, cNum_AnimScaleParam_f7, "AnimScaleParam.f7", 0.12f);
NEBULA_TUNABLE_NAMED(f32, cNum_AnimScaleParam_f8, "AnimScaleParam.f8", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_AnimScaleParam_f9, "AnimScaleParam.f9", 0.06f);
NEBULA_TUNABLE_NAMED(f32, cNum_AnimScaleParam_f10, "AnimScaleParam.f10", 0.12f);
NEBULA_TUNABLE_NAMED(f32, cNum_AnimScaleParam_f11, "AnimScaleParam.f11", 0.91f);
NEBULA_TUNABLE_NAMED(f32, cNum_AnimScaleParam_f12, "AnimScaleParam.f12", 0.25f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeCrush_f1, "exeCrush.f1", 0.3f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeCrush_f2, "exeCrush.f2", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryStop_f1, "tryStop.f1", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_tryStop_f2, "tryStop.f2", 0.001f);

namespace NrvAnimScaleController {
    NEW_NERVE(AnimScaleControllerNrvAnim, AnimScaleController, Anim);
    NEW_NERVE(AnimScaleControllerNrvStop, AnimScaleController, Stop);
    NEW_NERVE(AnimScaleControllerNrvPointerVibration, AnimScaleController, PointerVibration);
    NEW_NERVE(AnimScaleControllerNrvHitReaction, AnimScaleController, HitReaction);
    NEW_NERVE(AnimScaleControllerNrvCrush, AnimScaleController, Crush);
};  // namespace NrvAnimScaleController

namespace {

    class AnimScaleDefaultParam : public AnimScaleParam {
    public:
        AnimScaleDefaultParam();

        AnimScaleParam tightParam;
    };

    AnimScaleDefaultParam::AnimScaleDefaultParam() {
        tightParam.mAnimSpring = cNum_AnimScaleDefaultParam_f1;
        tightParam.mAnimDamping = cNum_AnimScaleDefaultParam_f2;
        tightParam.mPointerTime = cNum_AnimScaleDefaultParam_f3;
        tightParam.mPointerBase = cNum_AnimScaleDefaultParam_f4;
        tightParam.mPointerAmplitude = cNum_AnimScaleDefaultParam_f5;
        tightParam.mPointerFreq = cNum_AnimScaleDefaultParam_f6;
    };

    AnimScaleDefaultParam sStaticAnimParam;
};  // namespace

AnimScaleParam::AnimScaleParam() {
    mAnimSpring = cNum_AnimScaleParam_f1;
    mAnimDamping = cNum_AnimScaleParam_f2;
    mScaleMin = cNum_AnimScaleParam_f3;
    mScaleMax = cNum_AnimScaleParam_f4;
    mPointerTime = cNum_AnimScaleParam_f5;
    mPointerBase = cNum_AnimScaleParam_f6;
    mPointerAmplitude = cNum_AnimScaleParam_f7;
    mPointerFreq = cNum_AnimScaleParam_f8;
    mHitReactionVelocity = cNum_AnimScaleParam_f9;
    mHitReactionSpring = cNum_AnimScaleParam_f10;
    mHitReactionDamping = cNum_AnimScaleParam_f11;
    mCrushStep = 0x14;
    mCrushEnd = cNum_AnimScaleParam_f12;
}

AnimScaleController::AnimScaleController(AnimScaleParam* pScaleParam) : NerveExecutor("スケールアニメコントロール\000" /* スケールアニメコントロール\00 */) {
    mParam = pScaleParam;
    mScale.set(1.0f);
    mScaleVelocity = 0.0f;

    if (pScaleParam == nullptr) {
        ::sStaticAnimParam = AnimScaleDefaultParam();  // again, with a mod's numbers
        mParam = &::sStaticAnimParam;
    }
    initNerve(GET_NERVE(AnimScaleController, AnimScaleControllerNrvStop));
}

void AnimScaleController::setParamTight() {
    ::sStaticAnimParam = AnimScaleDefaultParam();  // again, with a mod's numbers
    mParam = &::sStaticAnimParam.tightParam;
}

void AnimScaleController::startAnim() {
    setNerve(GET_NERVE(AnimScaleController, AnimScaleControllerNrvAnim));
}

void AnimScaleController::startPointerHitVibration() {
    setNerve(GET_NERVE(AnimScaleController, AnimScaleControllerNrvPointerVibration));
}

void AnimScaleController::startHitReaction() {
    setNerve(GET_NERVE(AnimScaleController, AnimScaleControllerNrvHitReaction));
}

void AnimScaleController::startAndAddScaleVelocityY(f32 velocity) {
    mScaleVelocity += velocity;
    setNerve(GET_NERVE(AnimScaleController, AnimScaleControllerNrvAnim));
}

void AnimScaleController::startCrush() {
    setNerve(GET_NERVE(AnimScaleController, AnimScaleControllerNrvCrush));
}

void AnimScaleController::stopAndReset() {
    resetScale();
    setNerve(GET_NERVE(AnimScaleController, AnimScaleControllerNrvStop));
}

void AnimScaleController::resetScale() {
    mScaleVelocity = 0.0f;
    mScale.set(1.0f);
}

void AnimScaleController::exeAnim() {
    updateScale(mParam->mAnimSpring, mParam->mAnimDamping);
    if (tryStop()) {
        return;
    }
}

void AnimScaleController::exeStop() {
}

void AnimScaleController::exeHitReaction() {
    if (MR::isFirstStep(this)) {
        resetScale();
        mScaleVelocity = mParam->mHitReactionVelocity;
    }

    updateScale(mParam->mHitReactionSpring, mParam->mHitReactionDamping);
    if (tryStop()) {
        return;
    }
}

void AnimScaleController::exePointerVibration() {
    f32 nerveStep = getNerveStep();
    f32 dpdVibrationValue = MR::getReduceVibrationValue(nerveStep, mParam->mPointerTime, mParam->mPointerBase, mParam->mPointerAmplitude, mParam->mPointerFreq);

    mScale.y = dpdVibrationValue;
    mScale.x = MR::sqrt(1.0f / mScale.y);
    mScale.z = mScale.x;
}

void AnimScaleController::exeCrush() {
    mScaleVelocity = 0.0f;

    f32 nerveRate = MR::calcNerveRate(this, mParam->mCrushStep);
    f32 crushValue = MR::getConvergeVibrationValue(nerveRate, 1.0f, mParam->mCrushEnd, cNum_exeCrush_f1, cNum_exeCrush_f2);

    mScale.y = crushValue;
    mScale.x = MR::sqrt(1.0f / mScale.y);
    mScale.z = mScale.x;

    if (MR::isGreaterStep(this, mParam->mCrushStep)) {
        setNerve(GET_NERVE(AnimScaleController, AnimScaleControllerNrvStop));
    }
}

bool AnimScaleController::isHitReaction(s32 maxStep) const {
    if (!isNerve(GET_NERVE(AnimScaleController, AnimScaleControllerNrvHitReaction))) {
        return false;
    }

    if (maxStep > 0) {
        return MR::isLessStep(this, maxStep);
    }
    return true;
}

bool AnimScaleController::tryStop() {
    if (MR::abs(1.0f - mScale.y) < cNum_tryStop_f1 && MR::abs(mScaleVelocity) < cNum_tryStop_f2) {
        resetScale();
        setNerve(GET_NERVE(AnimScaleController, AnimScaleControllerNrvStop));
        return true;
    }
    return false;
}

void AnimScaleController::update() {
    updateNerve();
}

void AnimScaleController::updateScale(f32 spring, f32 damping) {
    f32 velocity = ((1.0f - mScale.y) * spring + mScaleVelocity);
    velocity = velocity * damping;
    mScaleVelocity = velocity;
    mScale.y += velocity;

    f32 clampedY;

    f32 clampMin = mParam->mScaleMin;
    f32 clampMax = mParam->mScaleMax;
    if (mScale.y < clampMin) {
        clampedY = clampMin;
    } else if (mScale.y > clampMax) {
        clampedY = clampMax;
    } else {
        clampedY = mScale.y;
    }

    // Volume kept: x and z take 1 / sqrt(y).
    mScale.y = clampedY;
    mScale.x = MR::sqrt(1.0f / mScale.y);
    mScale.z = mScale.x;
}
