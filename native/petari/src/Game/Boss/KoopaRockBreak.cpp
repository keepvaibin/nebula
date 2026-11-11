#include "Game/Boss/KoopaRockBreak.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/System/LumaCore.hpp"

KoopaRockBreak::KoopaRockBreak(LiveActor* pActor) : PartsModel(pActor, "岩壊れモデル", "KoopaRockBreak", nullptr, MR::DrawBufferType_Enemy, false) {
    lumacore::setClass(this, lumacore::kClassId< KoopaRockBreak >);
}

void KoopaRockBreak::init(const TableIter& rIter) {
    PartsModel::init(rIter);

    MR::invalidateClipping(this);

    MR::initLightCtrl(this);

    loadFixedPosition("RockBreakFixPos");

    makeActorDead();
}

void KoopaRockBreak::control() {
    if (MR::isActionEnd(this)) {
        kill();
    }
}
