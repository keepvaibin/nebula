#pragma once

#include "Game/System/NerveExecutor.hpp"

class HitSensor;
class TableIter;
class Koopa;
class KoopaBattleStairsBase;
class KoopaDemoJumpToPlanet;
class KoopaDemoPowerUp;

class KoopaSequencer : public NerveExecutor {
public:
    KoopaSequencer(const char* pName, s32 vs);

    virtual void init(Koopa* pKoopa, const TableIter& rIter);
    virtual void update();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);


    void startBattleLevel2();
    void startBattleLevel3();
    void updateFallVoice();
    void updateDemoDown(const char* pPlayerPosName);

    Koopa* mKoopa;
    KoopaDemoPowerUp* mPowerUp;
    KoopaBattleStairsBase* mBattleStairs;
    KoopaDemoJumpToPlanet* mJumpToPlanet;
    u32 mLv;
    u32 mVs;
    bool mIsAngry;
};
