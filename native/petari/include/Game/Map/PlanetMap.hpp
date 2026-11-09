#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class LodCtrl;
class ModelObj;
class PartsModel;

class PlanetMap : public LiveActor {
public:
    PlanetMap(const char*, const char* = nullptr);

    virtual void init(const TableIter&) override;
    virtual void makeActorAppeared() override;
    virtual void makeActorDead() override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void control() override;
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

    virtual void init(const TableIter&) override;
};

class RailPlanetMap : public PlanetMap {
public:
    RailPlanetMap(const char*);

    virtual void init(const TableIter&) override;
};

struct PlanetMapClippingInfo {
    const char* mName;
    f32 mRadius;
    Vec mOffset;
};
