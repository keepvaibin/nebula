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

    f32 _8;  // the depth at the balloon's point, 0 near to 1 far
    TVec2f mScreenPos;
    TVec3f _14;
    f32 _20;
    u8 _pad[0x18];
    f32 _3C;
    u8 _pad2[0x14];
};

class TalkDirector : public LayoutActor {
public:
    TalkDirector(const char*);

    virtual ~TalkDirector();
    virtual void init(const JMapInfoIter&);
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
    TalkMessageCtrl* _3C;
    TalkMessageCtrl* _40;
    TalkMessageCtrl* _44;
    TalkState* mTalkState;
    bool _4C;
    bool _4D;
    bool _4E;
    bool mIsInvalidClipping;
    bool _50;
    s32 mDemoType;
    bool _58;
    bool _59;
    bool _5A;
    bool _5B;
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
    bool _7C;
    bool _7D;
    bool mHasOneGreenStar;
    bool mHasTwoGreenStars;
    bool mHasThreeGreenStars;
    bool mIsUnlockedRedDriver;
    bool _82;
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
