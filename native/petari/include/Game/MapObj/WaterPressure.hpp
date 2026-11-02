#pragma once

#include "Game/MapObj/PressureBase.hpp"

class ActorCameraInfo;

class WaterPressure : public PressureBase {
public:
    WaterPressure(const char*);
    virtual ~WaterPressure();

    virtual void init(const JMapInfoIter&);
    virtual void initBullet(const JMapInfoIter&);
    virtual bool shotBullet(f32);

    void calcGunPointFromCannon(TPos3f*);

    ActorCameraInfo* mCameraInfo;
    bool mIsInvalidSpinKill;
};
