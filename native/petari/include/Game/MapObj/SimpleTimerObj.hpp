#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class FlashingCtrl;

class SimpleTimerObj : public MapObjActor {
public:
    SimpleTimerObj(const char*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;

    void exeTimer();

    FlashingCtrl* mFlashingCtrl;
    s32 mTimeLimit;
    s32 mTimeLeft;
};
