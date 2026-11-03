#pragma once

#include "Game/LiveActor/PartsModel.hpp"

class MarioSearchLight : public PartsModel {
public:
    MarioSearchLight(LiveActor* pActor);
    virtual void init(const JMapInfoIter&);
    virtual void calcAndSetBaseMtx();
    virtual void updateHitSensor(HitSensor* pSensor);
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);

    f32 _9C;
    u16 _A0;
    HitSensor* _A4;
};
