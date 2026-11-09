#pragma once
#include "Game/Enemy/BegomanBase.hpp"
#include "Game/LiveActor/HitSensor.hpp"

class BegomanBaby : public BegomanBase {
public:
    BegomanBaby(const char*);
    BegomanBaby(NameObj*, const char*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPush(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    virtual u32 getKind() const override {
        return Kind_Baby;
    }

    virtual bool onTouchElectric(const Vec3f&, const Vec3f&) override;
    virtual bool setNerveReturn() override;
    virtual const Nerve* getNerveWait() override;

    bool receiveMsgTrample(HitSensor*, HitSensor*);
    void appearFromLaunch(const Vec3f&, const Vec3f&);
    void appearFromGuarder();
    void killWithGenItem();
    void exeNoCalcWait();
    void endNoCalcWait();
    void exeWait();
    void exeSignAttack();
    void exePursue();
    void exeTurn();
    void exeBrake();
    void exeStepBack();
    void exeReturn();
    void exeProvoke();
    void exeTrample();
    void exeHitReaction();
    void exeTired();
    void exeBlow();
    void exeAfterLaunch();
    void exeAfterLaunchOnGround();
    void exeLaunchFromGuarder();
    void exeLaunchTurn();
    void exeInWater();
    void exeKeepDistance();
    void exeBindStarPointer();
    void endBindStarPointer();
    bool calcHeadJoint(Pos3f*, const JointControllerInfo&);

    JointControlDelegator< BegomanBaby >* mBabyDelegator;
    Pos3f mWaterColumnMatrix;
    NameObj* mHost;
    bool mAppearThreeStarPiece;
};
