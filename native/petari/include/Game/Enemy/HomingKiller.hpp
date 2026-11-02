#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class ModelObj;
class PartsModel;

class HomingKiller : public LiveActor {
public:
    enum Type { Type_HomingKiller = 0, Type_Torpedo = 1, Type_MagnumKiller = 2 };

    HomingKiller(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void appear();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgEnemyAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void appear(const TVec3f&, const TVec3f&);
    bool isMoveStart() const;
    void setChaseStartEndDistance(f32, f32);
    void initMapToolInfo(const JMapInfoIter&);
    void calcInitPosture();
    void updateVelocity();
    void updateRotateZ(const TVec3f&);
    bool processMove();
    bool processChase();

    bool isChaseStart() const;
    bool tryChaseStart();
    bool tryFreeze(const Nerve*);
    bool tryBindedBreak();
    bool tryToExplosion(HitSensor* pSender, HitSensor* pReceiver);
    void sendMsgExplosionToNearActor();

    bool isUpdateChaseFrontVec(const TVec3f&) const;
    void calcFrontVecToTarget(TVec3f*) const;

    bool isValidShowModel() const;
    bool isWaterBreak() const;

    void updateBaseMtxNoRotateZ();
    void startMoveLevelSound(bool);
    void setBckRate(f32, bool);
    bool isChasing() const;

    void exeAppear();
    void exeMoveStart();
    void exeMove();
    void exeChaseStart();
    void exeChase();
    void exeFreeze();
    void exeBreak();
    void exeGoToTarget();

    inline f32 getScale() {
        return mType == Type_MagnumKiller ? 4.0f : 1.0f;
    }

    void setBasePosAndFront(const TVec3f& pos, const TVec3f& front) {
        mBasePos.set(pos);
        mBaseFront.set(front);
    }

    bool isInactive() const;
    bool isMoveNormal() const;
    bool isGravityIgnored() const;

    Type mType;
    f32 mChaseStartDist;
    f32 mChaseEndDist;
    f32 mChaseRotateSpeed;
    f32 mChaseStartAngle;
    TVec3f mFront;
    TVec3f mUp;
    TVec3f mBasePos;
    TVec3f mBaseFront;
    TVec3f mBaseUp;
    TPos3f mBaseMtx;
    s32 mFreezeTime;
    TVec3f mFreezePos;
    const Nerve* mUnfreezeNerve;
    s32 mChaseInvalidTime;
    s32 mMoveTime;
    TPos3f mEffectMtx;
    HitSensor* mTargetSensor;
    bool mDisableChase;
    bool mIsLinearShot;       // if set, ignore gravity and disable chase
    bool mUseFullSightAngle;  // if set, ignore gravity but use full sight angle to start chase
                                          // In game, this seems to only be used in Drip Drop for the Torpedos
    PartsModel* mPropeller;
    ModelObj* mTorpedoLight;
};

class HomingKillerLauncher : public LiveActor {
public:
    HomingKillerLauncher(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void appear();
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeAppearKiller();
    void exeDeadKiller();

    HomingKiller* mKiller;
};
