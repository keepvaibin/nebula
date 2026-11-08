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

    virtual void init(const TableIter&);
    virtual void kill();
    virtual void endClipped();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    void exeHide();
    void exeHideWait();
    void exeAppear();
    bool isPlayerExistUp() const;
    bool isPlayerTooNear(f32 distanceToPlayer) const;
    bool tryStartSwoon(bool isHipDropFelt);
    void exeSearch();
    void exeThrow();
    void endThrow();
    void exeSwoonStart();
    void exeSwoon();
    void exeHipDropReaction();
    void exeSwoonEnd();
    void exeStampDeath();
    void exeHitBlow();
    bool isNearPlayerHipDrop() const;
    bool isHiddenOrDown() const;
    bool isSwoon() const;
    void startHitBlow(HitSensor* pAttacker, HitSensor* pVictim);

    AnimScaleController* mAnimScaleController;
    FixedPosition* mStoneHoldPos;
    MoguStone* mStone;
    ModelObj* mHole;
    Vec3f mSight;
    Vec3f mUp;
    bool mThrowOnGravity;
    bool mIsCannonFleet;
};
