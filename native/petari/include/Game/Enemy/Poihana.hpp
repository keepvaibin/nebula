#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ActorCameraInfo;
class AnimScaleController;
class ModelObj;
class WalkerStateBindStarPointer;

class Poihana : public LiveActor {
public:
    Poihana(const char* pName);

    virtual ~Poihana();
    virtual void init(const JMapInfoIter& rIter);
    virtual void initAfterPlacement();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeNonActive();
    void endNonActive();
    void exeWait();
    void exeWalkAround();
    void exeSleepStart();
    void exeSleep();
    void exeGetUp();
    void exeSearch();
    void exeChasePlayer();
    void exeShootUpCharge();
    void exeShootUp();
    void endShootUp();
    void exeGoBack();
    void exeShock();
    void exeSwoonLand();
    void exeSwoon();
    void exeRecover();
    void exeShake();
    void exeDrown();
    void exeHide();
    void exeAppear();
    void exeDPDSwoon();
    void endDPDSwoon();

    bool tryToStartBind(HitSensor* pSender);
    void updateBindActorMtx();
    void endBind();
    void startBound();
    void calcBound();
    void contactMario(HitSensor* pSender, HitSensor* pReceiver);
    void controlVelocity();
    void calcMyGravity();
    bool tryNonActive();
    bool tryDrown();
    bool tryDPDSwoon();
    bool tryShock();
    bool tryHipDropShock();
    bool isNerveTypeWalkOrWait() const;
    bool isNeedForBackHome() const;
    bool isNeedForGetUp() const;
    bool isBackAttack(HitSensor* pMySensor) const;

    AnimScaleController* mAnimScaleCtrl;
    WalkerStateBindStarPointer* mBindStarPointer;
    ActorCameraInfo* mCamInfo;
    LiveActor* mBindedActor;
    f32 mLaunchIntensity;
    TVec3f mRespawnPos;
    TVec3f mHomePos;
    TVec3f mFrontVec;
    s32 mBoundTimer;
    s32 mRandDir;
    s32 mBehavior;
    bool mCanDrown;
    ModelObj* mWaterColumn;
    TVec3f _D8;
    bool mIsActive;
    u8 _E5;
};
