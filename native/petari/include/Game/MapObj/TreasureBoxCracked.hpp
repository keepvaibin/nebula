#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class CollisionParts;

class TreasureBoxCracked : public LiveActor {
public:
    TreasureBoxCracked(const char*);

    virtual ~TreasureBoxCracked();
    virtual void init(const JMapInfoIter&);
    virtual void control();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);

    void exeWait();
    void exeOpen();
    void exeItemBound();
    void exeOpenWait();
    void exeAlwaysOpen();
    void initItem(const JMapInfoIter&);
    void initItemTrans();
    LiveActor* initYellowChip(const JMapInfoIter&);
    LiveActor* initBlueChip(const JMapInfoIter&);
    LiveActor* initKinokoOneUp(const JMapInfoIter&);
    bool checkItemPos();
    bool isHalfOpen() const;

    CollisionParts* mBoxCollider;
    TPos3f mTopMtx;
    s32 mBoxType;
    s32 mOpenCondition;
    LiveActor* mItem;
    s32 mItemType;
    TVec3f mItemVelocity;
    TVec3f mItemTargetPos;
    bool mIsItemMoving;
    s32 mWaitForSwitchOn;
    s32 mPowerStarId;
};
