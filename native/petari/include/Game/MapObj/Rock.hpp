#pragma once

#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include <nebula_geometry.hpp>

class ModelObj;
class RockCreator;

class Rock : public LiveActor {
public:
    Rock(f32 moveSpeed = 10.0f, const char* pName = "ゴロゴロ岩");

    enum Type { NormalRock = 0, WanwanRolling = 1, WanwanRollingMini = 2, WanwanRollingGold = 3 };

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void kill() override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;

    static s32 getAppearFrame();
    static Type getType(const TableIter&);
    static s32 getAppearStarPieceNum(Type);
    void initMapToolInfo(const TableIter&);
    void initModel();
    void initSensor();
    void initEffect();
    bool isBreakByWall() const;
    bool isBodySensor(HitSensor* pSensor) const;
    bool move(f32);
    void calcBaseMtx(Pos3f*) const;
    bool isInClippingRange() const;
    void startSoundWanwanVoice();
    void startRollLevelSound(bool);
    bool tryFreeze(const Nerve*);
    void setBtkForEnvironmentMap(LiveActor*, const char*);
    void rumblePadAndCamera();
    void setNerveBreak(bool);
    void updateRotateX(f32);
    void appearStarPiece();
    void moveOnRail(f32, f32, bool);
    bool isForceInvalidBindSection() const;
    bool tryBreakReachedGoal();
    bool isMoveEnabled() const;

    void exeAppear();
    void exeAppearMoveInvalidBind();
    void exeMove();
    void exeMoveInvalidBind();
    void exeBreak();
    void exeFreeze();

    inline f32 getRadius() const {
        return mRockType == WanwanRollingMini ? 0.3f : mScale.x;
    }

    inline f32 getAngle() const {
        f32 rot = mRotation.x;
        return rot * PI_180;
    }

    inline bool isRockSensor(HitSensor* pSensor) const {
        return pSensor->isType(ATYPE_ROCK) || pSensor->isType(ATYPE_WANWAN);
    }

    RockCreator* mCreator;
    Type mRockType;
    ModelObj* mBreakModel;
    f32 mMoveSpeed;
    bool mBreakModelOnRailGoal;
    bool mSlowDownOnAttack;
    Vec3f mAppearPos;
    f32 mRadius;
    f32 mRotateSpeed;
    Pos3f mBaseMtx;
    Vec3f mFallVelocity;
    f32 mGravityRate;
    s32 mCurrentRailPoint;
    Vec3f mPrevPos;
    Vec3f mFront;
    u32 mAirTime;
    bool mIsNormalGravity;
    s32 mWanwanVoiceTimer;
    s32 mFreezeTime;
    Vec3f mFreezePos;
    const Nerve* mUnfreezeNerve;
    s32 mInvalidBindTime;
    s32 mAppearTime;
    f32 mAppearAngle;
    s32 mRollSoundTimer;
    s32 mSlowDownTimer;
};
