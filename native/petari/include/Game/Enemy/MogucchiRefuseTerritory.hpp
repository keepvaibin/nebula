#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class MogucchiRefuseTerritory : public LiveActor {
public:
    MogucchiRefuseTerritory(const char*);
    virtual void init(const TableIter&) override;
};
