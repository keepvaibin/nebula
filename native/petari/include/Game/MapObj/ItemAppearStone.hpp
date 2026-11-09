#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ItemAppearStone : public LiveActor {
public:
    ItemAppearStone(const char*);

    virtual void init(const TableIter&) override;
    virtual void kill() override;
    virtual void calcAndSetBaseMtx() override;

    void exeWait();
};
