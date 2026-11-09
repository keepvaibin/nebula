#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class CollisionParts;
class LodCtrl;
class MapPartsRailMover;
class ModelObj;

class UFOBase : public LiveActor {
public:
    UFOBase(const char*);

    virtual void init(const TableIter&) override;
    virtual void kill() override;
    virtual void makeActorDead() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
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

private:
    // The UFOs with their own sounds, resolved from the object name at init.
    enum Kind {
        Kind_Other,
        Kind_NormalB,
        Kind_BattleStageC,
        Kind_BattleStageD,
    };

    Kind mKind;
};

class UFOSolid : public UFOBase {
public:
    UFOSolid(const char*);
};

class UFOBreakable : public UFOBase {
public:
    UFOBreakable(const char*);

    virtual void initSensorType() override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;
};
