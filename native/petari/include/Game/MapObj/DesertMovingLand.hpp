#pragma once

#include "Game/MapObj/MapObjActor.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include <nebula_geometry.hpp>

class DesertMovingLand : public MapObjActor {
public:
    DesertMovingLand(const char* pName);
    virtual void init(const TableIter& rIter);
    virtual void control();
    virtual void connectToScene(const MapObjActorInitInfo& rIter);
    virtual void initCaseUseSwitchA(const MapObjActorInitInfo&);
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&);
    virtual void initCaseNoUseSwitchB(const MapObjActorInitInfo&);
    void startDemo();
    void updateDemoPlayerPos();
    void exeWait();
    void exeMoveDown();
    void exeMoveUp();
    void exeMoveSign();
    void exeStop();

    Vec3f mUpperPos;
    Vec3f mLowerPos;
    s32 mMoveDownTime;
    s32 mMoveUpTime;
    Mtx34f mDemoPlayerMtx;
};
