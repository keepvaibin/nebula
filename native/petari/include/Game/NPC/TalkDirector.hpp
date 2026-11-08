#pragma once

#include "Game/NPC/TalkMessageInfo.hpp"
#include "Game/Screen/LayoutActor.hpp"
#include "Game/Util/Array.hpp"

class ActorCameraInfo;
class LiveActor;
class TalkMessageCtrl;
class TalkBalloonHolder;
class TalkStateHolder;
class TalkState;

class TalkPeekZ {
public:
    TalkPeekZ();

    // The depth at the balloon's point, from the picture where the drawing has reached (its readback channel).
    void setDrawSyncToken();

    static const u32 cReadbackChannel = 2;

    f32 mDepth;  // the depth at the balloon's point, 0 near to 1 far
    Vec2f mScreenPos;
    Vec3f mWorldPos;
    f32 mProjection[7];
    f32 mViewport[6];
    u32 mAsker;  // the asker its probes carry
};

class TalkDirector : public LayoutActor {
public:
    TalkDirector(const char*);

    virtual ~TalkDirector();
    virtual void init(const TableIter&);
    virtual void movement();

    bool request(TalkMessageCtrl*, bool);
    bool test(TalkMessageCtrl*, bool, bool);
    bool start(TalkMessageCtrl*, bool, bool, bool);
    void updateMessage();
    void prepTalk(TalkMessageCtrl*, bool, bool, bool);
    void termTalk();
    TalkState* initState(TalkMessageCtrl*);
    bool isInvalidTalk() const;
    void appearYesNoSelector(const TalkMessageCtrl*) const;
    s32 getDemoType(const TalkMessageCtrl*, bool) const;
    bool getBranchResult(u16);
    void initBranchResult();
    void pauseOff();
    void balloonOff();
    bool isSystemTalking() const;
    bool isNormalTalking() const;
    LiveActor* getTalkingActor() const;
    void exePrep();
    void exeTalk();
    void exeSlct();
    void exeNext();
    void exeTerm();
    void exeWait();

    MR::Vector< MR::AssignableArray< TalkMessageCtrl* > > mMsgControls;
    TalkPeekZ* mPeekZ;
    TalkBalloonHolder* mBalloonHolder;
    TalkStateHolder* mStateHolder;
    TalkMessageCtrl* mMsgCtrl;
    TalkMessageCtrl* mBalloonCtrl;
    TalkMessageCtrl* mRequestCtrl;
    TalkMessageCtrl* mLastTalkCtrl;
    TalkState* mTalkState;
    bool mIsTalkStart;
    bool mIsTalkEnd;
    bool mIsInvalidTalk;
    bool mIsInvalidClipping;
    bool mIsNext;
    s32 mDemoType;
    bool mIsSkipTerm;
    bool mIsPlayerTalk;
    TalkMessageInfo mMessageInfo;
    ActorCameraInfo* mCameraInfo;
    LiveActor* mHostActor;
    bool mIsKinopioExplorerRescued;
    bool mIsKinopioExplorerOrganize;
    bool mIsKinopioExplorerTalkGetGrandStar2;
    bool mIsKinopioExplorerTrickComet;
    bool mIsKinopioExplorerTalkGetGrandStar3;
    bool mIsKinopioExplorerStartMessenger;
    bool mIsKinopioExplorerTalkGetGrandStar4;
    bool mIsKinopioExplorerTalkGetGrandStar5;
    bool mIsKinopioExplorerTalkGetGrandStar6;
    bool mIsKinopioExplorerTalkGoFinalBattle;
    bool mIsEndLuigiHideAndSeek;
    bool mIsKinopioExplorerCompleteTrickComet;
    bool mHasOneGreenStar;
    bool mHasTwoGreenStars;
    bool mHasThreeGreenStars;
    bool mIsUnlockedRedDriver;
    bool mIsActiveLuigiHideAndSeek;
    bool mIsGalaxyPurpleCometLaunch;
    bool misRosettaTalkTorchLecture;
    bool mIsRosettaTalkTrickComet;
    bool mIsRosettaTalkKoopa;
    bool mIsRosettaTalkCountDownStart;
    bool mIsRosettaTalkAstroDemoRecover;
    bool mIsRosettaTalkTorchProgress;
    bool mIsOnGameEventFlagViewNormalEnding;
};

namespace MR {
    void pauseOffTalkDirector();
    void balloonOffTalkDirector();
    void invalidateTalkDirector();
    void setTalkDirectorDrawSyncToken();
    bool isActiveTalkBalloonShort();
    void endNPCTalkCamera(bool, s32);
};  // namespace MR
