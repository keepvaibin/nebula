#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class LodCtrl;
class MapPartsRailMover;

class OceanFloaterLandParts : public LiveActor {
public:
    OceanFloaterLandParts(const char*);

    virtual void init(const TableIter&) override;
    virtual void makeActorAppeared() override;
    virtual void makeActorDead() override;
    virtual void control() override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void initMoveType(const TableIter&);
    void initRailMoveFunction(const TableIter&);
    void initLodCtrl(const TableIter&);
    void endFloatUpDemo();
    void exeWait();
    void exeMove();
    void exeDone();

    MapPartsRailMover* mRailMover;
    LodCtrl* mLodCtrl;
    Vec3f mRailEndPos;
    const char* mObjectName;
    bool mIsMoving;
    bool mIsRegisteredDemo;
    s32 mAlreadyDoneFlag;
};
