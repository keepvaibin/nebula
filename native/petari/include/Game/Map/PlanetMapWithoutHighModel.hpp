#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class PlanetMapWithoutHighModel : public MapObjActor {
public:
    PlanetMapWithoutHighModel(const char*);

    virtual void init(const TableIter&) override;
    virtual void connectToScene(const MapObjActorInitInfo&) override;
};
