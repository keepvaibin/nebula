#include "Game/Enemy/MogucchiRefuseTerritory.hpp"
#include "Game/Util.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 100.0f);

MogucchiRefuseTerritory::MogucchiRefuseTerritory(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< MogucchiRefuseTerritory >);
}

void MogucchiRefuseTerritory::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);
    MR::connectToSceneEnemyMovement(this);
    initHitSensor(1);
    MR::addHitSensor(this, "body", 0x20, 8, (cNum_init_f1 * mScale.x), Vec3f(0.0f));
    makeActorAppeared();
}
