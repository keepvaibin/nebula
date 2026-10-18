#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TVec.hpp>

class FireBall : public LiveActor {
public:
    FireBall(const char* = "\203\164\203\100\203\103\203\101\201\133\203\173\201\133\203\213" /* ファイアーボール */);

    virtual void init(const JMapInfoIter&);
    virtual void appear();
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    void initHitSensors(const char*);
    void appearAndThrow(const TVec3f&, f32, f32);
    HitSensor* isBindedAny() const;
    void setVelocityToPlayer(f32);
    void calcReflectVelocity();
    bool tryToKill();

    void exeThrow();
    void exeReflect();

    /* 0x8C */ LiveActor* mHost;
    /* 0x90 */ TVec3f mUp;
};
