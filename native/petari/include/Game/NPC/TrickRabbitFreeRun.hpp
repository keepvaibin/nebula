#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FootPrint;
class RabbitStateCaught;
class RabbitStateWaitStart;
class SpotMarkLight;
class TalkMessageCtrl;
class WalkerStateBlowDamage;
class WalkerStateRunaway;

class TrickRabbitFreeRun : public LiveActor {
public:
    TrickRabbitFreeRun(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void initAfterPlacement() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void initSensor();
    void initState();
    bool receiveMsgBlowDamage(HitSensor* pSender, HitSensor* pReceiver);
    bool requestCaught();
    bool isEnableCaught() const;
    bool isEnableBlowDamage() const;
    void startJumpSound();

    void exeWaitStart();
    void exeTryDemo();
    void exeRunawayStart();
    void exeRunaway();
    void exeBlowDamage();
    void exeCaught();
    void exePowerStarDemo();
    void exeGiveUp();

    Quat4f mRotateQuat;
    Vec3f mFrontVec;
    RabbitStateWaitStart* mStateWaitStart;
    WalkerStateRunaway* mStateRunaway;
    WalkerStateBlowDamage* mStateBlowDamage;
    RabbitStateCaught* mStateCaught;
    TalkMessageCtrl* mMsgCtrl;
    FootPrint* mFootPrint;
    SpotMarkLight* mSpotMarkLight;
};
