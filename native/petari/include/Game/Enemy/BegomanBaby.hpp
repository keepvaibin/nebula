#pragma once
#include "Game/Enemy/BegomanBase.hpp"
#include "Game/LiveActor/HitSensor.hpp"

class BegomanBaby : public BegomanBase {
public:
    BegomanBaby(const char*);
    BegomanBaby(NameObj*, const char*);

    virtual void init(const TableIter&);
    virtual void appear();
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPush(HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    virtual u32 getKind() const {
        return Kind_Baby;
    }

    virtual bool onTouchElectric(const Vec3f&, const Vec3f&);
    virtual bool setNerveReturn();
    virtual const Nerve* getNerveWait();

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
