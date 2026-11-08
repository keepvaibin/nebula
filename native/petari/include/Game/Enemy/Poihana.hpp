#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ActorCameraInfo;
class AnimScaleController;
class ModelObj;
class WalkerStateBindStarPointer;

class Poihana : public LiveActor {
public:
    // Placement arg 3.
    enum Behavior {
        Behavior_Normal = 0,
        Behavior_Sleep = 1,
        // Takes the spot where it recovers as home and chases with no home leash.
        Behavior_NewHome = 2,
    };

    Poihana(const char* pName);

    virtual void init(const TableIter& rIter);
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
    void exePointerSwoon();
    void endPointerSwoon();

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
    bool tryPointerSwoon();
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
    Vec3f mRespawnPos;
    Vec3f mHomePos;
    Vec3f mFrontVec;
    s32 mBoundTimer;
    s32 mRandDir;
    s32 mBehavior;
    bool mCanDrown;
    ModelObj* mWaterColumn;
    bool mIsActive;
};
