#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class Tsukidashikun : public MapObjActor {
public:
    Tsukidashikun(const char*);

    virtual void init(const TableIter&) override;

    void exeWait();
    void exeSign();
    void exeMove();
    void exeRelax();
    void connectToScene(const MapObjActorInitInfo&) override;
    void initCaseUseSwitchB(const MapObjActorInitInfo&) override;
    void initCaseNoUseSwitchB(const MapObjActorInitInfo&) override;
    void startMove();

    f32 mMoveSpeed;
    s32 mWaitTimer;
};
