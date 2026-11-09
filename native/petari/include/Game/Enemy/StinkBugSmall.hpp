#pragma once

#include "Game/Enemy/StinkBugBase.hpp"

class AnimScaleController;
class WalkerStateBindStarPointer;

class StinkBugSmall : public StinkBugBase {
public:
    StinkBugSmall(const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void kill() override;
    virtual void control() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual void calcAndSetBaseMtx() override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual void initAfterPlacement() override;
    void exeWait();
    void exeSearch();
    void exeDashSign();
    void exeDash();
    void exeDashEnd();
    void exeBack();
    void exeHipDropDown();
    void exeAttack();
    void exeSpinReaction();
    void exeShakeStart();
    void exePanic();
    void exeRecover();
    void exePointerSwoon();
    void endPointerSwoon();
    void exeForceFall();
    void exeShake();
    void exeDashSignEnd();
    bool tryPointerSwoon();
    bool tryForceFall();
    bool isPlayerInSmallTerritory() const;
    bool isShakeChance() const;
    bool isNrvEnableStarPieceAttack() const;

    AnimScaleController* mScaleController;
    WalkerStateBindStarPointer* mBindStarPointer;

private:
    bool mIsBodyAttack;
};
