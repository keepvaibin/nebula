#pragma once

#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/Table.hpp"
#include <nebula_geometry.hpp>

class FireMarioBall : public LiveActor {
public:
    FireMarioBall(const char*);
    virtual ~FireMarioBall();
    virtual void init(const TableIter&);
    virtual void appear();
    virtual void kill();
    virtual void attackSensor(HitSensor*, HitSensor*);
    void initSensor();

    bool attackFire(HitSensor*);
    void exeThrow();
    void appearAndThrow(const Vec3f&, const Vec3f&);
    HitSensor* isBindedAny() const;
    bool tryToKill();

    u16 mBindTimer;  // frames left bound before a wall kills it
};
