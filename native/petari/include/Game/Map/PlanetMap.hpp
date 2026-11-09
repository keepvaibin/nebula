#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class LodCtrl;
class ModelObj;
class PartsModel;

class PlanetMap : public LiveActor {
public:
    PlanetMap(const char*, const char* = nullptr);

    virtual void init(const TableIter&);
    virtual void makeActorAppeared();
    virtual void makeActorDead();
    virtual void startClipped();
    virtual void endClipped();
    virtual void control();
    virtual f32 getFarClipDistance() const;

    void initClipping(const TableIter&);
    void initModel(const char*, const TableIter&);
    void initBloomModel(const char*);
    bool tryEmitMyEffect();
    bool tryDeleteMyEffect();

    const char* mModelName;
    Vec3f mClippingCenter;
    LodCtrl* mLODCtrl;
    ModelObj* mBloomModel;
};

class FurPlanetMap : public PlanetMap {
public:
    FurPlanetMap(const char*);

    virtual void init(const TableIter&);
};

class RailPlanetMap : public PlanetMap {
public:
    RailPlanetMap(const char*);

    virtual void init(const TableIter&);
};

struct PlanetMapClippingInfo {
    const char* mName;
    f32 mRadius;
    Vec mOffset;
};
