#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class PhantomShipBridge : public LiveActor {
public:
    PhantomShipBridge(const char*);

    virtual void init(const TableIter&) override;
    virtual void calcAnim() override;

    void startMoveA();
    void startMoveB();
    void setStateMoveA();
    void exeMoveA();
    void exeMoveB();
    void exeWait();

    CollisionParts* mMoveCollision;
    s32 mIsNutShipBridge;
};
