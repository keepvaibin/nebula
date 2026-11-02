#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/ClippingDirector.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include <cstdio>
#include <nebula_game_api.h>

namespace {
    const bool sNotHidden = false;

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
    : mActor(pActor), mFollowsView(hasDistanceModels(pActor)), mHidden(&sNotHidden), mViewGroupID(-1) {
    MR::getClippingDirector()->entryLodCtrl(this, rIter);
}

void LodCtrl::appear() {
    MR::showModel(mActor);
}

void LodCtrl::kill() {
    MR::showModel(mActor);
}

void LodCtrl::validate() {
    MR::showModel(mActor);
    mFollowsView = true;
}

void LodCtrl::invalidate() {
    MR::showModel(mActor);
    mFollowsView = false;
}

void LodCtrl::update() {
    if (MR::isDead(mActor) || !mFollowsView) {
        return;
    }
    if (*mHidden) {
        if (!MR::isHiddenModel(mActor)) {
            MR::hideModelAndOnCalcAnim(mActor);
        }
    } else if (MR::isHiddenModel(mActor)) {
        MR::showModel(mActor);
    }
}

void LodCtrl::setClippingTypeSphereContainsModelBoundingBox(f32 bounds) {
    MR::setClippingTypeSphereContainsModelBoundingBox(mActor, bounds);
}

void LodCtrl::setFarClipping(f32 clip) {
    MR::setClippingFar(mActor, clip);
}

void LodCtrl::invalidateClipping() {
    MR::invalidateClipping(mActor);
}

void LodCtrl::setHiddenPtr(const bool* pHidden) {
    mHidden = pHidden;
}

bool LodCtrlFunction::isExistLodLowModel(const char* pName) {
    char buf[0x100];
    nebula_snprintf(buf, sizeof(buf), "/ObjectData/%sLow.arc", pName);
    return MR::isFileExist(buf, false);
}
