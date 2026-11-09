#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;

class Pole : public LiveActor {
public:
    Pole(const char*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void calcAnim() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;
    virtual bool receiveMsgEnemyAttack(u32, HitSensor*, HitSensor*) override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;
    virtual MtxPtr getBaseMtx() const override {
        if (mNoModel) {
            return const_cast< MtxPtr >(mBaseMtx.mMtx);
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
    void calcGravityMtx(Pos3f*);
    void updateTopPos(f32);
    f32 getPoleMoveStickX() const;
    f32 getPoleMoveStickY() const;

    Vec3f mBasePos;
    Vec3f mTopPos;
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
    Vec3f mCenterPos;
    Vec3f mSide;
    Vec3f mUp;
    Vec3f mFront;
    Pos3f mPosMtx;
    MtxPtr mTopMtx;
    MtxPtr mBottomMtx;
    Mtx34f mBaseMtx;
};
