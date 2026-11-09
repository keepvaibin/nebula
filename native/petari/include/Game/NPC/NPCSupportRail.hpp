#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class NPCSupportRail : public LiveActor {
public:
    NPCSupportRail(const char* pName);

    virtual void init(const TableIter&) override;
};
