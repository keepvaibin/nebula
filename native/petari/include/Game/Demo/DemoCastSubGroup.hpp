#pragma once

#include "Game/Demo/DemoCastGroup.hpp"
#include "Game/Demo/DemoExecutor.hpp"

class DemoCastSubGroup : public DemoCastGroup {
public:
    DemoCastSubGroup(const char*);

    virtual void init(const TableIter&) override;
    virtual bool tryRegisterDemoActor(LiveActor*, const TableIter&, const PlacementId&) override;

    DemoExecutor* mExecutor;
};
