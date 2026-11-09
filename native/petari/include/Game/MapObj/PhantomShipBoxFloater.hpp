#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FloaterFloatingForceTypeSpring;

class PhantomShipBoxFloater : public LiveActor {
public:
    PhantomShipBoxFloater(const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;

    f32 getCurrentSinkDepth() const;

    void exeWait();
    void exeMove();
    void exeMoveUnderWater();
    void playRippleSe();

    FloaterFloatingForceTypeSpring* mFloatingForce;
    const char* mObjectName;
    Vec3f mFloaterOffset;
    s32 mRippleSeCounter;
    // The box goes under past mSinkDepthUnder while ridden and comes back above mSinkDepthOver (by box size).
    f32 mSinkDepthUnder;
    f32 mSinkDepthOver;
};
