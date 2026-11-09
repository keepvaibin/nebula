#pragma once

#include "Game/MapObj/PressureBase.hpp"

class ActorCameraInfo;

class WaterPressure : public PressureBase {
public:
    WaterPressure(const char*);

    virtual void init(const TableIter&) override;
    virtual void initBullet(const TableIter&) override;
    virtual bool shotBullet(f32) override;

    void calcGunPointFromCannon(Pos3f*);

    ActorCameraInfo* mCameraInfo;
    bool mIsInvalidSpinKill;
};
