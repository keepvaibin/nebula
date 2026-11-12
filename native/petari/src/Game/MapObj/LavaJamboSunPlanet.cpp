#include "Game/MapObj/LavaJamboSunPlanet.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MapPartsUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvLavaJamboSunPlanet {
    NEW_NERVE(HostTypeWait, LavaJamboSunPlanet, Wait);
    NEW_NERVE(HostTypePause, LavaJamboSunPlanet, Wait);
    NEW_NERVE(HostTypeMove, LavaJamboSunPlanet, Move);
};  // namespace NrvLavaJamboSunPlanet

LavaJamboSunPlanet::LavaJamboSunPlanet(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< LavaJamboSunPlanet >);
    mBloomModel = nullptr;
    mLodCtrl = nullptr;
    mMoveSpeed = 0.0f;
    mClipCenter.setZeroVec();
}

void LavaJamboSunPlanet::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("LavaJamboSunPlanet", nullptr, false);
    initHitSensor(1);
    HitSensor* bodySensor = MR::addBodyMessageSensorMapObj(this);
    MR::initCollisionParts(this, "LavaJamboSunPlanet", bodySensor, nullptr);
    initNerve(GET_NERVE(LavaJamboSunPlanet, HostTypeWait));
    initRailRider(rIter);
    MR::initAndSetRailClipping(&mClipCenter, this, 100.0f, 500.0f);
    MR::needStageSwitchReadA(this, rIter);
    MR::needStageSwitchReadB(this, rIter);
    MR::connectToScenePlanet(this);
    mBloomModel = MR::createBloomModel(this, getBaseMtx());
    initSound(4, false);
    MR::setClippingTypeSphereContainsModelBoundingBox(this, 100.0f);
    MR::setClippingFarMax(this);
    MR::startBtk(this, "LavaJamboSunPlanet");
    mLodCtrl = MR::createLodCtrlPlanet(this, rIter, -1.0f);
    MR::tryRegisterDemoCast(this, rIter);
    makeActorAppeared();
}

void LavaJamboSunPlanet::initAfterPlacement() {
    MR::moveCoordAndTransToRailPoint(this, 0);
}

void LavaJamboSunPlanet::control() {
    mLodCtrl->update();
}

void LavaJamboSunPlanet::exeWait() {
    if (isNerve(GET_NERVE(LavaJamboSunPlanet, HostTypeWait)) && MR::isOnSwitchA(this)) {
        setNerve(GET_NERVE(LavaJamboSunPlanet, HostTypeMove));
        return;
    }
    if (isNerve(GET_NERVE(LavaJamboSunPlanet, HostTypePause)) && MR::isOnSwitchB(this)) {
        setNerve(GET_NERVE(LavaJamboSunPlanet, HostTypeMove));
    }
}

void LavaJamboSunPlanet::exeMove() {
    MR::getMapPartsArgMoveSpeed(&mMoveSpeed, this);
    mMoveSpeed *= 0.1f;
    MR::moveCoordAndFollowTrans(this, mMoveSpeed);
    MR::startLevelSound(this, "SE_AT_LV_JUMBO_SUN_MOVE");
    s32 pointAction = -1;
    MR::getCurrentRailPointArg1NoInit(this, &pointAction);
    if (pointAction == 1) {
        setNerve(GET_NERVE(LavaJamboSunPlanet, HostTypePause));
    } else if (MR::isRailReachedGoal(this)) {
        setNerve(GET_NERVE(LavaJamboSunPlanet, HostTypeWait));
    }
}
