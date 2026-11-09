#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class WalkerStateBindStarPointer;
class TerritoryMover;

class Karon : public LiveActor {
public:
    Karon(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPush(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

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
