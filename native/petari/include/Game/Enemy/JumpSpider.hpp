#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SpinHitController;
class AnimScaleController;

class JumpSpider : public LiveActor {
public:
    JumpSpider(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void kill() override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void exeWait();
    void exePreJump();
    void exeJump();
    void exeJumpEnd();
    void exeHitToPlayer();
    void exeStampFall();
    void endStampFall();
    void exeStampDeath();
    void endStampDeath();
    void exeHitBlow();
    void endHitBlow();
    void exePointerAimed();
    void endPointerAimed();
    void exeNoPowerStart();
    void exeNoPowerLand();
    void exeNoPowerWait();
    void exeNoPowerEnd();

    SpinHitController* mSpinHitController;
    AnimScaleController* mScaleController;
    Vec3f mFront;
    Vec3f mJumpPos;
};
