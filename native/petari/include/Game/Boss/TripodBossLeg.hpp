#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class IKJoint;
class TripodBoss;
class TripodBossMovableArea;
class TripodBossStepPoint;

class TripodBossLeg : public LiveActor {
public:
    TripodBossLeg(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void makeActorAppeared();
    virtual void makeActorDead();
    virtual void control();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);

    void initShadow(const char* pName);
    void setBody(TripodBoss* pBoss);
    void setMovableArea(TripodBossMovableArea* pArea);
    void setIKParam(f32 rootLength, f32 middleLength, const Vec3f& rRootLocalPos, const Vec3f& rSide, const Vec3f& rUp);
    void setStepTarget(TripodBossStepPoint* pPoint);
    void setWait();
    void setForceEndPoint(const Vec3f& rPoint);
    void setDemoEffectTiming(bool isOn);
    const Pos3f& getRootJointMatrix() const;
    const Pos3f& getMiddleJointMatrix() const;
    const Pos3f& getEndJointMatrix() const;
    const Pos3f& getRootLocalYMatrix() const;
    const Pos3f& getRootLocalYZMatrix() const;
    const Pos3f& getAnkleLocalXMatrix() const;
    const Pos3f& getAnkleLocalXZMatrix() const;
    void requestStepTarget(TripodBossStepPoint* pPoint);
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
    void addToTargetPower(const Vec3f& rTarget, f32 power);
    void addIKLimitPower();
    void moveEndPoint(f32 damping);
    void addAccelUpLeg(TripodBossStepPoint* pPoint, f32 accel);
    f32 calcLegHeight(TripodBossStepPoint* pPoint) const;
    void updatePose();
    void updateIKPose();
    bool bindEndPosition();
    void separateLocalAxisRootJoint(const Pos3f& rBodyMtx);
    void separateAnkleJointLocalAxis();
    void updateAnkleUp(f32 legHeight);
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
    Vec3f mForceEndPoint;  // the foot
    Vec3f mEndVelocity;
    Vec3f mEndTarget;
    s32 mPoseMode;  // 0: root from the body, leg up from the boss; 1: local root, world target (demo)
    f32 mLandingHeight;
    bool mIsAttackHit;
    bool mIsPressPlayer;
    s32 mDemoEffectTiming;  // 0 off, 1 the first step on, 2 held on
    AudAnmSoundObject* mSoundObj;
};

namespace MR {
    void separateMatrixRotateYZX(Pos3f* pYMtx, Pos3f* pYZMtx, const Pos3f& rBase, const Pos3f& rTarget);
};  // namespace MR
