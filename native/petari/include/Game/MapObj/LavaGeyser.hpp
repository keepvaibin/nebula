#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class LavaGeyser : public LiveActor {
public:
    LavaGeyser(const char*);

    virtual void init(const JMapInfoIter&);
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
    TVec3f _94;
    TVec3f _A0;
    f32 _AC;
    f32 _B0;  // one of these may be a different data type
    f32 _B4;
    f32 _B8;
};
