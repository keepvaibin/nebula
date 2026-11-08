#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class Sun : public LiveActor {
public:
    Sun(const char* pName);

    virtual void init(const TableIter& rIter);
};
