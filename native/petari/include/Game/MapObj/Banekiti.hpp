#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class AnimScaleController;
class WalkerStateBindStarPointer;
class MapPartsRailMover;

class Banekiti : public LiveActor {
public:
    Banekiti(const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void initStarPointer();
    void exeWait();
    void exeRepel();
    void exePointerSwoon();
    void endPointerSwoon();

    AnimScaleController* mAnimScaleCtrl;
    WalkerStateBindStarPointer* mBindStarPointer;
    MapPartsRailMover* mRailMover;
};
