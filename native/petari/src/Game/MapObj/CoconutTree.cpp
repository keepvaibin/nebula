#include "Game/MapObj/CoconutTree.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 2000.0f);

CoconutTree::CoconutTree(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< CoconutTree >);
}

void CoconutTree::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    initModelManagerWithAnm("CoconutTree", nullptr, false);
    MR::connectToSceneMapObj(this);
    initHitSensor(1);
    MR::addBodyMessageSensorMapObj(this);
    MR::initCollisionParts(this, "CoconutTree", getSensor("body"), nullptr);
    MR::setClippingTypeSphere(this, cNum_init_f1);
    makeActorAppeared();
}