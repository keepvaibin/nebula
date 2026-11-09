#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class TimerSwitch : public LiveActor {
public:
    TimerSwitch(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void control() override;

    s32 mTimeLimit;
    s32 mTimeLeft;
};
