#pragma once

#include "Game/NameObj/NameObj.hpp"

class PlanetMap;
class ModelObj;

class OceanHomeMapCtrl : public NameObj {
public:
    OceanHomeMapCtrl();

    virtual void init(const TableIter&) override;
    virtual void movement() override;

    void entryMapRing(PlanetMap*);

    PlanetMap* mOceanHomePlanet;           // Referencing an removed object?
    PlanetMap* mOceanRingPlanet;           // OceanRingPlanet
    ModelObj* mOceanRingPlanetLowInWater;  // OceanRingPlanetLowInWater
};

namespace OceanHomeMapFunction {
    void tryEntryOceanHomeMap(PlanetMap*);
};  // namespace OceanHomeMapFunction
