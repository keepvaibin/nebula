#pragma once

#include "Game/LiveActor/PartsModel.hpp"

class MarioSearchLight : public PartsModel {
public:
    MarioSearchLight(LiveActor* pActor);
    virtual void init(const TableIter&);
    virtual void calcAndSetBaseMtx();
    virtual void updateHitSensor(HitSensor* pSensor);
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);

    f32 mBeamDistance;  // the beam's reach, sweeping out to 2000
    u16 mHitTimer;  // frames the target is kept
    HitSensor* mTarget;  // the sensor the light found (the swim's throw target)
};
