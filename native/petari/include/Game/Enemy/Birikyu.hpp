#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class Birikyu : public LiveActor {
public:
    Birikyu(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void appear() override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual f32 getHitRadius() const;
    virtual const char* getCenterJointName() const;

    void initFromJmpArgs(const TableIter&);
    void initRail(const TableIter&);
    void initCollision();
    void initShadow();
    bool tryStopPointing();
    void goMove();
    void exeMove();
    void exeMoveCircle();
    void exeWaitAtEdge();
    void exeAttack();
    void exeAttackWait();
    void exeStopPointing();

    const char* mObjName;
    Vec3f mClipCenter;
    Vec3f mCircleCenter;
    bool mHasShadow;
    bool mHasRail;
    Vec3f mCircleAxis;
    Vec3f mCircleFront;
    f32 mCircleAngle;
    f32 mSpeed;
};

class BirikyuWithFace : public Birikyu {
public:
    BirikyuWithFace(const char*);

    virtual void init(const TableIter&) override;
    virtual void calcAndSetBaseMtx() override;
    virtual f32 getHitRadius() const override;
    virtual const char* getCenterJointName() const override;

    bool mIsFaceReversed;
};
