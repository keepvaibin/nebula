#include "Game/MapObj/BumpAppearPlanet.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/CollisionParts.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvBumpAppearPlanet {
    NEW_NERVE(BumpAppearPlanetNrvWait, BumpAppearPlanet, Wait);
    NEW_NERVE(BumpAppearPlanetNrvBumpOut, BumpAppearPlanet, BumpOut);
    NEW_NERVE(BumpAppearPlanetNrvBumpIn, BumpAppearPlanet, BumpIn);
};  // namespace NrvBumpAppearPlanet

BumpAppearPlanet::BumpAppearPlanet(const char* pName) : MapObjActor(pName), mCollisionB(), mCollisionC() {
    lumacore::setClass(this, lumacore::kClassId< BumpAppearPlanet >);
}

void BumpAppearPlanet::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    MapObjActorUtil::setupInitInfoPlanet(&info);
    info.setupSound(4);
    info.setupNerve(GET_NERVE(BumpAppearPlanet, BumpAppearPlanetNrvWait));
    initialize(rIter, info);

    mCollisionB = MR::createCollisionPartsFromLiveActor(this, "BumpAppearPlanetB", getSensor("body"), MR::getJointMtx(this, "BumpAppearPlanetB"),
                                                MR::CollisionScaleType_Default);
    mCollisionC = MR::createCollisionPartsFromLiveActor(this, "BumpAppearPlanetC", getSensor("body"), MR::getJointMtx(this, "BumpAppearPlanetC"),
                                                MR::CollisionScaleType_Default);
    MR::validateCollisionParts(mCollisionC);
}

void BumpAppearPlanet::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::invalidateCollisionParts(mCollisionB);
    }
}

void BumpAppearPlanet::exeBumpOut() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "BumpAppearPlanetPush");
        MR::startSound(this, "SE_OJ_BUMP_APPEAR_PLN_OUT");
        MR::validateCollisionParts(mCollisionB);
    }
}

void BumpAppearPlanet::exeBumpIn() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "BumpAppearPlanetPull");
        MR::startSound(this, "SE_OJ_BUMP_APPEAR_PLN_IN");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(BumpAppearPlanet, BumpAppearPlanetNrvWait));
    }
}

void BumpAppearPlanet::control() {
    if (mCollisionB->mIsValid) {
        mCollisionB->setMtx();
    }

    if (mCollisionC->mIsValid) {
        mCollisionC->setMtx();
    }
}

void BumpAppearPlanet::initCaseUseSwitchA(const MapObjActorInitInfo& /*rInfo*/) {
    MR::listenStageSwitchOnOffA(this, MR::Functor(this, &BumpAppearPlanet::startBumpOut), MR::Functor(this, &BumpAppearPlanet::startBumpIn));
}

void BumpAppearPlanet::startBumpOut() {
    setNerve(GET_NERVE(BumpAppearPlanet, BumpAppearPlanetNrvBumpOut));
}

void BumpAppearPlanet::startBumpIn() {
    setNerve(GET_NERVE(BumpAppearPlanet, BumpAppearPlanetNrvBumpIn));
}
