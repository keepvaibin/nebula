#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class SpaceShipStep : public MapObjActor {
public:
    SpaceShipStep(const char*);

    virtual void init(const TableIter&);
    virtual void control();
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);
    virtual void initCaseUseSwitchA(const MapObjActorInitInfo&) {
        return;
    }
    virtual void initCaseNoUseSwitchA(const MapObjActorInitInfo&) {
        return;
    }
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&) {
        return;
    }
    virtual void initCaseNoUseSwitchB(const MapObjActorInitInfo&) {
        return;
    }

    void exeWait();
    void exeMoveSign();
    void exeMove();
    void exeBreak();
    void updateResetToInitPosCheck();

    Vec3f mRailClippingCenter;
    s32 mHiddenStep;
    bool mHasBeenSeen;
};
