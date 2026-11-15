#include "Game/Enemy/AnimScaleController.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/NerveUtil.hpp"

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
        tightParam.mAnimSpring = 0.5f;
        tightParam.mAnimDamping = 0.85f;
        tightParam.mPointerTime = 8.0f;
        tightParam.mPointerBase = 0.8f;
        tightParam.mPointerAmplitude = 0.08f;
        tightParam.mPointerFreq = 2.2f;
    };

    AnimScaleDefaultParam sStaticAnimParam;
};  // namespace

AnimScaleParam::AnimScaleParam() {
    mAnimSpring = 0.2f;
    mAnimDamping = 0.91f;
    mScaleMin = 0.2f;
    mScaleMax = 1.8f;
    mPointerTime = 12.0f;
    mPointerBase = 0.55f;
    mPointerAmplitude = 0.12f;
    mPointerFreq = 2.0f;
    mHitReactionVelocity = 0.06f;
    mHitReactionSpring = 0.12f;
    mHitReactionDamping = 0.91f;
    mCrushStep = 0x14;
    mCrushEnd = 0.25f;
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
    f32 crushValue = MR::getConvergeVibrationValue(nerveRate, 1.0f, mParam->mCrushEnd, 0.3f, 4.0f);

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
    if (MR::abs(1.0f - mScale.y) < 0.001f && MR::abs(mScaleVelocity) < 0.001f) {
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
