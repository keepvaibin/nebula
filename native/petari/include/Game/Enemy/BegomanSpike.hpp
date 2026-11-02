#pragma once
#include "Game/Enemy/BegomanBase.hpp"
class BegomanHead;
class ModelObj;
class BegomanSpike : public BegomanBase {
public:
    BegomanSpike(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void appear();
    virtual void control();
    virtual void calcAnim();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPush(HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);
    virtual u32 getKind() const {
        return 1;
    }
    virtual bool onTouchElectric(const TVec3f&, const TVec3f&);
    virtual bool setNerveReturn();
    virtual void setNerveLaunch();
    virtual const Nerve* getNerveWait();
    void onSwitchB();
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
    TPos3f mHeadMatrix;
    ModelObj* mBrokenModel;
    TPos3f mWaterColumnMatrix;
    bool mTurnAfterBlow;
};
