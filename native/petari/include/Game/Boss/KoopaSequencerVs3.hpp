#pragma once

#include "Game/Boss/KoopaSequencer.hpp"

class ActorStateBaseInterface;
class KoopaBattleMain;
class KoopaBattleVs3Lv1;
class KoopaBattleVs3Lv2;
class KoopaDemoFallToPlanetVs3Lv2;
class KoopaDemoFallToPlanetVs3Lv3;

class KoopaSequencerVs3 : public KoopaSequencer {
public:
    KoopaSequencerVs3();

    virtual void init(Koopa* pKoopa, const TableIter& rIter) override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void exeWaitPlayer();
    void exeBattleStairs();
    void exeDemoBattleStart();
    void exeDemoJumpToPlanet();
    void exeBattleVs3Lv1();
    void exeFallToPlanetLv2();
    void exeBattleVs3Lv2();
    void exeFallToPlanetLv3();
    void exeBattleVs3Lv3();
    void exeDemoPowerUpFinal();
    void exeBattleFinal();
    void exeWaitDemoDown();
    void exeDemoDown();
    void exeDead();
    void exeWaitDemo();
    void initDemoCasts(const TableIter& rIter, const char* pDemoName, const char* pPartName);
    void updateFallToPlanet(ActorStateBaseInterface* pDemo, const Nerve* pNextNerve);
    bool isNerveBattleMain() const;

    KoopaBattleVs3Lv1* mBattleLv1;
    KoopaBattleVs3Lv2* mBattleLv2;
    KoopaBattleMain* mBattleMain;
    KoopaDemoFallToPlanetVs3Lv2* mDemoFallToPlanetLv2;
    KoopaDemoFallToPlanetVs3Lv3* mDemoFallToPlanetLv3;
};
