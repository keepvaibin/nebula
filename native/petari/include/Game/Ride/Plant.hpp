#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class ActorCameraInfo;
class J3DShapeDraw;
class PartsModel;
class PlantStalk;
class PlantLeaf;

class Plant : public LiveActor {
public:
    Plant(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void initAfterPlacement();
    virtual void draw() const;
    virtual void calcAnim();
    virtual void appear();
    virtual void startClipped();
    virtual void endClipped();
    virtual void updateHitSensor(HitSensor* pSensor);
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeWaitFar();
    void exeSeedWait();
    void exeWaitDemoWaitGrowUp();
    void exeDemoWaitGrowUp();
    void exeGrowUp();
    void exeHangWaitGrowUp();
    void exeHangUpGrowUp();
    void exeGrowthStop();
    void exeGrowthWait();
    void exeGrowthWaitInvalid();
    void exeHangStart();
    void exeHangUp();
    void exeHangDown();

    void initLeaf();
    void updateTopMtx();
    bool updateGrowUp();
    bool updateHangUp(f32);
    void updateBindLeaf();

    void startGrowUp();

    bool tryHangUp();
    bool tryReachGoal();
    bool tryAccelHangUp();

    PartsModel* mSeedPartsModel;
    TPos3f mSeedMtx;
    PlantStalk* mStalk;
    PartsModel* mTopPartsModel;
    TPos3f mTopMtx;
    s32 mNumLeaves;
    PlantLeaf** mLeaves;
    J3DShapeDraw* mShapeDraw;
    LiveActor* mRider;
    f32 mRailCoord;
    TVec3f mUp;
    TVec3f mFront;
    f32 mRideVelocity;
    s32 mAccelTimer;
    f32 mJumpSpeed;
    f32 mJumpSpeedOffsetY;
    TVec3f mClippingCenter;
    ActorCameraInfo* mCameraInfo;
    bool mPlayAppearDemo;
    bool mGrabbedTop;
};
