#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class HitSensor;

class StinkBugBase : public LiveActor {
public:
    StinkBugBase(const char*);

    virtual void init(const TableIter&) override;

    void setDashVelocity(f32);
    bool tryTurnSearch(f32);
    void turnDashSign(f32);
    void fixInitPos();
    bool isPlayerInTerritory(f32 reachMargin, f32 sideHalfSize, f32 upHalfSize, f32 backOffset) const;
    bool isHitHorn(HitSensor*, HitSensor*, f32 margin) const;

    Vec3f mFront;
    Vec3f mInitPos;
    Vec3f mInitFront;
    f32 mTerritoryAngle;
    f32 mRadius;
    bool mIsTurnLeft;
};
