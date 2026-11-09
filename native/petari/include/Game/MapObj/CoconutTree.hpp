#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class CoconutTree : public LiveActor {
public:
    CoconutTree(const char*);

    virtual void init(const TableIter&) override;
};