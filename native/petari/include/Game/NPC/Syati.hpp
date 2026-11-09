#pragma once

#include "Game/LiveActor/LiveActor.hpp"
#include <nebula_geometry.hpp>

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

    virtual void init(const TableIter& rIter) override;
    virtual void control() override;
    virtual void calcAndSetBaseMtx() override;
    virtual void attackSensor(HitSensor* pSender, HitSensor* pReceiver) override;
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver) override;

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
    void initRings(const TableIter& rIter);
    void initPose();
    void initTalking(const TableIter& rIter);
    void updateSwimCommon();
    void updatePoseByRail();
    void updateNumRingPassed();
    void updateBlink();
    bool isReadyToEmitRing() const;
    void syncNumRingLeftToActiveRings();
    void resetScore();
    void killAllRings();
    void emitRing();
    void setupBalloonFollowMtx(const Vec3f& rVec);
    bool calcHeadJoint(Pos3f* pPos, const JointControllerInfo& rInfo);

    Quat4f mPoseQuat;
    Vec3f mRailDirBlend;
    f32 mClippingRange;
    Vec3f mClippingVec;
    TalkMessageCtrl* mTalkMessageCtrl;
    ActorCameraInfo* mActorCameraInfo;
    s32 mHideOnShoreMode;
    PlayerPoseSetterInWater* mPlayerPoseSetterInWater;
    s32 mBlinkTimer;
    Vec3f mMarioMoveLocalOffsetVec;
    Pos3f mBalloonFollowMtx;
    JointControlDelegator< Syati >* mJointControlDelegator;
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
    inline PlayerPoseSetterInWater(const Vec3f& rVec, Syati* pSyati);
    void update();

    Vec3f mSyatiPos;
    Quat4f mSyatiQuat;
    s32 mStep;
    Vec* mOffset;
};
