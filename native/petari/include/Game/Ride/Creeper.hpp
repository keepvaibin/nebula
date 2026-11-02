#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class ActorCameraInfo;
class JUTTexture;
class PartsModel;

class CreeperPoint {
public:
    CreeperPoint(const TVec3f&, const TVec3f&, const CreeperPoint*);

    void updateFree();
    void updateBend(bool, const TVec3f&, f32, f32);
    void updateLocalAxis();

    const TVec3f& getPosition() const {
        return mPosition;
    }

    const TVec3f& getSide() const {
        return mSide;
    }

    const TVec3f& getUp() const {
        return mUp;
    }

    const TVec3f& getFront() const {
        return mFront;
    }

    TVec3f mPosition;
    TVec3f mNeutralPos;
    TVec3f mVelocity;
    TVec3f mSide;
    TVec3f mUp;
    TVec3f mFront;
    TVec3f mProjection;
    const CreeperPoint* mPrevPoint;
};

class Creeper : public LiveActor {
public:
    Creeper(const char*);

    virtual ~Creeper();

    virtual void init(const JMapInfoIter&);
    virtual void control();
    virtual void draw() const;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeFree();
    void exeFreeInvalid();
    void exeHangStart();
    void exeHangUp();
    void exeHangDown();

    void initPoints();
    void updatePoints();
    void bendPoints();

    bool tryHangUp();
    bool tryJump();
    bool updateHangUp(f32);

    void calcAndGetCurrentInfo(TVec3f*, TVec3f*) const;

    void loadMaterial() const;
    const CreeperPoint* getHeadPoint() const {
        return getPoint(mNumPoints - 1);
    }

    inline const CreeperPoint* getPoint(s32 idx) const {
        return mPoints[idx];
    }

    s32 mNumPoints;
    CreeperPoint** mPoints;
    LiveActor* mRider;
    TVec3f mFront;
    TVec3f mUp;
    f32 mHangSpeed;
    f32 mCoord;
    f32 mBendRate;
    f32 mBendRateBee;
    TVec3f mBendDirection;
    f32 mJumpSpeedFront;
    f32 mJumpSpeedUp;
    ActorCameraInfo* mCameraInfo;
    TVec3f mClippingCenter;
    JUTTexture* mTexture;
    PartsModel* mFlowerModel;
    PartsModel* mLeafModel;
    TPos3f mTopMtx;
};
