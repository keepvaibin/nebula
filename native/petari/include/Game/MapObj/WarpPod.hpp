#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class WarpPod;
class ActorCameraInfo;
class PlacementId;
class LiveActorGroup;
class JUTTexture;

class WarpPodMgr : public NameObj {
public:
    /// @brief Creates a new `WarpPodMgr`.
    /// @param pName A pointer to the null-terminated name of the object.
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
    /// @brief Creates a new `WarpPod`.
    /// @param pName A pointer to the null-terminated name of the object.
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
    void drawCylinder(u32) const;

    PlacementId* mPlacementId;
    s32 mGroupId;
    ActorCameraInfo* mCamInfo;
    char* mEventCameraName;
    char* mPairCameraName;
    u16 mDelay;
    u16 mCameraStep;
    s32 mVisibilityState;
    s32 mArg2;
    s32 mArg3;
    s32 mPathFlagIndex;
    s32 mGrandstarReq;
    s32 mCameraTime;
    s32 mGlowColorIndex;
    Vec3f* mPathPoints;
    u16 mNumPathPoints;
    bool mArg7;
    bool mIsInactive;
    u8 mIsPairCameraOn;
    bool mIsAppearDemoPending;
    WarpPod* mPairPod;
    JUTTexture* mGlowTexture;
};

namespace MR {
    WarpPodMgr* getWarpPodManager();
};  // namespace MR
