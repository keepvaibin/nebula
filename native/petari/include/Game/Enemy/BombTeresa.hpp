#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

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
    virtual void init(const JMapInfoIter&);
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
    void initFromJMapParam(const JMapInfoIter&);
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
    bool appearNormal(const TVec3f&, const TVec3f&);
    bool appearShadow(const TVec3f&, const TVec3f&);
    bool rootTongueMtxCallBack(TPos3f*, const JointControllerInfo&);
    bool endTongueMtxCallBack(TPos3f*, const JointControllerInfo&);
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
    TQuat4f _9C;
    TVec3f _AC;
    TVec3f _B8;
    TVec3f _C4;
    TVec3f _D0;
    f32 _DC;
    f32 _E0;
    f32 _E4;
    s32 _E8;
    bool _EC;
    bool mDisableRespawning;
    bool _EE;
    bool _EF;
};
