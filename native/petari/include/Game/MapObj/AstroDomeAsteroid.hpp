#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class AstroDomeAsteroid : public LiveActor {
public:
    AstroDomeAsteroid(const char*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void exeWait();

    Pos3f mBaseMtx;
};
