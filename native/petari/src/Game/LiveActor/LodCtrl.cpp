#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/ClippingDirector.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/ViewGroupCtrl.hpp"
#include "Game/LiveActor/ClippingActorHolder.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include <cstring>
#include <nebula_game_api.h>
#include "Game/Util/StringUtil.hpp"

namespace {
    const bool sOff = false;

    // The archive the actor's model comes from: its model's name, or a mod's class's archive holding it.
    const char* archiveOf(const LiveActor* pActor) {
        return NebulaMods::archiveOf(pActor->mName, MR::getModelResName(pActor));
    }

    // The levels place view cubes for the actors that ship distance models, and only those follow them.
    bool hasDistanceModels(const LiveActor* pActor) {
        const char* pArchive = archiveOf(pActor);
        return LodCtrlFunction::isExistLevel(pArchive, "Middle") || LodCtrlFunction::isExistLevel(pArchive, "Low");
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
    const char* pArchive = archiveOf(mActor);
    if (!LodCtrlFunction::isExistLevel(pArchive, "Low")) {
        return;
    }
    MR::FormattedName name("%sLow", pArchive);
    const size_t size = strlen(name.c_str()) + 1;
    char* pObjName = new char[size];
    memcpy(pObjName, name.c_str(), size);
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
    return isExistLevel(pName, "Low");
}

bool LodCtrlFunction::isExistLevel(const char* pArchive, const char* pLevel) {
    MR::FormattedName name("%s%s", pArchive, pLevel);
    // A mod's new model "<id>:<path>": its levels are archives beside it in the mod's folder (the cook writes
    // <path>Low.arc next to <path>.arc).
    if (strchr(pArchive, ':') != nullptr) {
        MR::FormattedName path;
        return MR::makeObjectArchiveFileName(path, name.c_str());
    }
    MR::FormattedName path("/ObjectData/%s", name.c_str());
    return MR::isFileExist(path.c_str(), false);
}

const LodCtrl* LodCtrlFunction::findOf(const LiveActor* pActor) {
    const ViewGroupCtrl* const pCtrl = MR::getClippingDirector()->mActorHolder->mViewGroupCtrl;
    for (s32 i = 0; i < pCtrl->mViewCtrlCount; i++) {
        if (pCtrl->mLodCtrls[i]->mActor == pActor) {
            return pCtrl->mLodCtrls[i];
        }
    }
    return nullptr;
}
