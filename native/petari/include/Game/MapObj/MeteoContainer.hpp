#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class MeteoContainer : public LiveActor {
public:
    MeteoContainer(const char*);

    virtual void init(const TableIter&);
    virtual void appear();
    virtual void kill();
    virtual void control();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPush(HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void exeWait();
    void exeDestroy();
};
