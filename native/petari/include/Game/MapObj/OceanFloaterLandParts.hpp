#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class LodCtrl;
class MapPartsRailMover;

class OceanFloaterLandParts : public LiveActor {
public:
    OceanFloaterLandParts(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void makeActorAppeared();
    virtual void makeActorDead();
    virtual void control();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void initMoveType(const JMapInfoIter&);
    void initRailMoveFunction(const JMapInfoIter&);
    void initLodCtrl(const JMapInfoIter&);
    void endFloatUpDemo();
    void exeWait();
    void exeMove();
    void exeDone();

    MapPartsRailMover* mRailMover;
    LodCtrl* mLodCtrl;
    TVec3f mRailEndPos;
    const char* mObjectName;
    bool mIsMoving;
    bool mIsRegisteredDemo;
    s32 mAlreadyDoneFlag;
};
