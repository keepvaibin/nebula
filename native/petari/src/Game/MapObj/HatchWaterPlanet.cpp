#include "Game/MapObj/HatchWaterPlanet.hpp"
#include "Game/LiveActor/LodCtrl.hpp"
#include "Game/LiveActor/ModelObj.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);

namespace NrvHatchWaterPlanet {
    NEW_NERVE(HatchWaterPlanetNrvWait, HatchWaterPlanet, Wait);
    NEW_NERVE(HatchWaterPlanetNrvOpen, HatchWaterPlanet, Open);
    NEW_NERVE(HatchWaterPlanetNrvWaitAfterOpen, HatchWaterPlanet, WaitAfterOpen);
};  // namespace NrvHatchWaterPlanet

HatchWaterPlanet::HatchWaterPlanet(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< HatchWaterPlanet >);
    mPlanetLODCtrl = nullptr;
    mCollisionParts = nullptr;
}

void HatchWaterPlanet::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("HatchWaterPlanet", nullptr, false);
    MR::connectToScenePlanet(this);
    initHitSensor(1);
    MR::addBodyMessageSensorMapObj(this);
    MR::initCollisionParts(this, "HatchWaterPlanetBefore", getSensor(nullptr), nullptr);
    mCollisionParts = MR::createCollisionPartsFromLiveActor(this, "HatchWaterPlanetAfter", getSensor(nullptr), MR::CollisionScaleType_Default);
    MR::invalidateCollisionParts(mCollisionParts);
    initEffectKeeper(0, nullptr);
    initSound(4, false);
    if (MR::tryRegisterDemoCast(this, rIter)) {
        MR::registerDemoActionNerve(this, GET_NERVE(HatchWaterPlanet, HatchWaterPlanetNrvOpen), nullptr);
    }

    MR::setClippingTypeSphereContainsModelBoundingBox(this, cNum_init_f1);
    MR::setClippingFarMax(this);
    mPlanetLODCtrl = MR::createLodCtrlPlanet(this, rIter, -1.0f);
    mPlanetLODCtrl->validate();
    initNerve(GET_NERVE(HatchWaterPlanet, HatchWaterPlanetNrvWait));
    makeActorAppeared();
}

void HatchWaterPlanet::control() {
    mPlanetLODCtrl->update();
}

void HatchWaterPlanet::exeWait() {
}

void HatchWaterPlanet::exeOpen() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "HatchWaterPlanet");
        MR::startBtk(this, "HatchWaterPlanet");
        MR::startSound(this, "SE_OJ_HATCH_WATER_PNT_ST");
    }

    MR::startLevelSound(this, "SE_OJ_LV_HATCH_WATER_PNT_OP");

    if (MR::isBckStopped(this)) {
        MR::startSound(this, "SE_OJ_HATCH_WATER_PNT_ED");
        MR::startSystemSE("SE_SY_READ_RIDDLE_S");
        setNerve(GET_NERVE(HatchWaterPlanet, HatchWaterPlanetNrvWaitAfterOpen));
    }
}

void HatchWaterPlanet::exeWaitAfterOpen() {
    if (MR::isFirstStep(this)) {
        MR::invalidateCollisionParts(this);
        MR::validateCollisionParts(mCollisionParts);
    }
}
