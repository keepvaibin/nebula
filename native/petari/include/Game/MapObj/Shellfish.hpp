#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class WalkerStateBindStarPointer;

class Shellfish : public LiveActor {
public:
    Shellfish(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void control() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;

    void exeWait();
    void exeOpen();
    void exeOpenWait();
    void exeCloseSignal();
    void exeClose();
    void exeShock();
    void exePointerOpen();
    void endPointerOpen();
    void initItem(const TableIter&);
    void initCoin(const TableIter&);
    void initYellowChip(const TableIter&);
    void initBlueChip(const TableIter&);
    void initKinokoOneUp(const TableIter&);
    void startBindItem();
    void endBindItem();
    void putItem();

    AnimScaleController* mScaleController;
    WalkerStateBindStarPointer* mStarPointerState;
    LiveActor* mItem;
    s32 mItemType;
    bool mIsItemBound;
    CollisionParts* mLowCollision;
};
