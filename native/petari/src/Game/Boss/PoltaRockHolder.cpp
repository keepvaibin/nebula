#include "Game/Boss/PoltaRockHolder.hpp"
#include "Game/Boss/PoltaRock.hpp"

namespace {
    static const s32 sMaxPoltaRock = 16;
};  // namespace

PoltaRockHolder::PoltaRockHolder() : DeriveActorGroup< PoltaRock >("\203\174\203\213\203\136\202\314\212\342\212\307\227\235" /* ポルタの岩管理 */, ::sMaxPoltaRock) {
    PoltaRock* pRock;

    for (int i = 0; i < ::sMaxPoltaRock; i++) {
        pRock = new PoltaRock("\203\174\203\213\203\136\212\342" /* ポルタ岩 */);
        pRock->initWithoutIter();
        registerActor(pRock);
    }
}
