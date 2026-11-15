#include "Game/MapObj/CoconutTree.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

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
    MR::setClippingTypeSphere(this, 2000.0f);
    makeActorAppeared();
}