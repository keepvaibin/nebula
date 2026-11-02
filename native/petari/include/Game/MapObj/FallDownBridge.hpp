#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class FallDownBridge : public LiveActor {
public:
    FallDownBridge(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void control();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);
    virtual void calcAndSetBaseMtx();

    void exeWait();
    void exeQuake();

    TVec3f _8C;
    f32 _98;
    f32 _9C;
    f32 _A0;
    f32 _A4;
    bool _A8;
};
