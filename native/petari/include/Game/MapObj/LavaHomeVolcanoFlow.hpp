#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class LavaHomeVolcanoFlow : public LiveActor {
public:
    LavaHomeVolcanoFlow(const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void startClipped() override;
    virtual void endClipped() override;

    void exeMove();
    void exeWait();
};
