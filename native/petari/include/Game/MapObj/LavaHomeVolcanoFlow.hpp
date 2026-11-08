#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class LavaHomeVolcanoFlow : public LiveActor {
public:
    LavaHomeVolcanoFlow(const char*);

    virtual void init(const TableIter& rIter);
    virtual void startClipped();
    virtual void endClipped();

    void exeMove();
    void exeWait();
};
