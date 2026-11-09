#pragma once

#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActor.hpp"
#include <nebula_geometry.hpp>

class DriftWood : public MapObjActor {
public:
    DriftWood(const char*);

    virtual void init(const TableIter&) override;
    virtual void connectToScene(const MapObjActorInitInfo&) override;
    virtual void calcAndSetBaseMtx() override;

    void attackSensor(HitSensor*, HitSensor*) override;
    bool tryVibrate();
    void exeWait();
    void exeVibrate();

    Vec3f mRailDirection;
    Vec3f mRailClipping;
    Vec3f mVibrateOffset;
    s32 mWaveSoundTimer;
};
