#include "Game/Boss/BossKameckVs2.hpp"
#include "Game/Boss/BossKameck.hpp"
#include "Game/Boss/BossKameckBarrier.hpp"
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
    static BossKameckBattlePattern sPatternLv3 = BossKameckBattlePattern(sBeamPatternLv3, true);
};  // namespace

namespace NrvBossKameckVs2 {
    NEW_NERVE(BossKameckVs2NrvOpeningDemo, BossKameckVs2, OpeningDemo);
    NEW_NERVE(BossKameckVs2NrvBattleLv1, BossKameckVs2, BattleLv1);
    NEW_NERVE(BossKameckVs2NrvBattleLv2, BossKameckVs2, BattleLv2);
    NEW_NERVE(BossKameckVs2NrvPowerUpDemo, BossKameckVs2, PowerUpDemo);
    NEW_NERVE(BossKameckVs2NrvBattleLv3, BossKameckVs2, BattleLv3);
    NEW_NERVE(BossKameckVs2NrvBattleLv4, BossKameckVs2, BattleLv4);
    NEW_NERVE(BossKameckVs2NrvEndDemo, BossKameckVs2, EndDemo);
};  // namespace NrvBossKameckVs2

BossKameckVs2::BossKameckVs2() : BossKameckSequencer("ボスカメックVs2"), mStateBattle(), mBarrier() {
}

void BossKameckVs2::init(BossKameck* pBoss, const TableIter& rIter) {
    BossKameckSequencer::init(pBoss, rIter);

    mStateBattle = new BossKameckStateBattle(pBoss);
    mStateBattle->init();
    mStateBattle->mIsVs2 = true;

    mBarrier = new BossKameckBarrier("カメックバリア", "BossKameckBarrier");
    mBarrier->init(rIter);

    initNerve(GET_NERVE(BossKameckVs2, BossKameckVs2NrvOpeningDemo));
    MR::declareStarPiece(mBossKameck, 24);
}

void BossKameckVs2::start() {
    setNerve(GET_NERVE(BossKameckVs2, BossKameckVs2NrvOpeningDemo));
}

void BossKameckVs2::attackSensor(HitSensor* pSender, HitSensor* pReceiver) {
    if (isBattle()) {
        mStateBattle->attackSensor(pSender, pReceiver);
    }
}

bool BossKameckVs2::receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) {
    if (isBattle()) {
        return mStateBattle->receiveMsgPlayerAttack(msg, pSender, pReceiver);
    }

    return false;
}

void BossKameckVs2::exeOpeningDemo() {
    if (MR::isFirstStep(this)) {
        mBattleDemo->startDemoAppearVs2();
    }

    if (MR::updateActorState(this, mBattleDemo)) {
        setNerve(GET_NERVE(BossKameckVs2, BossKameckVs2NrvBattleLv1));
        mBarrier->appear();
    }
}

void BossKameckVs2::exeBattleLv1() {
    if (MR::isFirstStep(this)) {
        mStateBattle->setMoveRail(mBossKameck->getMoveRail(0));
        mStateBattle->setBattlePattern(&::sPatternLv1);
        mStateBattle->mIsFinal = false;
        mStateBattle->mMoveNumMax = 3;
    }

    MR::updateActorStateAndNextNerve(this, mStateBattle, GET_NERVE(BossKameckVs2, BossKameckVs2NrvBattleLv2));
}

void BossKameckVs2::exeBattleLv2() {
    if (MR::isFirstStep(this)) {
        mBossKameck->appearStarPieceToPlayer(8);
        mStateBattle->setMoveRail(mBossKameck->getMoveRail(1));
        mStateBattle->setBattlePattern(&::sPatternLv2);
        mStateBattle->mIsFinal = false;
        mStateBattle->mMoveNumMax = 3;
    }

    MR::updateActorStateAndNextNerve(this, mStateBattle, GET_NERVE(BossKameckVs2, BossKameckVs2NrvPowerUpDemo));
}

void BossKameckVs2::exePowerUpDemo() {
    if (MR::isFirstStep(this)) {
        mBattleDemo->startDemoPowerUpVs2();
    }

    if (MR::updateActorState(this, mBattleDemo)) {
        setNerve(GET_NERVE(BossKameckVs2, BossKameckVs2NrvBattleLv3));
    }
}

void BossKameckVs2::exeBattleLv3() {
    if (MR::isFirstStep(this)) {
        mStateBattle->setMoveRail(mBossKameck->getMoveRail(2));
        mStateBattle->setBattlePattern(&::sPatternLv3);
        mStateBattle->mIsFinal = false;
        mStateBattle->mMoveNumMax = 3;
    }

    MR::updateActorStateAndNextNerve(this, mStateBattle, GET_NERVE(BossKameckVs2, BossKameckVs2NrvBattleLv4));
}

void BossKameckVs2::exeBattleLv4() {
    if (MR::isFirstStep(this)) {
        mBossKameck->appearStarPieceToPlayer(16);
        mStateBattle->setMoveRail(mBossKameck->getMoveRail(3));
        mStateBattle->setBattlePattern(&::sPatternLv3);
        mStateBattle->mIsFinal = true;
        mStateBattle->mMoveNumMax = 4;
    }

    MR::updateActorStateAndNextNerve(this, mStateBattle, GET_NERVE(BossKameckVs2, BossKameckVs2NrvEndDemo));
}

void BossKameckVs2::exeEndDemo() {
    if (MR::isFirstStep(this)) {
        mBattleDemo->startDemoDownVs2();
        mBarrier->kill();
    }

    MR::updateActorState(this, mBattleDemo);
}

bool BossKameckVs2::isBattle() const {
    if (isNerve(GET_NERVE(BossKameckVs2, BossKameckVs2NrvBattleLv1)) || isNerve(GET_NERVE(BossKameckVs2, BossKameckVs2NrvBattleLv2)) ||
        isNerve(GET_NERVE(BossKameckVs2, BossKameckVs2NrvBattleLv3)) || isNerve(GET_NERVE(BossKameckVs2, BossKameckVs2NrvBattleLv4))) {
        return true;
    }

    return false;
}
