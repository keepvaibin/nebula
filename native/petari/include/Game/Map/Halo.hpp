#pragma once

#include "Game/MapObj/MapObjActor.hpp"

struct HaloParam {
    const char* mName;
    f32 mClippingRadius;
};

class Halo : public MapObjActor {
public:
    Halo(const char*);

    virtual void init(const TableIter&);
    virtual void appear();
    virtual void connectToScene(const MapObjActorInitInfo&);
    virtual f32 getDistance() const;

    bool isDistanceAppear() const;
    bool isDistanceDisappear() const;
    void exeAppear();
    void exeDisappear();

    f32 mDistance;
};

class PowerStarHalo : public Halo {
public:
    PowerStarHalo(const char*);

    virtual void init(const TableIter&);
    virtual void appear();
    virtual f32 getDistance() const;

    void exeWaitScenarioOpeningEnd();

    s32 mAppearDuringOpeningCam;
};
