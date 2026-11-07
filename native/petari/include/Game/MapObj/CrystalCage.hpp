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
    Pos3f _94;
    s32 _C4;
    s32 _C8;
    RumbleCalculatorCosMultLinear* mRumbleCalc;
    Vec3f _D0;
    Vec3f _DC;
    Vec3f _E8;
    DummyDisplayModel* mDisplayModel;
    Vec3f _F8;
    bool _104;
    u8 _105;
    u8 _106;
    u8 _107;
    s32 _108;
    bool mIsBreakObjVisible;
    bool mPlayRiddleSFX;
    bool mHasBinding;
    u8 _10F;
    Vec3f _110;
};
