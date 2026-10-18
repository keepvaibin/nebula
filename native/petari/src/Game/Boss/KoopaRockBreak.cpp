#include "Game/Boss/KoopaRockBreak.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Util/LiveActorUtil.hpp"

KoopaRockBreak::KoopaRockBreak(LiveActor* pActor) : PartsModel(pActor, "\212\342\211\363\202\352\203\202\203\146\203\213" /* 岩壊れモデル */, "KoopaRockBreak", nullptr, MR::DrawBufferType_Enemy, false) {
}

void KoopaRockBreak::init(const JMapInfoIter& rIter) {
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

KoopaRockBreak::~KoopaRockBreak() {
}
