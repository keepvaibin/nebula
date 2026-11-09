#pragma once

#include "Game/Render/TypedDraw.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;
class GameTexture;
class PartsModel;

class CreeperPoint {
public:
    CreeperPoint(const Vec3f&, const Vec3f&, const CreeperPoint*);

    void updateFree();
    void updateBend(bool, const Vec3f&, f32, f32);
    void updateLocalAxis();

    const Vec3f& getPosition() const {
        return mPosition;
    }

    const Vec3f& getSide() const {
        return mSide;
    }

    const Vec3f& getFront() const {
        return mFront;
    }

    Vec3f mPosition;
    Vec3f mNeutralPos;
    Vec3f mVelocity;
    Vec3f mSide;
    Vec3f mUp;
    Vec3f mFront;
    Vec3f mProjection;
    const CreeperPoint* mPrevPoint;
};

class Creeper : public LiveActor {
public:
    Creeper(const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual void draw() const override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

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

    void calcAndGetCurrentInfo(Vec3f*, Vec3f*) const;

    SiteMaterial loadMaterial() const;
    const CreeperPoint* getHeadPoint() const {
        return getPoint(mNumPoints - 1);
    }

    inline const CreeperPoint* getPoint(s32 idx) const {
        return mPoints[idx];
    }

    s32 mNumPoints;
    CreeperPoint** mPoints;
    LiveActor* mRider;
    Vec3f mFront;
    Vec3f mUp;
    f32 mHangSpeed;
    f32 mCoord;
    f32 mBendRate;
    f32 mBendRateBee;
    Vec3f mBendDirection;
    f32 mJumpSpeedFront;
    f32 mJumpSpeedUp;
    ActorCameraInfo* mCameraInfo;
    Vec3f mClippingCenter;
    GameTexture* mTexture;
    PartsModel* mFlowerModel;
    PartsModel* mLeafModel;
    Pos3f mTopMtx;
};
