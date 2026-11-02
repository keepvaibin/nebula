#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class WarpPod;
class ActorCameraInfo;
class JMapIdInfo;
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

    const LiveActor* _C;
    LiveActorGroup* _10;
    s32 _14;
};

class WarpPod : public LiveActor {
public:
    /// @brief Creates a new `WarpPod`.
    /// @param pName A pointer to the null-terminated name of the object.
    WarpPod(const char* pName) : LiveActor(pName), mJMapIdInfo(nullptr) {
    }

    virtual void init(const JMapInfoIter& rIter);
    virtual void movement();
    virtual void draw() const;
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

    JMapIdInfo* mJMapIdInfo;
    s32 mGroupId;
    ActorCameraInfo* mCamInfo;
    char* mEventCameraName;
    char* _9C;
    u16 mDelay;
    u16 _A2;
    u16 _A4;
    u16 _A6;
    s32 mVisibilityState;
    s32 mArg2;
    s32 mArg3;
    s32 mPathFlagIndex;
    s32 mGrandstarReq;
    s32 mCameraTime;
    s32 mGlowColorIndex;
    TVec3f* _C4;
    u16 _C8;
    bool mArg7;
    bool mIsInactive;
    u8 _CC;
    bool _CD;
    WarpPod* mPairPod;
    JUTTexture* _D4;
    JUTTexture* _D8;
};

namespace MR {
    WarpPodMgr* getWarpPodManager();
};  // namespace MR
