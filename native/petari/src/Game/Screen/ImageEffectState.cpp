#include "Game/Screen/ImageEffectState.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Screen/BloomEffect.hpp"
#include "Game/Screen/BloomEffectSimple.hpp"
#include "Game/Screen/DepthOfFieldBlur.hpp"
#include "Game/Screen/ImageEffectBase.hpp"
#include "Game/Screen/ImageEffectDirector.hpp"
#include "Game/Screen/ScreenBlurEffect.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_update_f1, "update.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f2, "update.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f3, "update.f3", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f4, "update.f4", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f5, "update.f5", 0.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f6, "update.f6", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f7, "update.f7", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f8, "update.f8", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f9, "update.f9", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f10, "update.f10", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f11, "update.f11", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f12, "update.f12", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f13, "update.f13", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f14, "update.f14", 255.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f15, "update.f15", 255.0f);

void ImageEffectState::update() {
    ImageEffectBase* pCurrent = mHost->mCurrentEffect;
    bool isCurrent = pCurrent == getEffect();

    if (isCurrent) {
        if (getEffect() == nullptr) {
            return;
        }

        if (getEffect()->mIsOn) {
            return;
        }

        ImageEffectBase* pBase = getEffect();
        pBase->mIsOn = true;
        pBase->notifyTurnOn();
        return;
    }

    ImageEffectBase* pBase = mHost->mCurrentEffect;

    if (pBase != nullptr) {
        if (pBase->mIsOn) {
            pBase->mIsOn = false;
            pBase->notifyTurnOff();
            return;
        }

        if (pBase->isSomething()) {
            return;
        }
    }

    if (getEffect() != nullptr) {
        ImageEffectBase* pBase = getEffect();
        pBase->mIsOn = true;
        pBase->notifyTurnOn();
    }

    ImageEffectDirector* pHost = mHost;
    pHost->setCurrentEffect(getEffect());
}

void ImageEffectState::forceOff() {
    ImageEffectBase* pBase;

    if (getEffect() == nullptr) {
        return;
    }

    pBase = getEffect();
    pBase->mIsShown = false;
    pBase->mIsOn = false;
    pBase->mFadeLevel = 0.0f;
    pBase->notifyForceOff();
}

namespace ImageEffectStateImpl {
    StateBloomNormal::StateBloomNormal(ImageEffectDirector* pHost)
        : ImageEffectState(pHost), mIsSnap(true), mBloomIntensityNow(), mBloomIntensity(), mThresholdNow(), mThreshold(), mIntensity1Now(), mIntensity1(), mIntensity2Now(), mIntensity2() {
    }

    void StateBloomNormal::update() {
        if (mIsSnap) {
            mBloomIntensityNow = mBloomIntensity;
            mThresholdNow = mThreshold;
            mIntensity1Now = mIntensity1;
            mIntensity2Now = mIntensity2;
            mIsSnap = false;
        } else {
            f32 rate = cNum_update_f1;
            mBloomIntensityNow += ((mBloomIntensity + cNum_update_f2) - mBloomIntensityNow) * rate;
            mThresholdNow += ((mThreshold + cNum_update_f3) - mThresholdNow) * rate;
            mIntensity1Now += ((mIntensity1 + cNum_update_f4) - mIntensity1Now) * rate;
            mIntensity2Now += ((mIntensity2 + cNum_update_f5) - mIntensity2Now) * rate;

            if (mBloomIntensityNow > cNum_update_f6) {
                mBloomIntensityNow = cNum_update_f7;
            }

            if (mThresholdNow > cNum_update_f8) {
                mThresholdNow = cNum_update_f9;
            }

            if (mIntensity1Now > cNum_update_f10) {
                mIntensity1Now = cNum_update_f11;
            }

            if (mIntensity2Now > cNum_update_f12) {
                mIntensity2Now = cNum_update_f13;
            }
        }

        u8 bloomIntensity = (s32)(mBloomIntensityNow);
        static_cast< BloomEffect* >(StateBloomNormal::getEffect())->mBloomIntensity = bloomIntensity;

        u8 threshold = (s32)(mThresholdNow);
        static_cast< BloomEffect* >(StateBloomNormal::getEffect())->mThreshold = threshold;

        u8 intensity1 = (s32)(mIntensity1Now);
        static_cast< BloomEffect* >(StateBloomNormal::getEffect())->mIntensity1 = intensity1 / cNum_update_f14;

        u8 intensity2 = (s32)(mIntensity2Now);
        static_cast< BloomEffect* >(StateBloomNormal::getEffect())->mIntensity2 = intensity2 / cNum_update_f15;

        ImageEffectState::update();
    }

