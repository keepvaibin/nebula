#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class FireBall : public LiveActor {
public:
    FireBall(const char* = "ファイアーボール");

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;

    void initHitSensors(const char*);
    void appearAndThrow(const Vec3f&, f32, f32);
    HitSensor* isBindedAny() const;
    void setVelocityToPlayer(f32);
    void calcReflectVelocity();
    bool tryToKill();

    void exeThrow();
    void exeReflect();

    LiveActor* mHost;
    Vec3f mUp;
};
