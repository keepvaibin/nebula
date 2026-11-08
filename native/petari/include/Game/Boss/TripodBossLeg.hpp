#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class IKJoint;
class TripodBoss;
class TripodBossMovableArea;
class TripodBossStepPoint;

class TripodBossLeg : public LiveActor {
public:
    /// @brief Creates a new `TripodBossLeg`.
    /// @param pName A pointer to the null-terminated name of the object.
    TripodBossLeg(const char* pName);

    virtual ~TripodBossLeg();
    virtual void init(const TableIter&);
    virtual void makeActorAppeared();
    virtual void makeActorDead();
    virtual void control();
    virtual void attackSensor(HitSensor*, HitSensor*);

    void initShadow(const char*);
    void setBody(TripodBoss*);
    void setMovableArea(TripodBossMovableArea*);
    void setIKParam(f32, f32, const Vec3f&, const Vec3f&, const Vec3f&);
    void setStepTarget(TripodBossStepPoint*);
    void setWait();
    void setForceEndPoint(const Vec3f&);
    void setDemoEffectTiming(bool);
    const Pos3f& getRootJointMatrix() const;
    const Pos3f& getMiddleJointMatrix() const;
    const Pos3f& getEndJointMatrix() const;
    const Pos3f& getRootLocalYMatrix() const;
    const Pos3f& getRootLocalYZMatrix() const;
    const Pos3f& getAnkleLocalXMatrix() const;
    const Pos3f& getAnkleLocalXZMatrix() const;
    void requestStepTarget(TripodBossStepPoint*);
    void requestLeaveOut();
    void requestBreak();
    void requestStartDemo();
    void requestEndDemo();
    bool isLanding() const;
    bool isDamage() const;
    bool isStop() const;
    bool isBroken() const;
    bool isPressPlayer() const;
    bool canStep() const;
    bool canCancelStep() const;
    bool canWeighting() const;
    void addToTargetPower(const Vec3f&, f32);
    void addIKLimitPower();
    void addAccelUpLeg(TripodBossStepPoint*, f32);
    f32 calcLegHeight(TripodBossStepPoint*) const;
    void updatePose();
    void updateIKPose();
    bool bindEndPosition();
    void separateLocalAxisRootJoint(const Pos3f&);
    void separateAnkleJointLocalAxis();
    void updateAnkleUp(f32);
    void updateAnkleSlerpToBasePose();
    void updateAnkleLanding();
    void updateAnkleShadowMatrix();

    void exeHold();
    void exeDemo();
    void exeMove();
    void exeUp();
    void exeLeaveOut();
    void exeMoveToLandingPos();
    void exeStampSign();
    void exeLanding();
    void exeDamageVibration();
    void exeDamage();
    void exeBreak();

    TripodBoss* mBoss;
    TripodBossMovableArea* mMoveArea;
    TripodBossStepPoint* mPrevStepPoint;
    TripodBossStepPoint* mStepPoint;
    IKJoint* mJoint;
    Pos3f mRootLocalMtx;
    Pos3f mRootLocalYMtx;
    Pos3f mRootLocalYZMtx;
    Pos3f mAnkleLocalXMtx;
    Pos3f mAnkleLocalXZMtx;
    Pos3f mEndJointMtx;
    Pos3f mAnkleShadowMtx;
    Quat4f mAnkleStartQuat;
    Quat4f mAnkleLandingQuat;
    Vec3f mRootLocalPos;
    Vec3f mForceEndPoint;
    Vec3f mEndVelocity;
    Vec3f mEndTarget;
    s32 mPoseMode;
    f32 mLandingHeight;
    bool mIsAttackHit;
    bool mIsPressPlayer;
    s32 mDemoEffectTiming;
    AudAnmSoundObject* mSoundObj;
};

namespace MR {
    void separateMatrixRotateYZX(Pos3f*, Pos3f*, const Pos3f&, const Pos3f&);
};  // namespace MR
