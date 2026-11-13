#include "Game/LiveActor/ActorLightCtrl.hpp"
#include "Game/Map/LightFunction.hpp"
#include "Game/NameObj/NameObjExecuteHolder.hpp"
#include "Game/System/DrawBuffer.hpp"
#include "Game/Util.hpp"
#include "Game/Util/GameLightSignal.hpp"

ActorLightCtrl::ActorLightCtrl(const LiveActor* pActor)
    : mActor(pActor), mLightType(-1), mDrawBuffer(), mHasFixedType(), mAreaLightInf(), mLightID(), mBlendFrom(), mInterpolate(-1), mBlendStep(-1) {
}

void ActorLightCtrl::init(int lightType, bool /* unused */) {
    if (lightType >= 0) {
        mLightType = lightType;
        mHasFixedType = true;
    }

    initActorLightInfo();
    tryFindNewAreaLight(false);
    mAreaLightInf = LightFunction::getAreaLightInfo(mLightID);
    mLightInfo = *getTargetActorLight(mAreaLightInf);
}

void ActorLightCtrl::update(bool direct) {
    if (!MR::isHiddenModel(mActor)) {
        tryFindNewAreaLight(direct);
        updateLightBlend();
    }
}

void ActorLightCtrl::loadLight() const {
    if (mAreaLightInf) {
        GameLightSignal::actorLightType(mLightType);  // c95: the set's kind (characters or the environment)
        if (mBlendFrom) {
            LightFunction::loadActorLightInfo(&mLightInfo);
        } else {
            LightFunction::loadActorLightInfo(getTargetActorLight(mAreaLightInf));
        }
    }
}

void ActorLightCtrl::reset() {
    mLightID.clear();

    if (LightFunction::tryFindNewAreaLightID(mActor->mPosition, &mLightID)) {
        resetCurrentLightInfo();
        mBlendFrom = nullptr;
        mLightInfo = *getTargetActorLight(mAreaLightInf);
    }

    mAreaLightInf = LightFunction::getAreaLightInfo(mLightID);

    if (!mHasFixedType) {
        mDrawBuffer->resetLightSort(this);
    }
}

// Packets share a light run (one load for all) only when both lights load the same thing: a blending light loads its
// own mix, so neither side may be blending (a packet joining a blending head took its mix and popped when the blend
// ended), and the area light's entry is picked by the type.
bool ActorLightCtrl::isSameLight(const ActorLightCtrl* pLight) const {
    if (mBlendFrom || pLight->mBlendFrom) {
        return false;
    }

    return mAreaLightInf == pLight->mAreaLightInf && mLightType == pLight->mLightType;
}

const ActorLightInfo* ActorLightCtrl::getActorLight() const {
    if (mBlendFrom) {
        return &mLightInfo;
    }

    return getTargetActorLight(mAreaLightInf);
}

void ActorLightCtrl::initActorLightInfo() {
    if (mHasFixedType) {
        return;
    }

    MR::findActorLightInfo(mActor);
    return;
}

void ActorLightCtrl::tryFindNewAreaLight(bool isImmediate) {
    if (LightFunction::tryFindNewAreaLightID(mActor->mPosition, &mLightID)) {
        if (mAreaLightInf != nullptr) {
            // From the light shown now: the old area's, or where a blend still running has got to.
            mBlendStart = *getActorLight();
            mBlendFrom = &mBlendStart;
            mLightInfo = mBlendStart;
        }

        resetCurrentLightInfo();

        if (!mInterpolate || isImmediate) {
            mInterpolate = 0;
            mBlendFrom = nullptr;
            mLightInfo = *getTargetActorLight(mAreaLightInf);
        }

        if (mInterpolate < 0) {
            mInterpolate = LightFunction::getDefaultStepInterpolate();
        }

        if (!mHasFixedType) {
            mDrawBuffer->resetLightSort(this);
        }
    }
}

void ActorLightCtrl::updateLightBlend() {
    if (mBlendFrom) {
        mBlendStep++;

        if (mBlendStep >= mInterpolate) {
            mLightInfo = *getTargetActorLight(mAreaLightInf);
            mBlendFrom = nullptr;
            mBlendStep = -1;

            if (!mHasFixedType) {
                mDrawBuffer->resetLightSort(this);
            }

        } else {
            f32 blend = (f32)mBlendStep / mInterpolate;
            const ActorLightInfo* info = getTargetActorLight(mAreaLightInf);
            LightFunction::blendActorLightInfo(&mLightInfo, *mBlendFrom, *info, blend);
        }
    }
}

void ActorLightCtrl::resetCurrentLightInfo() {
    mAreaLightInf = LightFunction::getAreaLightInfo(mLightID);
    mInterpolate = mAreaLightInf->mInterpolate;
    mLightInfo.mInfo0.mIsFollowCamera = getTargetActorLight(mAreaLightInf)->mInfo0.mIsFollowCamera;
    mLightInfo.mInfo1.mIsFollowCamera = getTargetActorLight(mAreaLightInf)->mInfo1.mIsFollowCamera;
    mBlendStep = 0;
}

const ActorLightInfo* ActorLightCtrl::getTargetActorLight(const AreaLightInfo* pInfo) const {
    s32 type = mLightType;

    if (type == 0) {
        return &pInfo->mPlayerLight;
    } else if (type == 1) {
        return &pInfo->mStrongLight;
    } else if (type == 2) {
        return &pInfo->mWeakLight;
    } else if (type == 3) {
        return &pInfo->mPlanetLight;
    }

    return 0;
}
