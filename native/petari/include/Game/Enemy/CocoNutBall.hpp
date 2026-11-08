#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class CocoNutBall : public LiveActor {
public:
    CocoNutBall(const char* pName = "ヤシの実弾");

    virtual void init(const TableIter& rIter);
    virtual void appear();
    virtual void kill();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void initSensors(const char*, const char*);
    void appearAndThrow(const Vec3f&, f32);
    void hitBackToPlayer();
    void demoBreak(const Vec3f&);
    HitSensor* isBindedAny() const;
    bool isValidReceivePunch() const;
    bool isNerveTrowToOrFreeze() const;  // inline used several places
    bool isSensorBody(HitSensor*) const;
    void calcHitBackVelocitAndGravity();
    bool isHitBackRight() const;
    bool isHitBackFront() const;
    void calcHitBackDstPos(Vec3f*, bool, bool);
    bool tryToKill(bool);
    void setVelocityToPlayer(f32, f32);
    void freeze();
    bool isFreezable();
    void processApproachToPlayer();
    void exeThrow();
    void exeHitBackToHost();
    void exeHitBackToPlayer();
    void exeRebound();
    void exeFreeze();
    void exeFreezeRelease();

    LiveActor* mHost;
    Vec3f mHitBackGravity;
    s32 mRallyNum;
    s32 mFreezeStep;
    Vec3f mFreezePos;
    Vec3f mFreezeVelocity;
    bool mIsThrowRight;
    bool mIsHeightLimited;
    f32 mHeightMax;
    f32 mHeightMin;
    Vec3f mUp;
    f32 mKillDistance;
    bool mIsRallyBgm;
};
