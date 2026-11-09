#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class LavaGeyser : public LiveActor {
public:
    LavaGeyser(const char*);

    virtual void init(const TableIter&) override;
    virtual void startClipped() override;
    virtual void updateHitSensor(HitSensor*) override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;

    void exeWait();
    void exeWaitSwitch();
    void exeSign();
    void exeShootUp();
    void exeShootKeep();
    void exeShootDown();

    s32 mArg0;
    s32 mArg1;
    Vec3f mTopPos;
    Vec3f mUp;
};
