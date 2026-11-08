#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class HitSensor;

class StinkBugBase : public LiveActor {
public:
    StinkBugBase(const char*);

    virtual void init(const TableIter&);

    void setDashVelocity(f32);
    bool tryTurnSearch(f32);
    bool tryTurnDashSign(f32);
    void fixInitPos();
    bool isPlayerInTerritory(f32, f32, f32, f32) const;
    bool isHitHorn(HitSensor*, HitSensor*, f32) const;

    Vec3f mFront;
    Vec3f mInitPos;
    Vec3f mInitFront;
    f32 mTerritoryAngle;
    f32 mRadius;
    bool mIsTurnLeft;
};
