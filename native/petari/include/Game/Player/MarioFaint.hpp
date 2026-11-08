#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioFaint : public MarioState {
public:
    MarioFaint(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();

    void setVec(const Vec3f&);

    u16 mTimer;
    u16 mAfterTimer;  // frames after it: no new damage
    u16 mStep;  // update's step
    Vec3f mVelocity;  // the slide's velocity
    bool mNoDamage;
    bool mTookDamage;
};
