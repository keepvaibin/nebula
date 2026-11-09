#pragma once

#include "Game/MapObj/MapObjActor.hpp"

class SpaceShipStep : public MapObjActor {
public:
    SpaceShipStep(const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;
    virtual void initCaseUseSwitchA(const MapObjActorInitInfo&) override {
        return;
    }
    virtual void initCaseNoUseSwitchA(const MapObjActorInitInfo&) override {
        return;
    }
    virtual void initCaseUseSwitchB(const MapObjActorInitInfo&) override {
        return;
    }
    virtual void initCaseNoUseSwitchB(const MapObjActorInitInfo&) override {
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
