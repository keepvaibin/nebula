#pragma once

#include "Game/MapObj/MapObjActor.hpp"
#include <nebula_geometry.hpp>

class SandCapsulePressGround : public MapObjActor {
public:
    SandCapsulePressGround(const char*);

    virtual void init(const TableIter&);

    virtual void initCaseUseSwitchA(const MapObjActorInitInfo&) {
    }

    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&) {
    }

    virtual void initCaseNoUseSwitchA(const MapObjActorInitInfo&) {
    }

    virtual void initCaseNoUseSwitchB(const MapObjActorInitInfo&) {
    }

    void exeWait();
    void exeMoveStart();
    void exeMove();
    void exeMoveEnd();
    void exeDisappearDemo();
    void exeStop();
    void calcMoveVelocity();
    bool tryStop();

    Vec3f mLowerPos;
    Vec3f mUpperPos;
    Pos3f mEffectMtx;
};
