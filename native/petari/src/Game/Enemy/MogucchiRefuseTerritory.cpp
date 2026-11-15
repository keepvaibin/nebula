#include "Game/Enemy/MogucchiRefuseTerritory.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

MogucchiRefuseTerritory::MogucchiRefuseTerritory(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< MogucchiRefuseTerritory >);
}

void MogucchiRefuseTerritory::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::connectToSceneEnemyMovement(this);
    initHitSensor(1);
    MR::addHitSensor(this, "body", 0x20, 8, (100.0f * mScale.x), Vec3f(0.0f));
    makeActorAppeared();
}
