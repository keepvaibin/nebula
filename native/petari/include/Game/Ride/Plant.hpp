#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;
class PartsModel;
class PlantStalk;
class PlantLeaf;

class Plant : public LiveActor {
public:
    Plant(const char*);

    virtual void init(const TableIter&) override;
    virtual void initAfterPlacement() override;
    virtual void draw() const override;
    virtual void calcAnim() override;
    virtual void appear() override;
    virtual void startClipped() override;
    virtual void endClipped() override;
    virtual void updateHitSensor(HitSensor* pSensor) override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveOtherMsg(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

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
    Pos3f mSeedMtx;
    PlantStalk* mStalk;
    PartsModel* mTopPartsModel;
    Pos3f mTopMtx;
    s32 mNumLeaves;
    PlantLeaf** mLeaves;
    LiveActor* mRider;
    f32 mRailCoord;
    Vec3f mUp;
    Vec3f mFront;
    f32 mRideVelocity;
    s32 mAccelTimer;
    f32 mJumpSpeed;
    f32 mJumpSpeedOffsetY;
    Vec3f mClippingCenter;
    ActorCameraInfo* mCameraInfo;
    bool mPlayAppearDemo;
    bool mGrabbedTop;
};
