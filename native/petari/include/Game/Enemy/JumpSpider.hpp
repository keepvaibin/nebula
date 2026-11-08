#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SpinHitController;
class AnimScaleController;

class JumpSpider : public LiveActor {
public:
    JumpSpider(const char*);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void kill();
    virtual void startClipped();
    virtual void endClipped();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

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
