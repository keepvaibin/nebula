#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SnowplowSwitch : public LiveActor {
public:
    SnowplowSwitch(const char* pName);

    virtual void init(const TableIter&) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    bool mIsDiscovered;
};
