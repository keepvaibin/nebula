#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class LavaGeyser : public LiveActor {
public:
    LavaGeyser(const char*);

    virtual void init(const TableIter&);
    virtual void startClipped();
    virtual void updateHitSensor(HitSensor*);
    virtual void attackSensor(HitSensor*, HitSensor*);

    void exeWait();
    void exeWaitSwitch();
    void exeSign();
    void exeShootUp();
    void exeShootKeep();
    void exeShootDown();

    s32 mArg0;
    s32 mArg1;
    Vec3f _94;
    Vec3f _A0;
    f32 _AC;
    f32 _B0;  // one of these may be a different data type
    f32 _B4;
    f32 _B8;
};
