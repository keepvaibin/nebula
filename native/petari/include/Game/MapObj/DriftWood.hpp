#pragma once

#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActor.hpp"
#include <nebula_geometry.hpp>

class DriftWood : public MapObjActor {
public:
    DriftWood(const char*);

    virtual void init(const TableIter&);
    virtual void connectToScene(const MapObjActorInitInfo&);
    virtual void calcAndSetBaseMtx();

    void attackSensor(HitSensor*, HitSensor*);
    bool tryVibrate();
    void exeWait();
    void exeVibrate();

    Vec3f mRailDirection;
    Vec3f mRailClipping;
    Vec3f mVibrateOffset;
    s32 mWaveSoundTimer;
};
