#include "Game/Boss/PoltaGroundRockHolder.hpp"
#include "Game/Boss/PoltaGroundRock.hpp"

namespace {
    static const s32 sMaxPoltaGroundRock = 16;
};  // namespace

PoltaGroundRockHolder::PoltaGroundRockHolder() : DeriveActorGroup< PoltaGroundRock >("\203\174\203\213\203\136\222\156\226\312\212\342\212\307\227\235" /* ポルタ地面岩管理 */, ::sMaxPoltaGroundRock) {
    PoltaGroundRock* pGroundRock;

    for (int i = 0; i < ::sMaxPoltaGroundRock; i++) {
        pGroundRock = new PoltaGroundRock("\203\174\203\213\203\136\222\156\226\312\212\342" /* ポルタ地面岩 */);
        pGroundRock->initWithoutIter();
        registerActor(pGroundRock);
    }
}

void PoltaGroundRockHolder::breakAll() {
    int memberNum = getObjNum();

    for (int i = 0; i < memberNum; i++) {
        getMember(i)->requestBreak();
    }
}
