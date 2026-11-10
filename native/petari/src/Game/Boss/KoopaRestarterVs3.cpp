#include "Game/Boss/KoopaRestarterVs3.hpp"
#include "Game/Boss/KoopaFunction.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/System/LumaCore.hpp"

KoopaRestarterVs3::KoopaRestarterVs3(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< KoopaRestarterVs3 >);
}

void KoopaRestarterVs3::init(const TableIter& rIter) {
    MR::needStageSwitchReadAppear(this, rIter);
    MR::syncStageSwitchAppear(this);

    makeActorDead();
}

void KoopaRestarterVs3::appear() {
    LiveActor::appear();

    KoopaFunction::tryRestartKoopa();
}
