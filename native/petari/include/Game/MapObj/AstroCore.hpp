#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class AstroCore : public MapObjActor {
public:
    AstroCore(const char*);

    virtual void init(const TableIter&) override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void exeWait();
    void exeGrow();

    void startDemo();
    void startAnimGrow();
    void setStateBeforeGrow();
};
