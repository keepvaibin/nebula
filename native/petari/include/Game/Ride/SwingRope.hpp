#pragma once

#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;
class GameTexture;
class SledRopePoint;
class SwingRopePoint;

class SwingRope : public LiveActor {
public:
    SwingRope(const char*);

    virtual void init(const TableIter& rIter);
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
    void restrictPointToTail(s32, const Vec3f*, f32 length = 0.0f);
    void restrictPointToHead(s32, const Vec3f&, f32 length = 0.0f);

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

    void drawStop(TypedDraw& rDraw) const;
    void drawFree(TypedDraw& rDraw) const;

    const Vec3f& getBasePos() const {
        return mBasePos;
    }

    Vec3f mBasePos;
    f32 mRopeLength;
    bool mIsStretched;
    s32 mStretchTime;  // Time since start of stretch condition
    s32 mAccelTime;    // Time since start of accel
    s32 mNumPoints;
    SwingRopePoint** mPoints;
    LiveActor* mRider;
    Pos3f mRideMtx;
    f32 mGrabCoord;
    f32 mGrabPointNum;
    f32 mSlideSpeed;
    SledRopePoint* mSledPoint;
    ActorCameraInfo* mCameraInfo;
    Vec3f mFootPos;
    Vec3f mShadowDropPos;
    s32 mPrevSoundLvl;
};

class SwingRopeGroup : public NameObj {
public:
    SwingRopeGroup(const char*);

    virtual SiteMaterial makeMaterial() const;
    // What its objects draw with.
    SiteMaterial mMaterial;

    GameTexture* mTexture;
};
