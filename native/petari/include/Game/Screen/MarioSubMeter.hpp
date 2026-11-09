#pragma once

#include "Game/NameObj/NameObj.hpp"

class SubMeterLayout;

class MarioSubMeter : public NameObj {
public:
    MarioSubMeter(const char* pName);

    virtual ~MarioSubMeter() override;
    virtual void init(const TableIter& rIter) override;

    void setBeeLifeRatio(f32);
    void setWaterLifeRatio(f32);
    void activeSubMeterBee();
    void activeSubMeterWater();
    void frameOutSubMeter();
    void activate();
    void deactivate();

    SubMeterLayout* mFlyMeter;
    SubMeterLayout* mAirMeter;
    SubMeterLayout* mActiveMeter;
};
