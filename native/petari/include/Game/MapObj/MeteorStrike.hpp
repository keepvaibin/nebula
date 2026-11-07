#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

enum MeteorStrikeType {
    MeteorStrikeType_Environment = 1,
    MeteorStrikeType_Cannon = 2
};

class ModelObj;

class MeteorStrike : public LiveActor {
public:
    MeteorStrike(const char*);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void appear();
    virtual void kill();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    void appear(s32);
    bool getMovedPos(Vec3f*, s32) const;
    static f32 getSpeed(const TableIter&);
    void initMapToolInfo(const TableIter&);
    void initModel();
    void initRailDirection();
    void calcBreakPosture(const Vec3f&, Pos3f*);
    void emitEffectColumn(const Pos3f&);
    void startRumble();
    bool isInScreen() const;

    void exeMove();
    void exeBreak();

    s32 mType;
    f32 mStepSize;  // Obj_arg0
    Vec3f mRailDir;
    ModelObj* mBrokenModel;
    s32 mStepsLeft;
    s32 mTotalSteps;
    Mtx34f mLavaColumnPos;
};
