#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FallingSmallRock : public LiveActor {
public:
    FallingSmallRock(const char*);

    virtual void init(const TableIter&) override;
    virtual void startClipped() override;
    virtual void endClipped() override;

    void exeMove();
    inline void exeWait();
};
