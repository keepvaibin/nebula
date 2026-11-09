#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class AnimScaleController;
class BegomanBaby;
struct JointControllerInfo;
template < typename T >
class JointControlDelegator;
class WalkerStateBindStarPointer;

namespace MR {
    struct ActorMoveParam;
    class FunctorBase;
};  // namespace MR

struct BegomanSound {
    const char* mSound;
};

class BegomanBase : public LiveActor {
public:
    enum Kind {
        Kind_Boss,
        Kind_Spike,
        Kind_Spring,
        Kind_Baby,
    };

    BegomanBase(const char* pName);

    virtual void initAfterPlacement() override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual u32 getKind() const = 0;
    // Returns true when the contact has lasted long enough to knock the begoman away.
    virtual bool onTouchElectric(const Vec3f& rRailPos, const Vec3f& rRailDir);
    virtual bool setNerveReturn() = 0;
    virtual void setNerveLaunch();
    virtual const Nerve* getNerveWait() = 0;
    virtual void addVelocityOnPushedFromElectricRail(const Vec3f& rRailPos, const Vec3f& rRailDir);
    virtual bool requestAttack();
    const BegomanSound* getSoundNormal();
    const BegomanSound* getSoundBoss();

    f32 getRailPushVelHBoss() const;
    f32 getRailPushJumpBoss() const;

    void initCore(const TableIter& rIter, const char* pModelArcName, bool);
    void initEffect(s32 extraEffectNum);
    void initEffectAndStarPointerBind();
    void initShadow(f32 radius, const char* pJointName);
    void initSensor(s32 extraSensorNum, f32 radius, f32 pushedRailRadius, const char* pSensorJointName);
    void initUseSwitchB(const TableIter& rIter, const MR::FunctorBase& rFunctor);

    bool calcJointLocator1(Pos3f* pPos, const JointControllerInfo&);
    void calcAnimCore(Pos3f* pPos);

    bool isPlayerWaking() const;
    void setNerveAttackOr(const Nerve* pAttackNerve, const Nerve* pOtherNerve);

    void exeNoCalcWaitCore(f32 tolerance, const Nerve* pNerve);
    void finishNoCalcWait();
    void exeWaitCore(const MR::ActorMoveParam& rMoveParam, const Nerve* pAttackNerve, const Nerve* pNoAttackNerve, const Nerve* pFarNerve);
    void exeSignAttackCore(const MR::ActorMoveParam& rMoveParam, const Nerve* pNerve);
    void exePursueCore(const MR::ActorMoveParam& rMoveParam, const Nerve* pBrakeNerve, const Nerve* pTurnNerve, const BegomanSound& rSound, f32 speedRate);
    void exeTurnCore(const MR::ActorMoveParam& rMoveParam, const Nerve* pBrakeNerve, const Nerve* pPursueNerve, bool notCheckRail);
    void exeBrakeCore(const Nerve* pNerve);
    void exeStepBackCore(const MR::ActorMoveParam& rMoveParam, const Nerve* pNerve);
    void exeProvokeCore(const MR::ActorMoveParam& rMoveParam, const Nerve* pNerve);
    void exeHitReactionCore(const MR::ActorMoveParam& rMoveParam, const Nerve* pNerve);
    void exeTiredCore(const MR::ActorMoveParam& rMoveParam, const Nerve* pNerve);
    void exeReturnCore(const Nerve* pNerve);
    void exeKeepDistanceCore(const Nerve* pWaitNerve, const Nerve* pAttackNerve, const Nerve* pBrakeNerve, f32 farDist, f32 nearDist);
    void exeLaunch();
    void exeBindStarPointer();
    void finishBindStarPointer();

    // pFront null: the launcher's own front.
    static void launchBegoman(LiveActor* pActor, BegomanBase** pBegomanArray, s32 numBegoman, f32 radius, f32 velH, f32 velV, const Vec3f* pFront);
    static void launchBegomanBabyFromGuarder(LiveActor* pActor, BegomanBaby** pBabyArray, s32 numBegoman, f32 radius, f32 velH, f32 velV,
                                             const Vec3f* pFront);
    // Every entry of pBabyArray is a BegomanBaby.
    static void launchBegomanBabyLauncher(LiveActor* pActor, BegomanBase* const* pBabyArray, s32 numBegoman, f32 radius, f32 velH, f32 velV,
                                          const Vec3f* pFront);

    void updateTargetVec();
    void updateRotateY(f32 speed, f32 tolerance = 0.005f);
    void addVelocityEscapeToSide(f32 velocity);
    void calcDirectionAwayFromRail(Vec3f* pOut, const Vec3f& rRailPos, const Vec3f& rRailDir);
    void pushedFromElectricRail(HitSensor* pSensor, const Vec3f& rRailPos, const Vec3f& rRailDir, f32 awaySpeed, f32 outSpeed, bool limit);
    bool checkTouchElectricRail(bool notCheckPush);

    bool reboundPlaneWithEffect(const Vec3f& rNormal, f32 rate, f32 cosAngleMin, const char* pEffectName);
    void reboundWallAndGround(Vec3f* pFront, bool emitGroundSpark);

    void preventSwingby(f32 damp);
    void preventSlopeBlow(f32 damp);
    // Scales the upward and ground-plane velocity by rate * damp + (1 - rate).
    void dampingVerticalAndParallelVelocity(f32 damp, f32 rate);
    void calcBlowReaction(const Vec3f& rAttackerPos, const Vec3f& rHitPos, f32 blowStrengthParallel, f32 blowStrengthVertical);

    bool isNearInitPos() const;
    bool isFallNextMove(f32 dist, f32 depth);
    bool trySetReturnNerve();
    bool tryAndSetStarPointerBind(const Nerve* pNerve);
    bool incAndCheckTiredCounter();
    bool isInWaterAndSetWaterNerve(const Nerve* pWaterNerve, Pos3f* pPos);

    JointControlDelegator< BegomanBase >* mBaseDelegator;

    Vec3f mFaceVec;
    Vec3f mTargetVec;

    Vec3f mLastGravity;
    Vec3f mAnimScale;

    Quat4f mPoseQuat;
    Quat4f mSpinQuat;

    s32 mTiredCounter;
    // Consecutive frames touching an electric rail.
    s32 mElectricCounter;

    Vec3f mInitPos;

    AnimScaleController* mScaleControler;
    WalkerStateBindStarPointer* mStarPointBind;

    bool mIsTouchElectricRail;
    bool mCanTrySetReturn;
    bool mInvertSideVec;
};

class BegomanAttackPermitter : public LiveActor {
public:
    BegomanAttackPermitter(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;

    bool requestAttack(BegomanBase* pBegoman);

    void exeWait();
    void exeReceive();
    void exePermit();

    BegomanBase* mBegoman;

    f32 mDistToPlayer;

    bool mHasRequest;
};
