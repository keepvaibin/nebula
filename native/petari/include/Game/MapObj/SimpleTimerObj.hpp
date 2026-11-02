#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class FlashingCtrl;

class SimpleTimerObj : public MapObjActor {
public:
    SimpleTimerObj(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void appear();
    virtual void kill();
    virtual void control();

    void exeTimer();

    FlashingCtrl* mFlashingCtrl;
    s32 mTimeLimit;
    s32 mTimeLeft;
};
