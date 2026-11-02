#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

enum MeteorStrikeType {
    MeteorStrikeType_Environment = 1,
    MeteorStrikeType_Cannon = 2
};

class ModelObj;

class MeteorStrike : public LiveActor {
public:
    MeteorStrike(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void appear();
    virtual void kill();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);

    void appear(s32);
    bool getMovedPos(TVec3f*, s32) const;
    static f32 getSpeed(const JMapInfoIter&);
    void initMapToolInfo(const JMapInfoIter&);
    void initModel();
    void initRailDirection();
    void calcBreakPosture(const TVec3f&, TPos3f*);
    void emitEffectColumn(const TPos3f&);
    void startRumble();
    bool isInScreen() const;

    void exeMove();
    void exeBreak();

    s32 mType;
    f32 mStepSize;  // Obj_arg0
    TVec3f mRailDir;
    ModelObj* mBrokenModel;
    s32 mStepsLeft;
    s32 mTotalSteps;
    TMtx34f mLavaColumnPos;
};
