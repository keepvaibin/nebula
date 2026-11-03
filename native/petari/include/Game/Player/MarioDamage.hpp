#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioDamage : public MarioState {
public:
    MarioDamage(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();
    virtual bool notice();

    void setVec(const TVec3f&);
    void setVecSize(f32, f32);

    void stopHead(const TVec3f&);

    inline void setStrings(const char* pChar1, const char* pChar2) {
        _28 = pChar1;
        _2C = pChar2;
    }

    u8 _11;
    bool _12;
    u16 _14;
    u16 _16;
    u16 _18;
    TVec3f _1C;
    const char* _28;
    const char* _2C;
};
