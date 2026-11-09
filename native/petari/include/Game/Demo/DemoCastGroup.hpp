#pragma once

#include "Game/NameObj/NameObj.hpp"

class PlacementId;
class LiveActor;
class LiveActorGroup;

class DemoCastGroup : public NameObj {
public:
    DemoCastGroup(const char*);

    virtual ~DemoCastGroup() override {
    }

    virtual void init(const TableIter&) override;
    virtual bool tryRegisterDemoActor(LiveActor*, const TableIter&, const PlacementId&);
    virtual bool tryRegisterDemoActor(LiveActor*, const char*, const TableIter&);
    virtual void registerDemoActor(LiveActor*, const TableIter&);

    PlacementId* mInfo;
    LiveActorGroup* mGroup;
};
