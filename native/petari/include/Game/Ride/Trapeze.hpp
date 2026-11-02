#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class ActorCameraInfo;
class JUTTexture;
class PartsModel;
class SwingRopePoint;

class Trapeze : public LiveActor {
public:
    Trapeze(const char*);

    virtual void init(const JMapInfoIter& rIter);
    virtual void draw() const;
    virtual void updateHitSensor(HitSensor* pSensor);
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeStop();
    virtual void exeFree();
    void exeFreeInvalid();
    void exeSwingWait();
    void exeSwingSlideDownStart();
    void exeSwingSlideDown();
    void exeSwingFrontStart();
    void exeSwingFrontEnd();
    void exeSwingBackStart();
    void exeSwingBackEnd();

    bool tryJump();

    void updateStick(const TVec3f&, f32);
    void updateStickMtx();
    void updateHangPoint();
    bool updateBind();
    bool updateSwing();
    bool updateSlideDown();

    bool isStartSwingFront() const;
    bool isStartSwingBack() const;

    void drawRope(const TVec3f&, const TVec3f&, const TVec3f&, const TVec3f&, f32, f32) const;

    TVec3f mSide;
    TVec3f mUp;
    TVec3f mFront;
    f32 mRopeLength;
    SwingRopePoint* mStickPoint;
    PartsModel* mTrapezeModel;
    TPos3f mPosMtx;
    LiveActor* mRider;
    SwingRopePoint* mHangPoint;
    f32 mGrabCoord;
    f32 mSwingVel;
    bool mSwingReverse;
    bool mIsSwingFront;
    bool mIsSwingBack;
    bool mWasSwingFront;
    bool mWasSwingBack;
    ActorCameraInfo* mCameraInfo;
    s32 mPrevSoundLvl;
};

class TrapezeRopeDrawInit : public NameObj {
public:
    TrapezeRopeDrawInit(const char*);

    virtual void initDraw() const;

    JUTTexture* mTexture;
};

// these have to be header bodies because for some reason they are actually linked in NameObjFactory
namespace NrvTrapeze {
    NEW_NERVE(TrapezeNrvStop, Trapeze, Stop);
    NEW_NERVE(TrapezeNrvFree, Trapeze, Free);
    NEW_NERVE(TrapezeNrvFreeInvalid, Trapeze, FreeInvalid);
    NEW_NERVE(TrapezeNrvSwingWait, Trapeze, SwingWait);
    NEW_NERVE(TrapezeNrvSwingSlideDownStart, Trapeze, SwingSlideDownStart);
    NEW_NERVE(TrapezeNrvSwingSlideDown, Trapeze, SwingSlideDown);
    NEW_NERVE(TrapezeNrvSwingFrontStart, Trapeze, SwingFrontStart);
    NEW_NERVE(TrapezeNrvSwingFrontEnd, Trapeze, SwingFrontEnd);
    NEW_NERVE(TrapezeNrvSwingBackStart, Trapeze, SwingBackStart);
    NEW_NERVE(TrapezeNrvSwingBackEnd, Trapeze, SwingBackEnd);
    NEW_NERVE(TrapezeNrvSlowSwingFrontStart, Trapeze, SwingFrontStart);
    NEW_NERVE(TrapezeNrvSlowSwingFrontEnd, Trapeze, SwingFrontEnd);
    NEW_NERVE(TrapezeNrvSlowSwingBackStart, Trapeze, SwingBackStart);
    NEW_NERVE(TrapezeNrvSlowSwingBackEnd, Trapeze, SwingBackEnd);
};  // namespace NrvTrapeze
