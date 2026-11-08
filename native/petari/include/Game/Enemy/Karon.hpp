#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class WalkerStateBindStarPointer;
class TerritoryMover;

class Karon : public LiveActor {
public:
    Karon(const char* pName);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPush(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    bool isFallNextMove(bool isAlongVelocity);
    bool isFindPlayer() const;
    bool isNrvBroken() const;
    bool isNrvDisabled() const;
    void startBroken(const char* pActionName);
    void updateBroken(bool isBindBeforeKill);
    void keepVelocityAlongGravity();
    void appearStarPieceBurst();

    void exeFixWait();
    void exeWait();
    void exeWalk();
    void exeTurn();
    void exeWalkOnRail();
    void exeSearch();
    void exePursue();
    void endPursue();
    void exeBroken();
    void exeStepBroken();
    void exeRecover();
    void exeHitReaction();
    void exeDeath();
    void exeSinkDown();
    void exeBindStarPointer();
    void endBindStarPointer();

    TerritoryMover* mTerritoryMover;
    bool mStopSwAnim;
    AnimScaleController* mScaleController;
    WalkerStateBindStarPointer* mStateStarPointer;
};
