#pragma once

#include "Game/MapObj/MapObjActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class SandCapsulePressGround : public MapObjActor {
public:
    SandCapsulePressGround(const char*);

    virtual void init(const JMapInfoIter&);

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

    TVec3f _C4;
    TVec3f _D0;
    TPos3f _DC;
};
