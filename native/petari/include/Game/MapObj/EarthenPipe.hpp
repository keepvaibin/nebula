#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class ActorCameraInfo;
class PartsModel;

class EarthenPipe : public LiveActor {
public:
    EarthenPipe(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void calcAnim() {};
    virtual void makeActorAppeared();
    virtual MtxPtr getBaseMtx() const {
        return mTopJointMtx;
    }
    virtual void control();
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

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
    void calcTrans(f32);
    void processBgmPlayerIn();
    void processBgmPlayerOut();

    TVec3f _8C;
    TVec3f _98;
    f32 _A4;
    bool mIsIgnoreGravity;
    bool _A9;
    bool _AA;
    bool _AB;
    s32 mPipeMode;
    EarthenPipe* _B0;
    MtxPtr mTopJointMtx;
    MtxPtr mBottomJointMtx;
    TPos3f _BC;
    LiveActor* mHostActor;
    TPos3f _F0;
    TMtx34f _120;
    TPos3f _150;
    f32 _180;
    f32 mHorizExitForce;
    f32 mVertExitForce;
    s32 mMusicChangeIdx;
    s32 mMusicState;
    u8 _194;
    u8 _195;
    u8 _196;
    u8 _197;
    PartsModel* mPipeStreamModel;
    bool _19C;
    bool _19D;
    bool _19E;
    bool _19F;
    ActorCameraInfo* mCameraInfo;
};

class EarthenPipeMediator : public NameObj {
public:
    EarthenPipeMediator();

    struct Entry {
        EarthenPipe* _0;
        EarthenPipe* _4;
        s32 mPipeID;
    };

    void entry(EarthenPipe*, const JMapInfoIter&);

    s32 mNumEntries;
    Entry* mPipeEntries;
};
