#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class HitSensor;

class StinkBugBase : public LiveActor {
public:
    StinkBugBase(const char*);

    virtual void init(const JMapInfoIter&);

    void setDashVelocity(f32);
    bool tryTurnSearch(f32);
    bool tryTurnDashSign(f32);
    void fixInitPos();
    bool isPlayerInTerritory(f32, f32, f32, f32) const;
    bool isHitHorn(HitSensor*, HitSensor*, f32) const;

    Vec3f _8C;
    Vec3f _98;
    Vec3f _A4;
    f32 _B0;
    f32 mRadius;
    bool _B8;
};
