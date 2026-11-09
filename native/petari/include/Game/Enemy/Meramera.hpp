#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class Triangle;
class AnimScaleController;
class WalkerStateBindStarPointer;
class ParabolicPath;

class Meramera : public LiveActor {
public:
    Meramera(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual void kill() override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPush(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

    void initElementType(const TableIter& rIter);
    void initEffect();
    void initSensor();
    void initBind();
    void initShadow();
    void initAppearState(const TableIter& rIter);
    bool requestDamage(HitSensor* pSender, HitSensor* pReceiver);
    bool requestFire(HitSensor* pSender, HitSensor* pReceiver);
    bool requestForceRunaway();
    bool requestDownForce(HitSensor* pSender, HitSensor* pReceiver);
    bool requestDown(HitSensor* pSender, HitSensor* pReceiver);
    bool requestAttack(HitSensor* pSender, HitSensor* pReceiver);
    bool requestFlatDown(HitSensor* pSender, HitSensor* pReceiver);
    bool sendMsgElementAttack(HitSensor* pSender, HitSensor* pReceiver);
    bool tryWalk();
    bool tryWalkEnd();
    bool tryEndShrink();
    bool tryChase();
    bool tryChaseStartToDash();
    bool tryChaseDashToTurn();
    bool tryChaseTurnToDash();
    bool tryEndChase();
    bool tryRunaway();
    bool tryStartDiving();
    bool tryDiving();
    bool tryEndDiving();
    bool tryFloat();
    bool tryEndFloat();
    bool tryEndIgnition();
    bool tryForceSink();
    bool tryRecovery();
    bool tryEndAttackSuccess();
    bool tryPointBind();

    void exeWait();
    void exeWalk();
    void exeShrink();
    void exeChaseStart();
    void exeChaseDash();
    void exeChaseTurn();
    void exeDamage();
    void exeRunaway();
    // Also ends StartDiving.
    void endRunaway();
    void exeStartDiving();
    void exeDiving();
    void exeSink();
    void exeFloat();
    void exeIgnition();
    void exeIgnitionForce();
    void exeAttackSuccess();
    void exeDown();
    void exeFlatDown();

    void exeBindStarPointer();
    // Also ends BindStarPointerNoFire.
    void endBindStarPointer();
    void exeBindStarPointerNoFire();
    void exeReadyRestart();

    bool isAffectedSpin() const;
    bool isEnableAttack() const;
    bool isPushMoved() const;
    bool isEnableFireball() const;
    bool isEnableChase() const;
    void addToTargetMovingAccel(const Vec3f& rTarget, f32 accel, f32 turnCos);
    void addMovingAccel(const Vec3f& rDir, f32 accel, f32 turnCos);
    void addRunawayJumpPower();
    void addOverWallAccel(const Vec3f& rDir);
    void updatePose();
    void updateExtinguishMtx();
    bool checkDirectDivingPoint();
    bool checkDivingPoint(f32 dist, f32 angleDegree);
    bool checkDivingPointMore(f32 dist);
    // On lava (fire) or water (ice).
    bool isHitRecover() const;
    bool findDivingPoint(Vec3f start, const Vec3f& rProbe);
    void resetAppear();
    void emitEffectHead(s32 effectType);
    void deleteEffectHead(bool useForce);
    void startBodyEffect(s32 bodyEffectType, const char* pAnimName);
    void emitEffectHeatBody();
    void emitEffectCoolDownBody();
    void emitEffectColdBody();
    void turnFireDirectionToSpin(f32 degree);
    void startWaitLevelSound();
    void startChaseLevelSound();

    inline bool isSensor(const HitSensor* pSensor, const char* pName) const;
    inline bool isActionDisabled() const;
    inline Vec3f calcVecToPlayer() const;

    AnimScaleController* mAnimScaleController;
    WalkerStateBindStarPointer* mWalkerStateBindStarPointer;
    ParabolicPath* mParabolicPath;
    Pos3f mEffectMtx;
    Pos3f mExtinguishMtx;
    Pos3f mFallEffectMtx;
    Quat4f mPoseQuat;
    Vec3f mFront;
    Vec3f mFireUp;
    Vec3f mHomePosition;
    Vec3f mDashDir;
    Vec3f mPlayerPosition;
    Vec3f mWalkTarget;
    Vec3f mDivePos;
    Vec3f mDiveDir;
    Vec3f mDiveUp;
    // Ground-plane distance to the last move target; -1 before any.
    f32 mTargetDistance;
    f32 mChaseDistance;
    // Placement arg0: Status_Runaway, Status_Float, or -1 for wait.
    s32 mAppearStatus;
    s32 mElementType;
    s32 mEffectType;
    s32 mBodyEffectType;
    s32 mRunawayTimer;
    bool mCanDive;
    // Placement arg2: respawn at home after a kill.
    bool mIsValidRestart;
};
