#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class TimerSwitch : public LiveActor {
public:
    TimerSwitch(const char* pName);

    virtual void init(const TableIter&);
    virtual void control();

    s32 mTimeLimit;
    s32 mTimeLeft;
};
