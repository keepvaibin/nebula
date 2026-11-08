#pragma once

#include "Game/Boss/KoopaSequencer.hpp"

class KoopaBattleMain;
class KoopaSubSequenceBattle;

class KoopaSequencerVs2 : public KoopaSequencer {
public:
    KoopaSequencerVs2();

    virtual void init(Koopa* pKoopa, const TableIter& rIter);
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
#if defined(NEBULA_DEV)
    virtual void testHit(HitSensor* pPlayer);
#endif

    void exeWaitDemoBattleStart();
    void exeDemoBattleStart();
    void exeDemoJumpToPlanet();
    void exeBattle();
    void exeWaitDemoDown();
    void exeDemoDown();
    void exeWaitDemo();
    bool isBattleActive() const;

    KoopaSubSequenceBattle* mSubSequenceBattle;
    KoopaBattleMain* mBattleMain;
};
