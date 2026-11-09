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

    virtual void draw() const;

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

    virtual void init(const TableIter& rIter);
    virtual void movement();
    virtual void appear();
    virtual void control();
    virtual void attackSensor(HitSensor*, HitSensor*);

    void glowEffect();
    void initPair();
    void appearWithDemo();
    void startEventCamera() const;
    void endEventCamera();
    void initDraw();
    void drawCylinder() const;

    // Opens by the saved warp pod path flag (arg 3 = 0).
    bool isSavedPath() const {
        return mPathOpenType == 0;
    }

    PlacementId* mPlacementId;
    s32 mGroupId;
    ActorCameraInfo* mCamInfo;
    char* mEventCameraName;
    char* mPairCameraName;
    u16 mDelay;
    u16 mCameraStep;
    s32 mVisibilityState;
    s32 mArg2;
    s32 mPathOpenType;
    s32 mPathFlagIndex;
    s32 mGrandstarReq;
    s32 mCameraTime;
    s32 mGlowColorIndex;
    Vec3f* mPathPoints;
    u16 mNumPathPoints;
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
