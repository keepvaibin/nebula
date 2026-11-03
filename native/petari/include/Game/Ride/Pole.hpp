#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class ActorCameraInfo;

class Pole : public LiveActor {
public:
    Pole(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void appear();
    virtual void calcAnim();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);
    virtual MtxPtr getBaseMtx() const {
        if (mNoModel) {
            return (MtxPtr)&mBaseMtx;
        } else {
            return LiveActor::getBaseMtx();
        }
    }

    void exeDemoAppear();
    void exeFree();
    void exeFreeInvalid();
    void exeBindStart();
    void exeBindWait();
    void exeBindTurn();
    void exeBindClimbUp();
    void exeBindFallDown();
    void exeBindHandstandStart();
    void exeBindHandstandWait();
    void exeBindHandstandEnd();
    void exeBindHandstandTurn();
    bool tryJump(bool, f32);
    bool tryTurn();
    bool tryClimbUp();
    bool tryFallDown();
    bool tryHandstandTurn();
    bool isEnableTurn() const;
    void updateBindTrans();
    void calcGravityMtx(TPos3f*);
    void updateTopPos(f32);
    f32 getPoleMoveStickX() const;
    f32 getPoleMoveStickY() const;

    TVec3f mBasePos;
    TVec3f mTopPos;
    f32 mHeight;
    bool mDisableHandstand;
    bool mIsSquare;
    bool mNoModel;
    bool mInvertStick;
    bool mUsePole9mCollision;
    f32 mClimbCoord;
    f32 mClimbSpeed;
    LiveActor* mRider;
    ActorCameraInfo* mCameraInfo;
    TVec3f mCenterPos;
    TVec3f mSide;
    TVec3f mUp;
    TVec3f mFront;
    TPos3f mPosMtx;
    MtxPtr mTopMtx;
    MtxPtr mBottomMtx;
    TMtx34f mBaseMtx;
};
