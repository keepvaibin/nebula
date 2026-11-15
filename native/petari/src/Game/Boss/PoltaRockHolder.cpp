#include "Game/Boss/PoltaRockHolder.hpp"
#include "Game/Boss/PoltaRock.hpp"

namespace {
    const s32 sMaxPoltaRock = 16;
};  // namespace

PoltaRockHolder::PoltaRockHolder() : DeriveActorGroup< PoltaRock >("ポルタの岩管理", ::sMaxPoltaRock) {
    for (s32 i = 0; i < ::sMaxPoltaRock; i++) {
        PoltaRock* pRock = new PoltaRock("ポルタ岩");
        pRock->initWithoutIter();
        registerActor(pRock);
    }
}
