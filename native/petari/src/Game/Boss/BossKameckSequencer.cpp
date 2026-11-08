#include "Game/Boss/BossKameckSequencer.hpp"
#include "Game/Boss/BossKameckBattleDemo.hpp"

BossKameckSequencer::BossKameckSequencer(const char* pName) : NerveExecutor(pName), mBossKameck(), mBattleDemo() {
}

void BossKameckSequencer::init(BossKameck* pBoss, const TableIter& rIter) {
    mBossKameck = pBoss;

    mBattleDemo = new BossKameckBattleDemo(pBoss, rIter);
    mBattleDemo->init();
}

void BossKameckSequencer::update() {
    updateNerve();
}
