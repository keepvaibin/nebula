#pragma once

#include "Game/Boss/KoopaSequencer.hpp"

class KoopaBattleMain;
class KoopaSubSequenceBattle;

class KoopaSequencerVs2 : public KoopaSequencer {
public:
    KoopaSequencerVs2();

    virtual void init(Koopa* pKoopa, const TableIter& rIter) override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

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
