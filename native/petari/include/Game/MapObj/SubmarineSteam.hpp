#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class SubmarineSteam : public LiveActor {
public:
    SubmarineSteam(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;

    void exeSteam();
    void exeWaitForSwitchOn();

    Vec3f mUp;
};
