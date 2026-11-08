#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FallingSmallRock : public LiveActor {
public:
    FallingSmallRock(const char*);

    virtual void init(const TableIter&);
    virtual void startClipped();
    virtual void endClipped();

    void exeMove();
    inline void exeWait();
};
