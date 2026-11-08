#include "Game/Boss/PoltaGroundRockHolder.hpp"
#include "Game/Boss/PoltaGroundRock.hpp"

namespace {
    const s32 sMaxPoltaGroundRock = 16;
};  // namespace

PoltaGroundRockHolder::PoltaGroundRockHolder() : DeriveActorGroup< PoltaGroundRock >("ポルタ地面岩管理", ::sMaxPoltaGroundRock) {
    for (s32 i = 0; i < ::sMaxPoltaGroundRock; i++) {
        PoltaGroundRock* pGroundRock = new PoltaGroundRock("ポルタ地面岩");
        pGroundRock->initWithoutIter();
        registerActor(pGroundRock);
    }
}

void PoltaGroundRockHolder::breakAll() {
    s32 memberNum = getObjNum();

    for (s32 i = 0; i < memberNum; i++) {
        getMember(i)->requestBreak();
    }
}
