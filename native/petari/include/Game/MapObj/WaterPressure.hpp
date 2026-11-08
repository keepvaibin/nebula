#pragma once

#include "Game/MapObj/PressureBase.hpp"

class ActorCameraInfo;

class WaterPressure : public PressureBase {
public:
    WaterPressure(const char*);

    virtual void init(const TableIter&);
    virtual void initBullet(const TableIter&);
    virtual bool shotBullet(f32);

    void calcGunPointFromCannon(Pos3f*);

    ActorCameraInfo* mCameraInfo;
    bool mIsInvalidSpinKill;
};
