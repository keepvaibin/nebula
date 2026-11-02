#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FloaterFloatingForceTypeSpring;

class PhantomShipBoxFloater : public LiveActor {
public:
    PhantomShipBoxFloater(const char*);

    virtual ~PhantomShipBoxFloater();
    virtual void init(const JMapInfoIter&);
    virtual void control();

    f32 getCurrentSinkDepth() const;

    void exeWait();
    void exeMove();
    void exeMoveUnderWater();
    void playRippleSe();

    FloaterFloatingForceTypeSpring* mFloatingForce;
    const char* mObjectName;
    TVec3f mFloaterOffset;
    s32 mRippleSeCounter;
};
