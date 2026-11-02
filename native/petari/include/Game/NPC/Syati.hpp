#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class ActorCameraInfo;
class LiveActorGroup;
class PlayerPoseSetterInWater;
class RaceManagerLayout;
class TalkMessageCtrl;
struct JointControllerInfo;

template < class T >
class JointControlDelegator;

class Syati : public LiveActor {
public:
    Syati(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void control();
    virtual void calcAndSetBaseMtx();
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver);
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    void exeWait();
    void exeFadeoutBeforeTalk();
    void exeWaitBlank();
    void exeFadeinBeforeTalk();
    void exeTalkStartMission();
    void exeReadyToStart();
    void exeCountDown();
    void exeSwim();
    void exeEmitRing();
    void exeWaitStarAppeared();
    void exeReachToEnd();
    void exeWaitAllRingDisappear();
    void exeTalkRetryMission();
    void exeForceKill();
    void exeHideOnShore();
    void exeWaitOnShore();
    void exeWaitTalkNormal();
    void exeTalkNormal();
    void exeStart();
    void initRings(const JMapInfoIter& rIter);
    void initPose();
    void initTalking(const JMapInfoIter& rIter);
    void updateSwimCommon();
    void updatePoseByRail();
    void updateNumRingPassed();
    void updateBlink();
    bool isReadyToEmitRing() const;
    void syncNumRingLeftToActiveRings();
    void resetScore();
    void killAllRings();
    void emitRing();
    void setupBalloonFollowMtx(const TVec3f& rVec);
    bool calcHeadJoint(TPos3f* pPos, const JointControllerInfo& rInfo);

    TQuat4f _8C;
    TVec3f _9C;
    f32 mClippingRange;
    TVec3f mClippingVec;
    TalkMessageCtrl* mTalkMessageCtrl;
    ActorCameraInfo* mActorCameraInfo;
    s32 mHideOnShoreMode;
    PlayerPoseSetterInWater* mPlayerPoseSetterInWater;
    s32 mBlinkTimer;
    TVec3f mMarioMoveLocalOffsetVec;
    TPos3f mBalloonFollowMtx;
    JointControlDelegator< Syati >* mJointControlDelegator;
    TMtx34f _10C;
    RaceManagerLayout* mRaceManagerLayout;
    LiveActorGroup* mPrizeRingGroup;
    s32 mNumRings;
    s32 mStarSpawnType;
    s32 mSwimMode;
    s32 mPrizeRingLife;
    s32 mPrizeRingCount;
    s32 mCurrentRailPointNo;
};

class PlayerPoseSetterInWater {
public:
    inline PlayerPoseSetterInWater(const TVec3f& rVec, Syati* pSyati);
    void update();

    TVec3f _0;
    TQuat4f _C;
    s32 _1C;
    Vec* _20;
};
