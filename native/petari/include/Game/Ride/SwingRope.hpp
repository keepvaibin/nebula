#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class ActorCameraInfo;
class JUTTexture;
class SledRopePoint;
class SwingRopePoint;

class SwingRope : public LiveActor {
public:
    SwingRope(const char*);

    virtual void init(const JMapInfoIter& rIter);
    virtual void draw() const;
    virtual void updateHitSensor(HitSensor* pSensor);
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeStop();
    void exeFree();
    void exeFreeInvalid();
    void exeBindSlideDownStart();
    void exeBindSlideDown();
    void exeBindStretch();
    void exeBindLoose();

    void initPoints();

    f32 calcPointNo(f32) const;
    f32 calcFriction(s32) const;

    void addPointGravity();
    void restrictPointToTail(s32, const TVec3f*, f32 length = 0.0f);
    void restrictPointToHead(s32, const TVec3f&, f32 length = 0.0f);

    bool isAllPointsStop() const;
    bool isStretched() const;
    bool tryJump();
    void updateHangPoint();
    void updateRideMtx();
    void updateFootPos();
    bool updateStretch();
    bool updateLoose();
    void updateLooseUpperPoints();
    bool updateSlideDown();
    void updateStretchHangUpperPoints();
    void updateHangLowerPoints();
    void updateHangLowerPointPos(s32);

    void drawStop() const;
    void drawFree() const;
    void drawBind() const;

    const TVec3f& getBasePos() const {
        return mBasePos;
    }

    TVec3f mBasePos;
    f32 mRopeLength;
    bool mIsStretched;
    s32 mStretchTime;  // Time since start of stretch condition
    s32 mAccelTime;    // Time since start of accel
    s32 mNumPoints;
    SwingRopePoint** mPoints;
    LiveActor* mRider;
    TPos3f mRideMtx;
    f32 mGrabCoord;
    f32 mGrabPointNum;
    f32 mSlideSpeed;
    SledRopePoint* mSledPoint;
    ActorCameraInfo* mCameraInfo;
    f32 mFootCoord;
    f32 mFootPointNum;
    f32 mGrabToFootDist;
    TVec3f mFootPos;
    TVec3f mShadowDropPos;
    f32 mShadowDropOffset;
    s32 mPrevSoundLvl;
};

class SwingRopeGroup : public NameObj {
public:
    SwingRopeGroup(const char*);

    virtual void initDraw() const;

    JUTTexture* mTexture;
};
