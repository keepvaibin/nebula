#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;
class PartsModel;

class EarthenPipe : public LiveActor {
public:
    EarthenPipe(const char*);

    virtual void init(const TableIter&) override;
    virtual void calcAnim() override {};
    virtual void makeActorAppeared() override;
    virtual MtxPtr getBaseMtx() const override {
        return mTopJointMtx;
    }
    virtual void control() override;
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*) override;

    bool tryShowUp();
    bool tryHideDown();
    bool isNerveShowUp() const;
    void exeWait();
    void exeReady();
    void exePlayerIn();
    void exeTargetPipeShowUp();
    void exePlayerOut();
    void exeInvalid();
    void exeHide();
    void exeShow();
    void exeWaitToHideDown();
    void exeWaitToShowUp();
    void exeShowUp();
    void exeHideDown();
    void calcTrans(f32 rate);
    void processBgmPlayerIn();
    void processBgmPlayerOut();

    Vec3f mBasePos;
    Vec3f mUp;
    f32 mPipeLength;
    bool mIsIgnoreGravity;
    s32 mPipeMode;
    EarthenPipe* mTargetPipe;
    MtxPtr mTopJointMtx;
    MtxPtr mBottomJointMtx;
    Pos3f mLavaEffectMtx;
    LiveActor* mHostActor;
    Pos3f mPlayerMtx;
    Mtx34f mPlayerInStartMtx;
    Pos3f mPlayerInEndMtx;
    f32 mHorizExitForce;
    f32 mVertExitForce;
    s32 mMusicChangeIdx;
    s32 mMusicState;
    bool mIsRabbitWait;
    PartsModel* mPipeStreamModel;
    bool mIsWaterFlowing;
    ActorCameraInfo* mCameraInfo;
};

class EarthenPipeMediator : public NameObj {
public:
    EarthenPipeMediator();

    struct Entry {
        EarthenPipe* mFirst;
        EarthenPipe* mSecond;
        s32 mPipeID;
    };

    void entry(EarthenPipe*, const TableIter&);

    s32 mNumEntries;
    Entry* mPipeEntries;
    s32 mPipeEntryRoom;
};
