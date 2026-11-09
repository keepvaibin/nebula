#include "Game/Gravity/GravityFollower.hpp"
#include "Game/Gravity/GlobalGravityObj.hpp"
#include "Game/Util/LiveActorUtil.hpp"

GravityFollower::GravityFollower(GlobalGravityObj* pGravityObj, const TableIter& rIter)
    : BaseMatrixFollower(rIter), mGravity(pGravityObj->getGravity()) {
}

void GravityFollower::setGravityFollowHost(const NameObj* pHost) {
    mGravity->mHost = pHost;
}

void GravityFollower::update() {
    GlobalGravityObj* pFollowTarget = static_cast< GlobalGravityObj* >(getFollowTargetActor());

    if (MR::isDead(pFollowTarget) || MR::isClipped(pFollowTarget) || !isValid()) {
        mGravity->mValidFollower = false;
    } else {
        Pos3f followMtx;
        calcFollowMatrix(&followMtx);

        mGravity->updateMtx(followMtx);
        mGravity->mValidFollower = true;
    }
}

namespace MR {
    bool addBaseMatrixFollowerGravity(GlobalGravityObj* pGravityObj, const TableIter& rIter) {
        if (!MR::isValidFollowID(rIter)) {
            return false;
        }

        MR::addBaseMatrixFollower(new GravityFollower(pGravityObj, rIter));
        return true;
    }
};  // namespace MR
