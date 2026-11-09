#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class CocoNutBall : public LiveActor {
public:
    CocoNutBall(const char* pName = "ヤシの実弾");

    virtual void init(const TableIter& rIter) override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void appearAndThrow(const Vec3f& rAppearPos, f32 angleDegree);
    void hitBackToPlayer();
    void demoBreak(const Vec3f& rPos);
    HitSensor* isBindedAny() const;
    bool isValidReceivePunch() const;
    bool isNerveThrowToOrFreeze() const;
    bool isSensorBody(HitSensor* pSensor) const;
    void calcHitBackVelocityAndGravity();
    bool isHitBackRight() const;
    bool isHitBackFront() const;
    void calcHitBackDstPos(Vec3f* pOut, bool isRight, bool isFront);
    bool tryToKill(bool alwaysKill);
    // angleDegree turns the aim about mUp.
    void setVelocityToPlayer(f32 speed, f32 angleDegree);
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
