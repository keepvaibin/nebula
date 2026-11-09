#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class TicoReading : public LiveActor {
public:
    TicoReading(const char*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void kill() override;

    void exeWait();
};
