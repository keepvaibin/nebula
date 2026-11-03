#pragma once

#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActor.hpp"
#include <JSystem/JGeometry/TVec.hpp>

class DriftWood : public MapObjActor {
public:
    DriftWood(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void connectToScene(const MapObjActorInitInfo&);
    virtual void calcAndSetBaseMtx();

    void attackSensor(HitSensor*, HitSensor*);
    bool tryVibrate();
    void exeWait();
    void exeVibrate();

    TVec3f mRailDirection;
    TVec3f mRailClipping;
    TVec3f mVibrateOffset;
    s32 mWaveSoundTimer;
};
