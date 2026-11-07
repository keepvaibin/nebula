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

    virtual ~BombTeresa() {
    }
    virtual void init(const TableIter&);
    virtual void initAfterPlacement();
    virtual void makeActorAppeared();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPush(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

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
    bool tryCheseEnd();
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
    void addTeresaSpinPullVelocity(f32);

    inline bool isOutOfRange(f32 dist) {
        return !MR::isNearPlayer(this, dist);
    }

    JointControlDelegator< BombTeresa >* mJointDelegator;
    JointControlDelegator< BombTeresa >* mJointDelegator2;
    AnimScaleController* mScaleController;
    WalkerStateBindStarPointer* mBindStarPointer;
    Quat4f _9C;
    Vec3f _AC;
    Vec3f _B8;
    Vec3f _C4;
    Vec3f _D0;
    f32 _DC;
    f32 _E0;
    f32 _E4;
    s32 _E8;
    bool _EC;
    bool mDisableRespawning;
    bool _EE;
    bool _EF;
};
