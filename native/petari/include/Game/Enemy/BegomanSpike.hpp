#pragma once
#include "Game/Enemy/BegomanBase.hpp"
class BegomanHead;
class ModelObj;
class BegomanSpike : public BegomanBase {
public:
    BegomanSpike(const char* pName);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void control() override;
    virtual void calcAnim() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPush(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;
    virtual u32 getKind() const override {
        return Kind_Spike;
    }
    virtual bool onTouchElectric(const Vec3f&, const Vec3f&) override;
    virtual bool setNerveReturn() override;
    virtual void setNerveLaunch() override;
    virtual const Nerve* getNerveWait() override;
    void onSwitchB();
    bool isNrvBlown() const;
    bool isNrvBroken() const;
    bool isNrvStarPointerBindable() const;
    void tearDownPursue();
    void generateItem();
    void exeNoCalcWait();
    void exeWait();
    void exeSignAttack();
    void exePursue();
    void exeTurn();
    void exeBrake();
    void exeStepBack();
    void exeReturn();
    void exeProvoke();
    void exeTrampleReaction();
    void exeHitReaction();
    void exeTired();
    void exeBlow();
    void exeElectricDeath();
    void exeBrokenPiece();
    void exeInWater();
    void exeKeepDistance();
    void endNoCalcWait();
    void endWait();
    void endHandBlur();
    void endPursue();
    void endTurn();
    void endBindStarPointer();
    void exeBindStarPointer();
    BegomanHead* mHead;
    Pos3f mHeadMatrix;
    ModelObj* mBrokenModel;
    Pos3f mWaterColumnMatrix;
    bool mTurnAfterBlow;
};
