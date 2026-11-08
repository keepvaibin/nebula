#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class PlanetMapWithoutHighModel : public MapObjActor {
public:
    PlanetMapWithoutHighModel(const char*);

    virtual void init(const TableIter&);
    virtual void connectToScene(const MapObjActorInitInfo&);
};
