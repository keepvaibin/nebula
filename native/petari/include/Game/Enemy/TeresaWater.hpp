#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class WalkerStateBindStarPointer;

class TeresaWater : public LiveActor {
public:
    TeresaWater(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;

    void exeMove();
    void exeHit();
    void exeStun();
    void exePointerSwoon();
    void endPointerSwoon();
    bool tryPointerSwoon();

    AnimScaleController* mScaleCtrl;
    WalkerStateBindStarPointer* mBindStarPointer;
    Vec3f mFront;
    Vec3f mFarPos;
    Vec3f mInitPos;
    Vec3f* mTarget;
};
