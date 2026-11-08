#include "Game/Boss/BossKameckVs1.hpp"
#include "Game/Boss/BossKameck.hpp"
#include "Game/Boss/BossKameckBattleDemo.hpp"
#include "Game/Boss/BossKameckBattlePattern.hpp"
#include "Game/Boss/BossKameckStateBattle.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorStateUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"

namespace {
    const s32 sBeamPatternLv1[] = {2, 1, BossKameckBattlePattern::cPatternEnd};
    const s32 sBeamPatternLv2[] = {3, 1, BossKameckBattlePattern::cPatternEnd};
    const s32 sBeamPatternLv3[] = {4, 1, 3, BossKameckBattlePattern::cPatternEnd};
    static BossKameckBattlePattern sPatternLv1 = BossKameckBattlePattern(sBeamPatternLv1, false);
    static BossKameckBattlePattern sPatternLv2 = BossKameckBattlePattern(sBeamPatternLv2, false);
    static BossKameckBattlePattern sPatternLv3 = BossKameckBattlePattern(sBeamPatternLv3, false);
};  // namespace

namespace NrvBossKameckVs1 {
    NEW_NERVE(BossKameckVs1NrvOpeningDemo, BossKameckVs1, OpeningDemo);
    NEW_NERVE(BossKameckVs1NrvBattleLv1, BossKameckVs1, BattleLv1);
    NEW_NERVE(BossKameckVs1NrvBattleLv2, BossKameckVs1, BattleLv2);
    NEW_NERVE(BossKameckVs1NrvPowerUpDemo, BossKameckVs1, PowerUpDemo);
    NEW_NERVE(BossKameckVs1NrvBattleLv3, BossKameckVs1, BattleLv3);
    NEW_NERVE(BossKameckVs1NrvEndDemo, BossKameckVs1, EndDemo);
};  // namespace NrvBossKameckVs1

BossKameckVs1::BossKameckVs1() : BossKameckSequencer("ボスカメックVs1"), mStateBattle() {
}

void BossKameckVs1::init(BossKameck* pBoss, const TableIter& rIter) {
    BossKameckSequencer::init(pBoss, rIter);

    mStateBattle = new BossKameckStateBattle(pBoss);
    mStateBattle->init();

    initNerve(GET_NERVE(BossKameckVs1, BossKameckVs1NrvOpeningDemo));
    MR::declareStarPiece(mBossKameck, 24);
}

void BossKameckVs1::start() {
    setNerve(GET_NERVE(BossKameckVs1, BossKameckVs1NrvOpeningDemo));
}

void BossKameckVs1::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isBattle()) {
        mStateBattle->attackSensor(pSender, pReceiver);
    }
}

bool BossKameckVs1::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isBattle()) {
        return mStateBattle->receiveMsgPlayerAttack(msg, pSender, pReceiver);
    }

    return false;
}

void BossKameckVs1::exeOpeningDemo() {
    if (MR::isFirstStep(this)) {
        mBattleDemo->startDemoAppearVs1();
    }

    if (MR::updateActorState(this, mBattleDemo)) {
        setNerve(GET_NERVE(BossKameckVs1, BossKameckVs1NrvBattleLv1));
    }
}

void BossKameckVs1::exeBattleLv1() {
    if (MR::isFirstStep(this)) {
        mStateBattle->setMoveRail(mBossKameck->getMoveRail(0));
        mStateBattle->setBattlePattern(&::sPatternLv1);
        mStateBattle->mIsFinal = false;
    }

    MR::updateActorStateAndNextNerve(this, mStateBattle, GET_NERVE(BossKameckVs1, BossKameckVs1NrvBattleLv2));
}

void BossKameckVs1::exeBattleLv2() {
    if (MR::isFirstStep(this)) {
        mBossKameck->appearStarPieceToPlayer(8);
        mStateBattle->setMoveRail(mBossKameck->getMoveRail(1));
        mStateBattle->setBattlePattern(&::sPatternLv2);
        mStateBattle->mIsFinal = false;
    }

    MR::updateActorStateAndNextNerve(this, mStateBattle, GET_NERVE(BossKameckVs1, BossKameckVs1NrvPowerUpDemo));
}

void BossKameckVs1::exePowerUpDemo() {
    if (MR::isFirstStep(this)) {
        mBattleDemo->startDemoPowerUpVs1();
    }

    if (MR::updateActorState(this, mBattleDemo)) {
        setNerve(GET_NERVE(BossKameckVs1, BossKameckVs1NrvBattleLv3));
    }
}

void BossKameckVs1::exeBattleLv3() {
    if (MR::isFirstStep(this)) {
        mBossKameck->appearStarPieceToUp(16);
        mStateBattle->setMoveRail(mBossKameck->getMoveRail(2));
        mStateBattle->setBattlePattern(&::sPatternLv3);
        mStateBattle->mIsFinal = true;
    }

    MR::updateActorStateAndNextNerve(this, mStateBattle, GET_NERVE(BossKameckVs1, BossKameckVs1NrvEndDemo));
}

void BossKameckVs1::exeEndDemo() {
    if (MR::isFirstStep(this)) {
        mBattleDemo->startDemoDownVs1();
    }

    MR::updateActorState(this, mBattleDemo);
}

bool BossKameckVs1::isBattle() const {
    if (isNerve(GET_NERVE(BossKameckVs1, BossKameckVs1NrvBattleLv1)) || isNerve(GET_NERVE(BossKameckVs1, BossKameckVs1NrvBattleLv2)) ||
        isNerve(GET_NERVE(BossKameckVs1, BossKameckVs1NrvBattleLv3))) {
        return true;
    }

    return false;
}
