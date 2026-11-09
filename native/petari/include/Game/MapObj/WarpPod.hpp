#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class WarpPod;
class ActorCameraInfo;
class PlacementId;
class LiveActorGroup;
class GameTexture;

class WarpPodMgr : public NameObj {
public:
    WarpPodMgr(const char* pName);

    virtual void draw() const override;

    WarpPod* getPairPod(const LiveActor*);
    void startEventCamera(const LiveActor*);
    void endEventCamera();
    void notifyWarpEnd(WarpPod*);

    const LiveActor* mCameraPod;
    LiveActorGroup* mPods;
    s32 mNextPairIndex;
};

class WarpPod : public LiveActor {
public:
    WarpPod(const char* pName) : LiveActor(pName), mPlacementId(nullptr) {
    }

    virtual void init(const TableIter& rIter) override;
    virtual void movement() override;
    virtual void appear() override;
    virtual void control() override;
    virtual void attackSensor(HitSensor*, HitSensor*) override;

    void glowEffect();
    void initPair();
    void appearWithDemo();
    void startEventCamera() const;
    void endEventCamera();
    void initDraw();
    // The tube's segments take the draw numbers `firstPart` + the point's index, whatever segments are left out.
    void drawCylinder(u32 firstPart) const;

    // Opens by the saved warp pod path flag (arg 3 = 0).
    bool isSavedPath() const {
        return mPathOpenType == 0;
    }

    PlacementId* mPlacementId;
    s32 mGroupId;
    ActorCameraInfo* mCamInfo;
    char* mEventCameraName;
    char* mPairCameraName;
    s32 mDelay;
    s32 mCameraStep;
    s32 mVisibilityState;
    s32 mArg2;
    s32 mPathOpenType;
    s32 mPathFlagIndex;
    s32 mGrandstarReq;
    s32 mCameraTime;
    s32 mGlowColorIndex;
    Vec3f* mPathPoints;
    u32 mNumPathPoints;
    bool mDrawsPath;
    bool mIsInactive;
    bool mIsPairCameraOn;
    bool mIsAppearDemoPending;
    WarpPod* mPairPod;
    GameTexture* mGlowTexture;
};

namespace MR {
    WarpPodMgr* getWarpPodManager();
};  // namespace MR
