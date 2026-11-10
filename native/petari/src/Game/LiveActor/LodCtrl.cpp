#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/ClippingDirector.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include <cstring>
#include <nebula_game_api.h>
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_createFarModel_f1, "createFarModel.f1", 100.0f);

namespace {
    NEBULA_TUNABLE(bool, sOff, false);

    // The levels place view cubes for the actors that ship distance models, and only those follow them.
    bool hasDistanceModels(const LiveActor* pActor) {
        const char* res = MR::getModelResName(pActor);
        MR::FormattedName buf("/ObjectData/%sMiddle", res);
        if (MR::isFileExist(buf.c_str(), false)) {
            return true;
        }
        buf.format("/ObjectData/%sLow", res);
        return MR::isFileExist(buf.c_str(), false);
    }
};  // namespace

LodCtrl::LodCtrl(LiveActor* pActor, const TableIter& rIter)
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
    MR::FormattedName path("/ObjectData/%sLow", res);
    if (!MR::isFileExist(path.c_str(), false)) {
        return;
    }
    MR::FormattedName name("%sLow", res);
    const size_t size = strlen(name.c_str()) + 1;
    char* pObjName = new char[size];
    memcpy(pObjName, name.c_str(), size);
    mFarModel = new ModelObj(pObjName, pObjName, mActor->getBaseMtx(), drawBufferType, movementType, calcAnimType, false);
    mFarModel->initWithoutIter();
    mFarModel->makeActorDead();
    MR::setClippingTypeSphereContainsModelBoundingBox(mFarModel, cNum_createFarModel_f1);
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
    MR::FormattedName buf("/ObjectData/%sLow", pName);
    return MR::isFileExist(buf.c_str(), false);
}
