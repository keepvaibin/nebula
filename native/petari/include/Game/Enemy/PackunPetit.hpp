#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class ModelObj;
class WalkerStateBindStarPointer;

class PackunPetit : public LiveActor {
public:
    PackunPetit(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void exeNonActive();
    void endNonActive();
    void exeWait();
    void exeFind();
    void exeThreat();
    void exeTurn();
    void exeAttackStart();
    void exeAttack();
    void exeAttackBack();
    void exeHitWaitForAttack();
    void exeHit();
    void exeTrampleDown();
    void exePunchDown();
    void exeSwoonStart();
    void exeSwoon();
    void exeSwoonToThreat();
    void exePointerSwoon();
    void endPointerSwoon();

    void initStarPointer(const char*);
    void initBlowModel();
    void punchDown(HitSensor*, HitSensor*);
    void selectNrvWait();
    bool tryNonActive();
    bool tryTurn();
    bool tryPointerSwoon();

    AnimScaleController* mScaleController;
    WalkerStateBindStarPointer* mStarPointerState;
    Vec3f mFront;
    ModelObj* mBlownModel;
    bool mDontTurn;
};
