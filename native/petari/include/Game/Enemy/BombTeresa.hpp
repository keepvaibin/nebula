#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include <nebula_geometry.hpp>

class AnimScaleController;
template < typename T >
class JointControlDelegator;
struct JointControllerInfo;
class WalkerStateBindStarPointer;

class BombTeresa : public LiveActor {
public:
    BombTeresa(const char*);

    virtual ~BombTeresa() override {
    }
    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void makeActorAppeared() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPush(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    void initTongue();
    void initSensor();
    void initFromPlacementParam(const TableIter&);
    void exeAppear();
    void exeShadowAppear();
    void exeBallAppear();
    void exeWait();
    void exeWander();
    void exeChase();
    void exeAttackTongueFailed();
    void exeDirectTackleSign();
    void exeDirectTackle();
    void exeDrift();
    void exeDriftRelease();
    void exeBindStarPointer();
    void endBindStarPointer();
    void exeExplosion();
    void exeShock();
    void exeDisperse();
    void exeReadyRestart();
    bool tryChaseEnd();
    bool tryDirectTackle();
    bool tryAbortDrift();
    bool tryDriftRelease();
    bool tryDriftReleaseEnd();
    bool tryExplosion();
    bool tryRevival();
    bool requestDrift();
    bool requestDisperse();
    bool appearNormal(const Vec3f&, const Vec3f&);
    bool appearShadow(const Vec3f&, const Vec3f&);
    bool rootTongueMtxCallBack(Pos3f*, const JointControllerInfo&);
    bool endTongueMtxCallBack(Pos3f*, const JointControllerInfo&);
    void updateNormalVelocity();
    void updateDriftReleaseVelocity();
    bool isTouchTongue() const;
    bool isEnableDrift() const;
    bool isEnablePointBind() const;
    bool isEnableDisperse() const;
    bool isEnableHitExplosionToPlayer() const;
    bool isEnableHitExplosionToEnemy() const;
    bool isEnableHitExplosion() const;
    bool isEnableStarPieceAttack() const;
    bool isEnableShockWave() const;
    void addTeresaSpinPullVelocity(f32 height);

    inline bool isOutOfRange(f32 dist) {
        return !MR::isNearPlayer(this, dist);
    }

    JointControlDelegator< BombTeresa >* mJointDelegator;
    JointControlDelegator< BombTeresa >* mJointDelegator2;
    AnimScaleController* mScaleController;
    WalkerStateBindStarPointer* mBindStarPointer;
    Quat4f mPoseQuat;
    Vec3f mFront;
    Vec3f mTonguePos;
    Vec3f mInitPos;
    f32 mTongueRate;
    f32 mDriftPullSpeed;
    f32 mDriftSpinSpeed;
    s32 mDriftStep;
    bool mIsDriftRelease;
    bool mDisableRespawning;
    bool mIsWanderAfterWait;  // Polta's (BombTeresaHolder): out of the player's reach it wanders after 60 steps of waiting
};
