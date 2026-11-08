#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class DummyDisplayModel;
class ModelObj;
class RumbleCalculatorCosMultLinear;

class CrystalCage : public LiveActor {
public:
    // By object name: CrystalCageS, CrystalCageM, anything else.
    enum CrystalCageType {
        CrystalCageType_Small,
        CrystalCageType_Medium,
        CrystalCageType_Large,
    };

    CrystalCage(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void initAfterPlacement();
    virtual void kill();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void forceBreak();
    void startBreak();
    void initMapToolInfo(const TableIter& rIter);
    void initModel(const char* pName);
    void tryOnSwitchDead();
    void exeWait();
    void exeBreak();
    void exeBreakAfter();

    CrystalCageType mCrystalCageType;
    ModelObj* mBreakObj;
    Pos3f mBreakMtx;
    s32 mHitPoint;
    s32 mHitInvalidTime;
    RumbleCalculatorCosMultLinear* mRumbleCalc;
    Vec3f mRumbleDir;
    Vec3f mBasePos;
    Vec3f mSensorPos;
    DummyDisplayModel* mDisplayModel;
    // Where the broken base rests.
    Vec3f mGroundPos;
    bool mBreakOnlyIfShown;
    s32 mPowerStarId;
    // Hidden whole when broken, instead of leaving a broken base.
    bool mVanishOnBreak;
    bool mPlayRiddleSFX;
    bool mHasBinding;
    Vec3f mBinderOffset;
};
