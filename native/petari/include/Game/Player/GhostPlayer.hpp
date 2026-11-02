#pragma once

#include "Game/LiveActor/LiveActor.hpp"

class ActorCameraInfo;
class CameraTargetMtx;
class FixedPosition;
class GhostPacket;
class HitSensor;
class JMapInfoIter;
class JetTurtleShadow;
class NameObjArchiveListCollector;
class RaceManagerLayout;
class XanimePlayer;

f32 getShiftRatio(s8 shiftValue);

namespace MR {
    void convToFloat(TVec3s&, s8, TVec3f*);
    void convToFloat(TVec3Sc&, s8, TVec3f*);
    void convToFloat(s16, s8, f32*);
    void convToFloat(s8, s8, f32*);
};  // namespace MR

class GhostPlayer : public LiveActor {
public:
    static void makeArchiveList(NameObjArchiveListCollector*, const JMapInfoIter&);

    GhostPlayer(const char*);

    virtual void init(const JMapInfoIter&);
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
    u32 receiveGhostPacket(GhostPacket*);

    u32 _8C;
    u32 _90;
    ActorCameraInfo* mCameraInfo;
    XanimePlayer* mXanimePlayer;
    XanimePlayer* mXanimePlayerUpper;
    Mtx mTargetRotationMtx;
    f32 mAnimTrackWeights[4];
    TVec3f mStartPos;
    u32 _EC[3];  // unused
    TVec3f mTargetRotation;
    TVec3f mInitialDirection;
    bool mIsHidden;
    bool mWaitingToStart;
    bool _112;
    bool mKilledByStar;
    bool mIsDemoCameraActive;
    bool mHasJetTurtle;
    LiveActor* mPowerStarTarget;
    JetTurtleShadow* mJetTurtleShadow;
    FixedPosition* mHandRPos;
    u16 mAppearStarPieceCooldown;
    u16 mPlayerTrampleCooldown;
    GhostPacket* mCurrentPacket;
    RaceManagerLayout* mRaceManagerLayout;
    CameraTargetMtx* mCameraTargetMtx;
};
