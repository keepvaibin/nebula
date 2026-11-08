#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class WalkerStateBindStarPointer;
class SpinHitController;

class SamboHead : public LiveActor {
public:
    SamboHead(const char*);

    virtual void init(const TableIter&);
    virtual void appear();
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPush(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void exeWaitUnderGround();
    void exeHide();
    void exeAppear();
    void exeWait();
    void exePrePursue();
    void exePursue();
    void exePursueEnd();
    void exeHitToPlayer();
    void exeStarPieceHit();
    void exeStampFall();
    void endStampFall();
    void exeStampDeath();
    void endStampDeath();
    void exeHitBlow();
    void endHitBlow();
    void exeBuryDeath();
    void exeBindStarPointer();
    void endBindStarPointer();
    void exeBindStarPointerEnd();
    // Returns the angle turned this step.
    f32 turnToPlayer();
    void updateFall();
    void updateBounceFall();
    bool isPursuing() const;
    bool isInactive() const;

    SpinHitController* mSpinCtrl;
    AnimScaleController* mScaleController;
    WalkerStateBindStarPointer* mBindStarPointer;
    Vec3f mFront;
    Vec3f mInitPos;
    bool mIsPursueAlways;
};
