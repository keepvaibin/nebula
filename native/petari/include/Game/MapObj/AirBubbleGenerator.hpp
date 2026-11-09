#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AirBubbleGenerator : public LiveActor {
public:
    AirBubbleGenerator(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void control() override;

    void exeWait();
    void exeGenerate();

    s32 mWaitTime;
    s32 mBubbleLifeTime;
};
