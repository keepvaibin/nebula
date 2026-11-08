#include "Game/LiveActor/ActorLightCtrl.hpp"
#include "Game/Map/LightFunction.hpp"
#include "Game/NameObj/NameObjExecuteHolder.hpp"
#include "Game/System/DrawBuffer.hpp"
#include "Game/Util.hpp"

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

bool ActorLightCtrl::isSameLight(const ActorLightCtrl* pLight) const {
    if (mBlendFrom) {
        return false;
    }

    return mAreaLightInf == pLight->mAreaLightInf;
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

void ActorLightCtrl::tryFindNewAreaLight(bool a2) {
    if (LightFunction::tryFindNewAreaLightID(mActor->mPosition, &mLightID)) {
        if (mAreaLightInf != nullptr) {
            const ActorLightInfo* inf = getTargetActorLight(mAreaLightInf);
            mBlendFrom = inf;
            mLightInfo = *inf;
        }

        resetCurrentLightInfo();

        if (!mInterpolate || a2) {
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
            f32 v = (f32)mBlendStep / mInterpolate;
            const ActorLightInfo* info = getTargetActorLight(mAreaLightInf);
            LightFunction::blendActorLightInfo(&mLightInfo, *mBlendFrom, *info, v);
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
