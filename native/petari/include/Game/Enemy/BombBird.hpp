#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Array.hpp"

class AnimScaleController;
class BombBirdBomb;
class FixedPosition;
class SpinHitController;
class WalkerStateBindStarPointer;

struct BombBirdParam {
    f32 mMoveAccel;
    f32 mGravityAccel;
    f32 mFriction;
    f32 mTurnDegree;
};

class BombBird : public LiveActor {
public:
    BombBird(const char*);

    virtual void init(const TableIter&) override;
    virtual void makeActorDead() override;
    virtual void startClipped() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void exeFlyOnRail();
    void exeFlyOnRailSearch();
    void doFly();
    void addVelocityToUpFromGround(f32);
    void killAllBomb();
    void exeFlyWithAttack();
    void exeBlow();
    void exeEscape();
    void exeDamage();
    void exeFall();
    void exeSwoonStart();
    void exeSwoon();
    void exeSwoonEnd();
    void exeTrample();
    void exeSinkDown();
    void exeBindStarPointer();
    void endBindStarPointer();

    MR::InlineArray< BombBirdBomb*, 3 > mBombs;
    BombBirdBomb* mHeldBomb;
    Vec3f mFront;
    bool mSearchPlayer;
    bool mHoldBomb;
    FixedPosition* mFixedPos;
    SpinHitController* mSpinHitController;
    AnimScaleController* mScaleController;
    WalkerStateBindStarPointer* mStarPointer;
};
