#pragma once

#include "Game/Boss/KoopaSequencer.hpp"

class KoopaBattleMain;
class KoopaSubSequenceBattle;

class KoopaSequencerVs1 : public KoopaSequencer {
public:
    KoopaSequencerVs1();

    virtual ~KoopaSequencerVs1();
    virtual void init(Koopa* pKoopa, const JMapInfoIter& rIter);
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
#if !defined(NEBULA_RELEASE)
    virtual void testHit(HitSensor* pPlayer);
#endif

    void exeBattleStairs();
    void exeDemoBattleStart();
    void exeDemoJumpToPlanet();
    void exeBattle();
    void exeWaitDemoBattleEnd();
    void exeDemoBattleEnd();
    void exeWaitDemo();

private:
    KoopaSubSequenceBattle* mSubSequenceBattle;
    KoopaBattleMain* mBattleMain;
};
