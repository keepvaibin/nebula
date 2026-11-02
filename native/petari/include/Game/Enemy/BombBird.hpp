#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Array.hpp"

class AnimScaleController;
class BombBirdBomb;
class FixedPosition;
class SpinHitController;
class WalkerStateBindStarPointer;

struct BombBirdParam {
    f32 _0;
    f32 _4;
    f32 _8;
    f32 _C;
};

class BombBird : public LiveActor {
public:
    BombBird(const char*);

    virtual ~BombBird() override;
    virtual void init(const JMapInfoIter&) override;
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

    MR::FixedArray< BombBirdBomb*, 3 > mBombs;
    BombBirdBomb* mHeldBomb;
    TVec3f mFront;
    bool mSearchPlayer;
    bool mHoldBomb;
    FixedPosition* mFixedPos;
    SpinHitController* mSpinHitController;
    AnimScaleController* mScaleController;
    WalkerStateBindStarPointer* mStarPointer;
};
