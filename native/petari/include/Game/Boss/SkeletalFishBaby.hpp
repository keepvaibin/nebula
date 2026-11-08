#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class AnimScaleController;
class JointController;
struct JointControllerInfo;
class SkeletalFishRailControl;

class SkeletalFishBaby : public LiveActor {
public:
    SkeletalFishBaby(const char*);

    virtual void init(const TableIter&);
    virtual void calcAnim();
    virtual void appear();
    virtual void makeActorAppeared();
    virtual void kill();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void exeSwim();
    void exeBind();
    void endBind();
    void exeBreak();
    void exeDead();
    bool calcJoint(Pos3f*, const JointControllerInfo&);
    bool damage(const Vec3f&, bool);
    void initRail(const TableIter&);
    void initJoint();
    void initSensor();
    bool isAttackable() const;
    bool isStarPointerPointing() const;

    JointController* mControllers[4];
    s32* mJointIndicies;
    f32 mSpeedMax;
    f32 mSpeed;
    SkeletalFishRailControl* mRailControl;
    Pos3f mRailMtxInv;
    AnimScaleController* mScaleController;
    LiveActor* mStarPieceTargets[4];
};
