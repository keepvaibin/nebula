#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class FixedPosition;
class HitSensor;
class MoguStone;
class ModelObj;

class Mogu : public LiveActor {
public:
    Mogu(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void kill();
    virtual void endClipped();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    void exeWait();
    void exeHide();
    void exeHideWait();
    void exeAppear();
    bool isPlayerExistUp();
    void exeSearch();
    void exeThrow();
    void endThrow();
    void tearDownThrow();
    void exeSwoonStart();
    void exeSwoon();
    void exeHipDropReaction();
    void exeSwoonEnd();
    void exeStampDeath();
    void exeHitBlow();
    bool isNearPlayerHipDrop();
    bool tryPunchHitted(HitSensor*, HitSensor*, bool);

    AnimScaleController* mAnimScaleController;
    FixedPosition* _90;
    MoguStone* mStone;
    ModelObj* mHole;
    TVec3f mSight;
    TVec3f _A8;
    bool _B4;
    bool mIsCannonFleet;
};
