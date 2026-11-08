#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class Triangle;
class AnimScaleController;
class WalkerStateBindStarPointer;
class ParabolicPath;

class Meramera : public LiveActor {
public:
    Meramera(const char*);

    virtual void init(const TableIter&);
    virtual void makeActorAppeared();
    virtual void kill();
    virtual void startClipped();
    virtual void endClipped();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPush(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void initElementType(const TableIter&);
    void initEffect();
    void initSensor();
    void initBind();
    void initShadow();
    void initAppearState(const TableIter&);
    bool requestDamage(HitSensor*, HitSensor*);
    bool requestFire(HitSensor*, HitSensor*);
    bool requestForceRunaway();
    bool requestDownForce(HitSensor*, HitSensor*);
    bool requestDown(HitSensor*, HitSensor*);
    bool requestAttack(HitSensor*, HitSensor*);
    bool requestFlatDown(HitSensor*, HitSensor*);
    bool sendMsgElementAttack(HitSensor*, HitSensor*);
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
    void endRunaway();
    void exeStartDiving();
    void endStartDiving();
    void exeDiving();
    void exeSink();
    void exeFloat();
    void exeIgnition();
    void exeIgnitionForce();
    void exeAttackSuccess();
    void exeDown();
    void exeFlatDown();

    void exeBindStarPointer();
    void endBindStarPointer();
    void exeBindStarPointerNoFire();
    void endBindStarPointerNoFire();
    void exeReadyRestart();

    bool isAffectedSpin() const;
    bool isEnableAttack() const;
    bool isPushMoved() const;
    bool isEnableFireball() const;
    bool isEnableChase() const;
    void addToTargetMovingAccel(const Vec3f&, f32, f32);
    void addMovingAccel(const Vec3f&, f32, f32);
    void addRunawayJumpPower();
    void addOverWallAccel(const Vec3f&);
    void updatePose();
    void updateExtinguishMtx();
    bool checkDirectDivingPoint();
    bool checkDivingPoint(f32, f32);
    bool checkDivingPointMore(f32);
    bool isHitRecover() const;
    bool findDivingPoint(Vec3f, const Vec3f&);
    void resetAppear();
    void emitEffectHead(s32);
    void deleteEffectHead(bool);
    void emitEffectHeatBody();
    void emitEffectCoolDownBody();
    void emitEffectColdBody();
    void turnFireDirectionToSpin(f32);
    void startWaitLevelSound();
    void startChaseLevelSound();

    inline bool isSensor(const HitSensor*, const char*) const;
    inline bool isActionDisabled() const;
    inline Vec3f getDistanceToPlayer() const;

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
    f32 mTargetDistance;
    f32 mChaseDistance;
    s32 mAppearStatus;  // Obj_arg0
    s32 mElementType;
    s32 mEffectType;
    s32 mBodyEffectType;
    s32 mRunawayTimer;
    bool mCanDive;
    bool mIsValidRestart;  // Obj_arg2
};
