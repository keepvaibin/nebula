#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class Tsukidashikun : public MapObjActor {
public:
    Tsukidashikun(const char*);

    virtual void init(const JMapInfoIter&);

    void exeWait();
    void exeSign();
    void exeMove();
    void exeRelax();
    void connectToScene(const MapObjActorInitInfo&);
    void initCaseUseSwitchB(const MapObjActorInitInfo&);
    void initCaseNoUseSwitchB(const MapObjActorInitInfo&);
    void startMove();

    f32 mMoveSpeed;
    s32 mWaitTimer;
};
