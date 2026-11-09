#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class AnimScaleParam;

class SnowMan : public LiveActor {
public:
    SnowMan(const char*);

    virtual void init(const TableIter&) override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;

    void damageHead();
    void damageBody();
    void exeMeltHead();
    void exeMeltBody();
    void exeDownHead();
    void exeDownBody();
    void exeWait();
    void exeWaitBody();
    void exeWaitHead();

    CollisionParts* mHeadCollisionParts;
    CollisionParts* mBodyCollisionParts;
    AnimScaleController* mAnimScaleCtrl;
    AnimScaleParam* mAnimScaleParam;
};
