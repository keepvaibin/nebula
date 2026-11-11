#include "Game/Boss/TripodBossKinokoOneUp.hpp"
#include "Game/Boss/TripodBossAccesser.hpp"
#include "Game/MapObj/BenefitItemOneUp.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvTripodBossKinokoOneUp {
    NEW_NERVE(TripodBossKinokoOneUpNrvActive, TripodBossKinokoOneUp, Active);
    NEW_NERVE(TripodBossKinokoOneUpNrvEnd, TripodBossKinokoOneUp, End);
};  // namespace NrvTripodBossKinokoOneUp

TripodBossKinokoOneUp::TripodBossKinokoOneUp(const char* pName) : LiveActor(pName), mOneUp(), mJointID(-1) {
    lumacore::setClass(this, lumacore::kClassId< TripodBossKinokoOneUp >);
    mLocalMtx.identity();
    mMtx.identity();
}

void TripodBossKinokoOneUp::init(const TableIter& rIter) {
    MR::getPlacementMatrixFromRT(rIter, &mLocalMtx);
    MR::connectToSceneMapObjDecorationMovement(this);
    MR::invalidateClipping(this);
    MR::getPlacementArg0NoInit(rIter, &mJointID);
    initNerve(GET_NERVE(TripodBossKinokoOneUp, TripodBossKinokoOneUpNrvActive));

    mOneUp = new BenefitItemOneUp("1UPキノコ");
    mOneUp->setFollowMtx(mMtx);
    mOneUp->initWithoutIter();
    MR::invalidateClipping(mOneUp);

    makeActorDead();
    MR::addTripodBossParts(this);
}

void TripodBossKinokoOneUp::control() {
}

void TripodBossKinokoOneUp::exeActive() {
    mMtx.set(mLocalMtx);
    MR::concatTripodBossAttachJointMatrix(&mMtx, mJointID);
    mMtx.getTrans(mPosition);

    if (MR::isFirstStep(this)) {
        mOneUp->makeActorAppeared();
    }

    if (MR::isBrokenTripodBoss() || MR::isDead(mOneUp)) {
        mOneUp->kill();
        setNerve(GET_NERVE(TripodBossKinokoOneUp, TripodBossKinokoOneUpNrvEnd));
    }
}

void TripodBossKinokoOneUp::exeEnd() {
}
