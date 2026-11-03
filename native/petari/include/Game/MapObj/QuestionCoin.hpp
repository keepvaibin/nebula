#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class ActorCameraInfo;

class QuestionCoin : public LiveActor {
public:
    QuestionCoin(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void appear();
    virtual void kill();
    virtual void startClipped();
    virtual void endClipped();
    virtual void calcAndSetBaseMtx();
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void initFromJMapInfoArg(const JMapInfoIter&);
    void initShadow();
    void updateActorMtx();
    void exeWait() {
    }

    void exeThrowActor();
    void exeCaught();
    bool isThrowActor() const {
        return 0.0f < mLaunchSpeed;
    }

    TPos3f mBaseMtx;
    LiveActor* mBoundActor;
    TPos3f mActorMtx;
    TQuat4f mLaunchQuat;
    ActorCameraInfo* mCameraInfo;
    f32 mLaunchSpeed;
    bool mIsWeakGravity;
    bool mCalcShadow;
    s32 mPoseType;
    s32 mShadowDropLengthType;
    TVec3f mShadowPos;
    f32 mShadowDropLength;
};
