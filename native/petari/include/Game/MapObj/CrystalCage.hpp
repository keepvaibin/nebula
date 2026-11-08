#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class DummyDisplayModel;
class ModelObj;
class RumbleCalculatorCosMultLinear;

class CrystalCage : public LiveActor {
public:
    CrystalCage(const char*);

    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void kill();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);

    void forceBreak();
    void initMapToolInfo(const TableIter&);
    void initModel(const char*);
    void tryOnSwitchDead();
    void exeWait();
    void exeBreak();
    void exeBreakAfter();

    s32 mCrystalCageType;
    ModelObj* mBreakObj;
    Pos3f mBreakMtx;
    s32 mHitPoint;
    s32 mHitInvalidTime;
    RumbleCalculatorCosMultLinear* mRumbleCalc;
    Vec3f mRumbleDir;
    Vec3f mBasePos;
    Vec3f mSensorPos;
    DummyDisplayModel* mDisplayModel;
    Vec3f mGroundPos;
    bool mBreakOnlyIfShown;
    s32 mPowerStarId;
    bool mIsBreakObjVisible;
    bool mPlayRiddleSFX;
    bool mHasBinding;
    Vec3f mBinderOffset;
};
