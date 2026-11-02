#pragma once

#include "Game/Player/MarioState.hpp"

class MarioActor;

class MarioTeresa : public MarioState {
public:
    MarioTeresa(MarioActor*);

    virtual bool start();
    virtual bool close();
    virtual bool update();
    virtual bool notice() {
        return true;
    }

    virtual bool keep() {
        return update();
    }

    void updateDropFlag();
    void resetTeresaMode();
    bool isTeresaAccel() const;
    f32 checkHeight();
    void checkAccel();
    void checkWind();
    void checkGroundReflect();
    void checkWallCeilReflect();
    void procNoControl();
    void procNearGroundControl();
    void procAirControl();
    void procDrop();
    void procControl();
    void addTeresaVerticalVelocity(f32);
    void addTeresaHorizontalVelocity(const TVec3f&);
    void doTeresaReflection(const TVec3f&, bool);

    TVec3f _14;
    f32 _20;
    f32 _24;
    TVec3f _28;
    TVec3f _34;
    u16 _40;
    u16 _42;
    u16 _44;
    u16 _46;
    u16 _48;
    f32 _4C;
    f32 _50;
    f32 _54;
    u8 _58;
    u8 _59;
};
