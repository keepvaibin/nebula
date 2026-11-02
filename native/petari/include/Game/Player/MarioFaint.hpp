#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioFaint : public MarioState {
public:
    MarioFaint(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();

    void setVec(const TVec3f&);

    u16 mTimer;
    u16 _14;
    u16 _16;
    TVec3f _18;
    bool mNoDamage;
    bool mTookDamage;
};
