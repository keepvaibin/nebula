#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class WalkerStateBindStarPointer;

class TeresaWater : public LiveActor {
public:
    TeresaWater(const char* pName);

    virtual void init(const TableIter&);
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

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
