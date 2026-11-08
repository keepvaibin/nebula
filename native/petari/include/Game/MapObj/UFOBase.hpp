#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class CollisionParts;
class LodCtrl;
class MapPartsRailMover;
class ModelObj;

class UFOBase : public LiveActor {
public:
    UFOBase(const char*);

    virtual void init(const TableIter&);
    virtual void kill();
    virtual void makeActorDead();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void initSensorType();

    void initSubModel(const TableIter&, const char*);
    void exeWaitForPlayerOn();
    void exeMove();
    void exeWait();
    void exeBreak();

    CollisionParts* mCollisionParts;
    LodCtrl* mLODCtrl;
    ModelObj* mModel;
    MapPartsRailMover* mRailMover;
    Vec3f mFront;
    f32 mRotateSpeed;
    s32 mMoveConditionType;
    bool mIsRailDone;
    const char* mObjName;
};

class UFOSolid : public UFOBase {
public:
    UFOSolid(const char*);
};

class UFOBreakable : public UFOBase {
public:
    UFOBreakable(const char*);

    virtual void initSensorType();
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);
};
