#pragma once

#include "Game/LiveActor/PartsModel.hpp"

class AnimScaleController;

class OtaKingLongFoot : public PartsModel {
public:
    OtaKingLongFoot(LiveActor* pHost, s32, const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void hide();
    void startAppearDemo();
    void startDownDemo();
    void endDemo();
    void initSensor();

    void exeWait();
    void exeDamage();
    void exeAppearDemo();
    void exeDownDemo();

    s32 mDemoBckStep;
    AnimScaleController* mScaleController;
};
