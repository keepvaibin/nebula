#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AirBubbleGenerator : public LiveActor {
public:
    AirBubbleGenerator(const char* pName);

    virtual void init(const TableIter&);
    virtual void control();

    void exeWait();
    void exeGenerate();

    s32 mWaitTime;
    s32 mBubbleLifeTime;
};
