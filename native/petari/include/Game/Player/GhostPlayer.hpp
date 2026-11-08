#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ActorCameraInfo;
class CameraTargetMtx;
class FixedPosition;
class HitSensor;
class TableIter;
class JetTurtleShadow;
class RaceManagerLayout;
class XanimePlayer;

// A ghost recording as the importer cooks it (crates/nebula-import cook/ghost.rs, NGST): a record a frame, then the
// animation names (UTF-8).
struct GhostFileHeader {
    char mMagic[4];  // "NGST"
    u32 mFrameNum;
    u32 mNameOffset;
    u32 mNameSize;
};

// The fields a frame's flags leave out are 0.
struct GhostFrame {
    u16 mFlags;  // 0x1 position, 0x800 velocity, 0x400 scale, 0x2/0x4/0x8 rotation x/y/z, 0x10 animation by name,
                 // 0x2000 by hash, 0x20 animation frame, 0x40 << i track weight i, 0x1000 rate
    s16 mAnimFrame;
    s16 mPosition[3];
    s8 mVelocity[3];
    s8 mScale[3];
    s8 mRotation[3];
    s8 mTrackWeights[4];
    s8 mRate;
    u32 mName;  // offset in the names
    u32 mHash;  // MR::getHashCode of the animation's name
};

f32 getShiftRatio(s8 shiftValue);

namespace MR {
    void convToFloat(Vec3s&, s8, Vec3f*);
    void convToFloat(Vec3Sc&, s8, Vec3f*);
    void convToFloat(s16, s8, f32*);
    void convToFloat(s8, s8, f32*);
};  // namespace MR

class GhostPlayer : public LiveActor {
public:

    GhostPlayer(const char*);

    virtual void init(const TableIter&);
    virtual void draw() const;
    virtual void appear();
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor*, HitSensor*);
    virtual bool receiveMsgPlayerAttack(u32, HitSensor*, HitSensor*);
    virtual bool receiveOtherMsg(u32, HitSensor*, HitSensor*);

    void exeWait();
    void warpPosition(const char*);
    void exePreStartDemo0();
    bool isRequestSkipDemo() const;
    void exePreStartDemo1();
    void exePreStartDemo2();
    void exeWinDemo();
    void exeLostDemo();
    void initAnimation();
    void setAnimation(const char*);
    void setAnimationWeight(const f32*);
    // The frame applied and the next made current, or 0 at the last frame (where the recording stops).
    u32 receiveGhostFrame();

    ActorCameraInfo* mCameraInfo;
    XanimePlayer* mXanimePlayer;
    XanimePlayer* mXanimePlayerUpper;
    Mtx mTargetRotationMtx;
    f32 mAnimTrackWeights[4];
    Vec3f mStartPos;
    Vec3f mTargetRotation;
    Vec3f mInitialDirection;
    bool mIsHidden;
    bool mWaitingToStart;
    bool mSkippingLeadIn;  // the recording's frames are skipped until its fourth track's weight is 0.9 or less
    bool mKilledByStar;
    bool mIsDemoCameraActive;
    bool mHasJetTurtle;
    LiveActor* mPowerStarTarget;
    JetTurtleShadow* mJetTurtleShadow;
    FixedPosition* mHandRPos;
    u16 mAppearStarPieceCooldown;
    u16 mPlayerTrampleCooldown;
    const GhostFrame* mFrame;
    const GhostFrame* mFrameEnd;
    const char* mGhostNames;
    RaceManagerLayout* mRaceManagerLayout;
    CameraTargetMtx* mCameraTargetMtx;
};
