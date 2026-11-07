#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class AstroDomeAsteroid : public LiveActor {
public:
    AstroDomeAsteroid(const char*);

    virtual void init(const TableIter&);
    virtual void appear();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void exeWait();

    Pos3f mBaseMtx;
};
