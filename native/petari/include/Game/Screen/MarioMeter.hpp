#pragma once

#include "Game/NameObj/NameObj.hpp"

class MeterLayout;
class SuddenDeathMeter;

class MarioMeter : public NameObj {
public:
    MarioMeter(const char*);

    virtual ~MarioMeter() override;
    virtual void init(const TableIter& rIter) override;

    void initLifeCount(s32);
    void setLifeCount(s32);
    void powerUp();
    void requestForceAppearHPMeter();
    void requestPlayerMoving();
    void requestPlayerStopped();
    void activate();
    void deactivate();

    MeterLayout* mHitPointMeter;
    SuddenDeathMeter* mSuddenDeathMeter;
    bool mUseSuddenDeath;
};
