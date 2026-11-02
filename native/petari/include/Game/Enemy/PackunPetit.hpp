#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class ModelObj;
class WalkerStateBindStarPointer;

class PackunPetit : public LiveActor {
public:
    PackunPetit(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

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
    void exeDPDSwoon();
    void endDPDSwoon();

    void initStarPointer(const char*);
    void initBlowModel();
    void punchDown(HitSensor*, HitSensor*);
    void selectNrvWait();
    bool tryNonActive();
    bool tryTurn();
    bool tryDPDSwoon();

    AnimScaleController* mScaleController;
    WalkerStateBindStarPointer* mStarPointerState;
    TVec3f _94;
    ModelObj* mBlownModel;
    bool mDontTurn;
};
