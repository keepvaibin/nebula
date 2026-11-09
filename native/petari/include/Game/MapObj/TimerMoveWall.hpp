#pragma once

#include "Game/MapObj/MapObjActor.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"

class TimerMoveWall : public MapObjActor {
public:
    TimerMoveWall(const char*);

    virtual void init(const TableIter&) override;
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&) override;

    void startMove();

    void exeWait();
    void exeMove();

    s32 mTimer;
    f32 mMoveLength;
};
