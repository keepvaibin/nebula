#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class AstroDome : public MapObjActor {
public:
    AstroDome(const char*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void control() override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void exeWait();
    void exeDisappear();
    void exeAppear();
};
