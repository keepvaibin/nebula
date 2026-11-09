#pragma once

#include "Game/Enemy/StinkBugBase.hpp"

class CollisionParts;

class StinkBugParent : public StinkBugBase {
public:
    StinkBugParent(const char*);

    virtual void init(const TableIter&) override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void exeWait();
    void exeSearch();
    void exeDashSign();
    void exeDashSignEnd();
    void exeDash();
    void exeDashEnd();
    void exeBack();
    void exeHipDropDown();
    void exeAttack();
    void exeSpinReaction();
    void exePanic();
    void exeRecover();
    void exeShakeStart();
    void exeShake();
    void exeDemoPrepare();
    void exeDemoChildDown();
    void exeDemoAnger();
    void jumpBackPlayerFromChild() const;
    bool isPlayerInParentTerritory() const;
    bool isShakeChance() const;
    bool isHitChild() const;
    bool isNrvEnableStarPieceAttack() const;
    bool isNrvMoving() const;
    bool isNrvDemo() const;

    CollisionParts* mParentOnlyParts;  // The body without the child; valid once the child is gone.
    bool mIsChildAlive;
};
