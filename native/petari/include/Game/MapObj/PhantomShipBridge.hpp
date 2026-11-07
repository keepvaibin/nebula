#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class PhantomShipBridge : public LiveActor {
public:
    PhantomShipBridge(const char*);

    virtual void init(const TableIter&);
    virtual void calcAnim();

    void startMoveA();
    void startMoveB();
    void setStateMoveA();
    void exeMoveA();
    void exeMoveB();
    void exeWait();

    CollisionParts* _8C;
    s32 mIsNutShipBridge;
};
