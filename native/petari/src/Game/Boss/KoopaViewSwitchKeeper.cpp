#include "Game/Boss/KoopaViewSwitchKeeper.hpp"
#include "Game/Boss/KoopaFunction.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/System/LumaCore.hpp"

KoopaViewSwitchKeeper::KoopaViewSwitchKeeper(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< KoopaViewSwitchKeeper >);
}

void KoopaViewSwitchKeeper::init(const TableIter& rIter) {
    MR::useStageSwitchReadAppear(this, rIter);
    MR::useStageSwitchReadA(this, rIter);

    makeActorDead();
}

void KoopaViewSwitchKeeper::initAfterPlacement() {
    KoopaFunction::registerKoopaViewSwitchKeeper(this);
}
