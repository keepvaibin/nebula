#pragma once

#include "Game/MapObj/MapObjActor.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include <nebula_geometry.hpp>

class DesertMovingLand : public MapObjActor {
public:
    DesertMovingLand(const char* pName);
    virtual void init(const TableIter& rIter) override;
    virtual void control() override;
    virtual void connectToScene(const MapObjActorInitInfo& rIter) override;
    virtual void initCaseUseSwitchA(const MapObjActorInitInfo&) override;
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&) override;
    virtual void initCaseNoUseSwitchB(const MapObjActorInitInfo&) override;
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
