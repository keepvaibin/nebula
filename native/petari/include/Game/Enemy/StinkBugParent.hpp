#pragma once

#include "Game/Enemy/StinkBugBase.hpp"

class CollisionParts;

namespace {
    struct Param {
        f32 mDashVelocity;
        f32 mDashDistance;
        s32 mPanicStep;
    };
};  // namespace

class StinkBugParent : public StinkBugBase {
public:
    StinkBugParent(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

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
    bool isShakeChance() const;
    bool isHitChild() const;
    bool isNrvEnableStarPieceAttack() const;

    inline bool isNrvMoving() const;
    inline bool isNrvDemo() const;

    CollisionParts* mParent;
    bool mIsChildAlive;
};
