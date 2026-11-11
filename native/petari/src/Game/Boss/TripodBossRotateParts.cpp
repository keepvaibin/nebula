#include "Game/Boss/TripodBossRotateParts.hpp"
#include "Game/Boss/TripodBossFixParts.hpp"
#include "Game/MapObj/MapPartsRotator.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

TripodBossRotateParts::TripodBossRotateParts(const char* pName) : TripodBossFixParts(pName), mRotator() {
    lumacore::setClass(this, lumacore::kClassId< TripodBossRotateParts >);
}

void TripodBossRotateParts::init(const TableIter& rIter) {
    TripodBossFixParts::init(rIter);

    mRotator = new MapPartsRotator(this);
    mRotator->init(rIter);
}

void TripodBossRotateParts::makeActorDead() {
    if (mRotator != nullptr && mRotator->isWorking()) {
        mRotator->end();
    }

    TripodBossFixPartsBase::makeActorDead();
}

void TripodBossRotateParts::activateTripodBoss() {
    TripodBossFixParts::activateTripodBoss();
    mRotator->start();
}

void TripodBossRotateParts::calcTripodLocalMatrix(Pos3f* pMtx) {
    if (mRotator->isWorking()) {
        pMtx->set(mRotator->getRotateMtx());
        pMtx->setTrans(mLocalPos);
    } else {
        MR::makeMtxTR(*pMtx, mLocalPos, mRotation);
    }
}

void TripodBossRotateParts::exeWaitOwn() {
    mRotator->movement();
    MR::startLevelSound(this, "SE_BM_LV_TRIPOD_PARTS_MOVE");
}
