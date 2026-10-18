#include "Game/Boss/BossStinkBugBombHolder.hpp"
#include "Game/Boss/BossStinkBugBomb.hpp"
#include "Game/Util/LiveActorUtil.hpp"

namespace {
    static const s32 sMaxBossStinkBugBomb = 16;
};  // namespace

BossStinkBugBombHolder::BossStinkBugBombHolder(LiveActor* pHost)
    : DeriveActorGroup< BossStinkBugBomb >("\203\173\203\130\203\112\203\201\203\200\203\126\224\232\222\145\212\307\227\235" /* ボスカメムシ爆弾管理 */, ::sMaxBossStinkBugBomb) {
    BossStinkBugBomb* pBomb;

    for (s32 i = 0; i < ::sMaxBossStinkBugBomb; i++) {
        pBomb = new BossStinkBugBomb("\203\173\203\130\203\112\203\201\203\200\203\126\224\232\222\145" /* ボスカメムシ爆弾 */);
        pBomb->initWithoutIter();

        MR::setBinderExceptActor(pBomb, pHost);
        registerActor(pBomb);
    }
}
