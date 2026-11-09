#pragma once

#include "Game/MapObj/MapObjActor.hpp"
#include <nebula_geometry.hpp>

class SandCapsulePressGround : public MapObjActor {
public:
    SandCapsulePressGround(const char*);

    virtual void init(const TableIter&) override;

    virtual void initCaseUseSwitchA(const MapObjActorInitInfo&) override {
    }

    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&) override {
    }

    virtual void initCaseNoUseSwitchA(const MapObjActorInitInfo&) override {
    }

    virtual void initCaseNoUseSwitchB(const MapObjActorInitInfo&) override {
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
