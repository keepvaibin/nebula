#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/ClippingDirector.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include <cstdio>
#include <cstring>
#include <nebula_game_api.h>

namespace {
    const bool sOff = false;

    // The levels place view cubes for the actors that ship distance models, and only those follow them.
    bool hasDistanceModels(const LiveActor* pActor) {
        const char* res = MR::getModelResName(pActor);
        char buf[0x100];
        nebula_snprintf(buf, sizeof(buf), "/ObjectData/%sMiddle.arc", res);
        if (MR::isFileExist(buf, false)) {
            return true;
        }
        nebula_snprintf(buf, sizeof(buf), "/ObjectData/%sLow.arc", res);
        return MR::isFileExist(buf, false);
    }
};  // namespace

LodCtrl::LodCtrl(LiveActor* pActor, const JMapInfoIter& rIter)
    : mActor(pActor), mFarModel(nullptr), mFollowsView(hasDistanceModels(pActor)), mNear(&sOff), mFar(&sOff), mHidden(&sOff),
      mViewGroupID(-1) {
    MR::getClippingDirector()->entryLodCtrl(this, rIter);
}

void LodCtrl::appear() {
    showNear();
}

void LodCtrl::kill() {
    showNear();
}

void LodCtrl::validate() {
    showNear();
    mFollowsView = true;
}

void LodCtrl::invalidate() {
    showNear();
    mFollowsView = false;
}

void LodCtrl::showNear() {
    MR::showModel(mActor);
    if (mFarModel != nullptr && !MR::isDead(mFarModel)) {
        mFarModel->makeActorDead();
    }
}

void LodCtrl::createFarModel(int drawBufferType, int movementType, int calcAnimType) {
    if (mViewGroupID < 0) {
        return;
    }
    const char* res = MR::getModelResName(mActor);
    char path[0x100];
    nebula_snprintf(path, sizeof(path), "/ObjectData/%sLow.arc", res);
    if (!MR::isFileExist(path, false)) {
        return;
    }
    char name[0x100];
    nebula_snprintf(name, sizeof(name), "%sLow", res);
    const size_t size = strlen(name) + 1;
    char* pObjName = new char[size];
    memcpy(pObjName, name, size);
    mFarModel = new ModelObj(pObjName, pObjName, mActor->getBaseMtx(), drawBufferType, movementType, calcAnimType, false);
    mFarModel->initWithoutIter();
    mFarModel->makeActorDead();
    MR::setClippingTypeSphereContainsModelBoundingBox(mFarModel, 100.0f);
    MR::copyTransRotateScale(mActor, mFarModel);
}

void LodCtrl::update() {
    if (MR::isDead(mActor) || !mFollowsView) {
        return;
    }
    if (*mHidden) {
        if (!MR::isHiddenModel(mActor)) {
            MR::hideModelAndOnCalcAnim(mActor);
        }
        if (mFarModel != nullptr && !MR::isDead(mFarModel)) {
            mFarModel->makeActorDead();
        }
    } else if (mFarModel != nullptr && *mFar && !*mNear) {
        if (!MR::isHiddenModel(mActor)) {
            MR::hideModelAndOnCalcAnim(mActor);
        }
        if (MR::isDead(mFarModel)) {
            mFarModel->makeActorAppeared();
        }
        MR::copyTransRotateScale(mActor, mFarModel);
    } else {
        showNear();
    }
}

void LodCtrl::setClippingTypeSphereContainsModelBoundingBox(f32 bounds) {
    MR::setClippingTypeSphereContainsModelBoundingBox(mActor, bounds);
    if (mFarModel != nullptr) {
        MR::setClippingTypeSphereContainsModelBoundingBox(mFarModel, bounds);
    }
}

void LodCtrl::setFarClipping(f32 clip) {
    MR::setClippingFar(mActor, clip);
    if (mFarModel != nullptr) {
        MR::setClippingFar(mFarModel, clip);
    }
}

void LodCtrl::invalidateClipping() {
    MR::invalidateClipping(mActor);
    if (mFarModel != nullptr) {
        MR::invalidateClipping(mFarModel);
    }
}

void LodCtrl::setViewCtrlPtr(const bool* pNear, const bool* pFar, const bool* pHidden) {
    mNear = pNear;
    mFar = pFar;
    mHidden = pHidden;
}

bool LodCtrlFunction::isExistLodLowModel(const char* pName) {
    char buf[0x100];
    nebula_snprintf(buf, sizeof(buf), "/ObjectData/%sLow.arc", pName);
    return MR::isFileExist(buf, false);
}
