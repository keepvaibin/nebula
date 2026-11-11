#include "Game/Boss/KoopaPowerUpSwitch.hpp"
#include "Game/Boss/KoopaFunction.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/System/LumaCore.hpp"

KoopaPowerUpSwitch::KoopaPowerUpSwitch(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< KoopaPowerUpSwitch >);
}

void KoopaPowerUpSwitch::init(const TableIter& rIter) {
    MR::useStageSwitchWriteA(this, rIter);
    MR::useStageSwitchWriteB(this, rIter);
    makeActorDead();
}

void KoopaPowerUpSwitch::initAfterPlacement() {
    KoopaFunction::registerKoopaPowerUpSwitch(this);
}
