#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class AnimScaleController;
class JointController;
struct JointControllerInfo;
class SkeletalFishRailControl;

class SkeletalFishBaby : public LiveActor {
public:
    /// @brief Creates a new `SkeletalFishBaby`.
    /// @param pName A pointer to the null-terminated name of the object.
    SkeletalFishBaby(const char*);

    virtual void init(const JMapInfoIter&);
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
    bool calcJoint(TPos3f*, const JointControllerInfo&);
    bool damage(const TVec3f&, bool);
    void initRail(const JMapInfoIter&);
    void initJoint();
    void initSensor();
    bool isAttackable() const;
    bool isStarPointerPointing() const;

    JointController* mControllers[4];
    s32* mJointIndicies;
    f32 mSpeedMax;
    f32 mSpeed;
    SkeletalFishRailControl* mRailControl;
    TPos3f _AC;
    f32 mObjArg1;
    AnimScaleController* mScaleController;
    LiveActor* mStarPieceTargets[4];
};
