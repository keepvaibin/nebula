#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class CollisionParts;

class TreasureBoxCracked : public LiveActor {
public:
    TreasureBoxCracked(const char*);

    virtual void init(const TableIter&);
    virtual void control();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);

    void exeWait();
    void exeOpen();
    void exeItemBound();
    void exeOpenWait();
    void exeAlwaysOpen();
    void initItem(const TableIter&);
    void initItemTrans();
    LiveActor* initYellowChip(const TableIter&);
    LiveActor* initBlueChip(const TableIter&);
    LiveActor* initKinokoOneUp(const TableIter&);
    bool checkItemPos();
    bool isHalfOpen() const;

    CollisionParts* mBoxCollider;
    Pos3f mTopMtx;
    s32 mBoxType;
    s32 mOpenCondition;
    LiveActor* mItem;
    s32 mItemType;
    Vec3f mItemVelocity;
    Vec3f mItemTargetPos;
    bool mIsItemMoving;
    s32 mWaitForSwitchOn;
    s32 mPowerStarId;
};
