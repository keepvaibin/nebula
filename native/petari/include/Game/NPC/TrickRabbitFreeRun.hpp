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
    /// @brief Creates a new `TrickRabbitFreeRun`.
    /// @param pName A pointer to the null-terminated name of the object.
    TrickRabbitFreeRun(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void initAfterPlacement();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

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

    TQuat4f mRotateQuat;
    TVec3f mFrontVec;
    RabbitStateWaitStart* mStateWaitStart;
    WalkerStateRunaway* mStateRunaway;
    WalkerStateBlowDamage* mStateBlowDamage;
    RabbitStateCaught* mStateCaught;
    TalkMessageCtrl* mMsgCtrl;
    FootPrint* mFootPrint;
    SpotMarkLight* mSpotMarkLight;
};
