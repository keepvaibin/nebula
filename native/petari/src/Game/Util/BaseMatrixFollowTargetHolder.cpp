#include "Game/Util/BaseMatrixFollowTargetHolder.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/PlacementLink.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    BaseMatrixFollowTargetHolder* getBaseMatrixFollowTargetHolder() {
        return MR::getSceneObj< BaseMatrixFollowTargetHolder >(SceneObj_BaseMatrixFollowTargetHolder);
    }
};  // namespace

BaseMatrixFollower::BaseMatrixFollower(const TableIter& rIter) : mLinkInfo(nullptr), mFollowTarget(nullptr), mFollowID(-1) {
    MR::getPlacementFollowID(rIter, &mFollowID);
    mLinkInfo = new PlacementLink(rIter, false);
}

NameObj* BaseMatrixFollower::getFollowTargetActor() const {
    return mFollowTarget->mActor;
}

void BaseMatrixFollower::calcFollowMatrix(Pos3f* pOut) const {
    pOut->set(mFollowTarget->getHostBaseMtx());
    pOut->concat(*pOut, mFollowTarget->mInvHostMtx);
}

bool BaseMatrixFollower::isEnableFollow() const {
    BaseMatrixFollowTarget* pTarget = mFollowTarget;

    if (pTarget == nullptr) {
        return false;
    }

    return pTarget->mActor;
}

bool BaseMatrixFollower::isValid() const {
    return mFollowTarget->isValid(mFollowID);
}

BaseMatrixFollowTarget::BaseMatrixFollowTarget(const PlacementLink* pInfo) {
    mLinkInfo = pInfo;
    mHostBaseMtx = nullptr;
    mActor = nullptr;
    mValidater = nullptr;
    mInvHostMtx.identity();
}

void BaseMatrixFollowTarget::set(LiveActor* pActor, const Pos3f& rMtx, const Pos3f* pBaseMtx, BaseMatrixFollowValidater* pValidator) {
    mValidater = pValidator;
    mActor = pActor;
    mInvHostMtx.set(rMtx.mMtx);
    mInvHostMtx.invert(mInvHostMtx);

    if (pBaseMtx) {
        mHostBaseMtx = pBaseMtx;
    }
}

MtxPtr BaseMatrixFollowTarget::getHostBaseMtx() const {
    if (!mHostBaseMtx) {
        return mActor->getBaseMtx();
    }

    return const_cast< MtxPtr >(mHostBaseMtx->mMtx);
}

bool BaseMatrixFollowTarget::isValid(s32 followId) const {
    if (mValidater) {
        return mValidater->isValid(followId);
    }

    return true;
}

BaseMatrixFollowTargetHolder::BaseMatrixFollowTargetHolder(const char* pName, s32 targetCapacity, s32 followerCapacity) : NameObj(pName) {
    lumacore::setClass(this, lumacore::kClassId< BaseMatrixFollowTargetHolder >);
    mTargets.init(targetCapacity);
    mFollowers.init(followerCapacity);
    MR::connectToSceneMapObjMovement(this);
}

void BaseMatrixFollowTargetHolder::initAfterPlacement() {
    for (s32 i = 0; i < mFollowers.size(); i++) {
        BaseMatrixFollower* pFollower = mFollowers[i];
        BaseMatrixFollowTarget* pTarget = findFollowTarget(pFollower);
        if (pTarget->mActor) {
            pFollower->setGravityFollowHost(pTarget->mActor);
        }
    }
}

void BaseMatrixFollowTargetHolder::movement() {
    for (s32 i = 0; i < mFollowers.size(); i++) {
        if (mFollowers[i]->isEnableFollow()) {
            mFollowers[i]->update();
        }
    }
}

void BaseMatrixFollowTargetHolder::addFollower(BaseMatrixFollower* pFollower) {
    mFollowers.push_back(pFollower);

    BaseMatrixFollowTarget* pTarget = findFollowTarget(pFollower);
    if (!pTarget) {
        pTarget = new BaseMatrixFollowTarget(pFollower->mLinkInfo);
        mTargets.push_back(pTarget);
    }

    pFollower->mFollowTarget = pTarget;
}

void BaseMatrixFollowTargetHolder::setFollowTargetInfo(LiveActor* pActor, const TableIter& rIter, const Pos3f* pBaseMtx,
                                                       BaseMatrixFollowValidater* pValidater) {
    PlacementLink linkInfo(rIter, true);
    BaseMatrixFollowTarget* pTarget = findFollowTarget(&linkInfo);
    if (pTarget) {
        Pos3f mtx;
        MR::getPlacementMatrixFromRT(rIter, &mtx);
        pTarget->set(pActor, mtx, pBaseMtx, pValidater);
    }
}

BaseMatrixFollowTarget* BaseMatrixFollowTargetHolder::findFollowTarget(const PlacementLink* pLinkInfo) {
    if (!pLinkInfo) {
        return nullptr;
    }

    for (s32 i = 0; i < mTargets.size(); i++) {
        if (*mTargets[i]->mLinkInfo == *pLinkInfo) {
            return mTargets[i];
        }
    }

    return nullptr;
}

BaseMatrixFollowTarget* BaseMatrixFollowTargetHolder::findFollowTarget(const BaseMatrixFollower* pFollower) {
    if (pFollower == nullptr) {
        return nullptr;
    }

    return findFollowTarget(pFollower->mLinkInfo);
}

namespace MR {
    bool isValidFollowID(const TableIter& rIter) {
        if (!isValidInfo(rIter)) {
            return false;
        }

        PlacementLink linkInfo(rIter, false);
        return linkInfo.isValid();
    }

    void addBaseMatrixFollower(BaseMatrixFollower* pFollower) {
        createSceneObj(SceneObj_BaseMatrixFollowTargetHolder);
        ::getBaseMatrixFollowTargetHolder()->addFollower(pFollower);
    }

    void addBaseMatrixFollowTarget(LiveActor* pActor, const TableIter& rIter, const Pos3f* pBaseMtx, BaseMatrixFollowValidater* pValidater) {
        if (isValidInfo(rIter) && isExistSceneObj(SceneObj_BaseMatrixFollowTargetHolder)) {
            ::getBaseMatrixFollowTargetHolder()->setFollowTargetInfo(pActor, rIter, pBaseMtx, pValidater);
        }
    }
};  // namespace MR

void BaseMatrixFollower::update() {
}
