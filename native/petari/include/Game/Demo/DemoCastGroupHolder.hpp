#pragma once

#include "Game/NameObj/NameObjGroup.hpp"

class DemoCastGroup;
class PlacementId;
class LiveActor;

class DemoCastGroupHolder : public NameObjGroup {
public:
    DemoCastGroupHolder();

    bool tryRegisterDemoActor(LiveActor*, const TableIter&, const PlacementId&) const;
    bool tryRegisterDemoActor(LiveActor*, const char*, const TableIter&) const;
    DemoCastGroup* getCastGroup(int) const;
    DemoCastGroup* findCastGroup(const char*) const;
};
