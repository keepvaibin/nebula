#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class Fountain : public LiveActor {
public:
    Fountain(const char*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;

    void initHitSensors(const char*);
    void exeWait();
    void exeMove();

    const char* mFountainName;
    Vec3f mUp;
};
