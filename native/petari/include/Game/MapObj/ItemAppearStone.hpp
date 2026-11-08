#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ItemAppearStone : public LiveActor {
public:
    ItemAppearStone(const char*);

    virtual void init(const TableIter&);
    virtual void kill();
    virtual void calcAndSetBaseMtx();

    void exeWait();
};
