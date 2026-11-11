#include "Game/Boss/TripodBossRailMoveParts.hpp"
#include "Game/Boss/TripodBossFixParts.hpp"
#include "Game/MapObj/MapPartsRailMover.hpp"
#include "Game/System/LumaCore.hpp"

TripodBossRailMoveParts::TripodBossRailMoveParts(const char* pName) : TripodBossFixParts(pName), mRailMover() {
    lumacore::setClass(this, lumacore::kClassId< TripodBossRailMoveParts >);
}

void TripodBossRailMoveParts::init(const TableIter& rIter) {
    TripodBossFixParts::init(rIter);
    initRailRider(rIter);

    mRailMover = new MapPartsRailMover(this);
    mRailMover->init(rIter);
}

void TripodBossRailMoveParts::makeActorDead() {
    if (mRailMover != nullptr && mRailMover->isWorking()) {
        mRailMover->end();
    }

    TripodBossFixPartsBase::makeActorDead();
}

void TripodBossRailMoveParts::activateTripodBoss() {
    mRailMover->start();
    TripodBossFixParts::activateTripodBoss();
}

void TripodBossRailMoveParts::calcTripodLocalMatrix(Pos3f* pMtx) {
    mLocalPos = mRailMover->mStartPos;

    TripodBossFixPartsBase::calcTripodLocalMatrix(pMtx);
}

void TripodBossRailMoveParts::exeWaitOwn() {
    mRailMover->movement();
}
