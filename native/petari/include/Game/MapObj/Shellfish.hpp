#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class WalkerStateBindStarPointer;

class Shellfish : public LiveActor {
public:
    Shellfish(const char*);

    virtual ~Shellfish();
    virtual void init(const JMapInfoIter&) override;
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
    void exeDPDOpen();
    void endDPDOpen();
    void initItem(const JMapInfoIter&);
    void initCoin(const JMapInfoIter&);
    void initYellowChip(const JMapInfoIter&);
    void initBlueChip(const JMapInfoIter&);
    void initKinokoOneUp(const JMapInfoIter&);
    void startBindItem();
    void endBindItem();
    void putItem();

    AnimScaleController* mScaleController;
    WalkerStateBindStarPointer* mStarPointerState;
    LiveActor* mItem;
    s32 mItemType;
    bool mIsItemBound;
    CollisionParts* mLowCollision;
    bool _A4;
};