    bool StateBloomNormal::doesEffectExist() const {
        return MR::isExistSceneObj(SceneObj_BloomEffect);
    }

    ImageEffectBase* StateBloomNormal::getEffect() const {
        return MR::getSceneObj< ImageEffectBase >(SceneObj_BloomEffect);
    }

    void StateBloomNormal::onChange() {
        mIsSnap = true;
    }

    void StateBloomNormal::setBloomIntensity(u8 bloomIntensity) {
        mBloomIntensity = bloomIntensity;
    }

    void StateBloomNormal::setThreshold(u8 threshold) {
        mThreshold = threshold;
    }

    void StateBloomNormal::setIntensity1(u8 intensity1) {
        mIntensity1 = intensity1;
    }

    void StateBloomNormal::setIntensity1Default() {
        mIntensity1 = static_cast< BloomEffect* >(StateBloomNormal::getEffect())->getIntensity1Default();
    }

    void StateBloomNormal::setIntensity2(u8 intensity2) {
        mIntensity2 = intensity2;
    }

    void StateBloomNormal::setIntensity2Default() {
        mIntensity2 = static_cast< BloomEffect* >(StateBloomNormal::getEffect())->getIntensity2Default();
    }

    bool StateBloomSimple::doesEffectExist() const {
        return MR::isExistSceneObj(SceneObj_BloomEffectSimple);
    }

    ImageEffectBase* StateBloomSimple::getEffect() const {
        return MR::getSceneObj< ImageEffectBase >(SceneObj_BloomEffectSimple);
    }

    void StateBloomSimple::setMaskFilterAll() {
        static_cast< BloomEffectSimple* >(StateBloomSimple::getEffect())->mMaskFilter = 0;
    }

    void StateBloomSimple::setMaskFilterRed() {
        static_cast< BloomEffectSimple* >(StateBloomSimple::getEffect())->mMaskFilter = 1;
    }

    void StateBloomSimple::setMaskFilterGreen() {
        static_cast< BloomEffectSimple* >(StateBloomSimple::getEffect())->mMaskFilter = 2;
    }

    void StateBloomSimple::setMaskFilterBlue() {
        static_cast< BloomEffectSimple* >(StateBloomSimple::getEffect())->mMaskFilter = 3;
    }

    void StateBloomSimple::setThreshold(u8 threshold) {
        static_cast< BloomEffectSimple* >(StateBloomSimple::getEffect())->mThreshold = threshold;
    }

    void StateBloomSimple::setIntensity(f32 intensity) {
        static_cast< BloomEffectSimple* >(StateBloomSimple::getEffect())->mIntensity = intensity;
    }

    bool StateScreenBlur::doesEffectExist() const {
        return MR::isExistSceneObj(SceneObj_ScreenBlurEffect);
    }

    ImageEffectBase* StateScreenBlur::getEffect() const {
        return MR::getSceneObj< ImageEffectBase >(SceneObj_ScreenBlurEffect);
    }

    void StateScreenBlur::setIntensity(f32 intensity) {
        static_cast< ScreenBlurEffect* >(StateScreenBlur::getEffect())->mIntensity = intensity;
    }

    bool StateDepthOfField::doesEffectExist() const {
        return MR::isExistSceneObj(SceneObj_DepthOfFieldBlur);
    }

    ImageEffectBase* StateDepthOfField::getEffect() const {
        return MR::getSceneObj< ImageEffectBase >(SceneObj_DepthOfFieldBlur);
    }

    void StateDepthOfField::setIntensity(f32 intensity) {
        static_cast< DepthOfFieldBlur* >(StateDepthOfField::getEffect())->mIntensity = intensity;
    }

    void StateDepthOfField::setBlurMaxDist(s32 blurMaxDist) {
        static_cast< DepthOfFieldBlur* >(StateDepthOfField::getEffect())->mBlurMaxDist = blurMaxDist;
    }

    void StateDepthOfField::setBlurMinDist(s32 blurMinDist) {
        static_cast< DepthOfFieldBlur* >(StateDepthOfField::getEffect())->mBlurMinDist = blurMinDist;
    }
};  // namespace ImageEffectStateImpl
