#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SnowplowSwitch : public LiveActor {
public:
    SnowplowSwitch(const char* pName);

    virtual void init(const TableIter&);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    bool mIsDiscovered;
};
