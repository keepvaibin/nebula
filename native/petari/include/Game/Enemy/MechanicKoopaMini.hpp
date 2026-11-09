#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class WalkerStateBindStarPointer;

class MechanicKoopaMini : public LiveActor {
public:
    MechanicKoopaMini(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void makeActorAppeared() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void updateHitSensor(HitSensor*) override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPush(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void onAppearSwitchOn();
    void exeWait();
    void exeWalkOnRail();
    void exeTurn();
    void exeJumpStart();
    void exeJumpEnd();
    void exeFind();
    void exePursue();
    void exePreFireAttack();
    void exeFireAttack();
    void endFireAttack();
    void exeFireAttackEnd();
    void exeAttackHit();
    void exeSpinHit();
    void exeTrample();
    void exeHipDropped();
    void exeBindStarPointer();
    void endBindStarPointer();

    bool mIsAppearJumping;
    bool mIsForbidPause;
    AnimScaleController* mScaleController;
    WalkerStateBindStarPointer* mBindStarPointer;
};
