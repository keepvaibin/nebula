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

class TalkDirector : public LayoutActor {
public:
    // Flow-data branch results by index (TalkFunction::getBranchAstroGalaxyResult).
    static const s32 cBranchResultNum = 28;

    TalkDirector(const char*);

    virtual void init(const TableIter&) override;
    virtual void movement() override;

    bool request(TalkMessageCtrl*, bool isForce);
    bool test(TalkMessageCtrl*, bool isForce, bool isDemo);
    bool start(TalkMessageCtrl*, bool isForce, bool isDemo, bool isPlayerTalk);
    void updateMessage();
    void prepTalk(TalkMessageCtrl*, bool isSkipTerm, bool isDemo, bool isPlayerTalk);
    void termTalk();
    TalkState* initState(TalkMessageCtrl*);
    bool isInvalidTalk() const;
    void appearYesNoSelector(const TalkMessageCtrl*) const;
    s32 getDemoType(const TalkMessageCtrl*, bool isDemo) const;
    bool getBranchResult(u16 index) const;
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
    // The last talk with a camera: termTalk ends that camera.
    TalkMessageInfo mMessageInfo;
    ActorCameraInfo* mCameraInfo;
    LiveActor* mHostActor;
    bool mBranchResult[cBranchResultNum];
};

namespace MR {
    void pauseOffTalkDirector();
    void balloonOffTalkDirector();
    void invalidateTalkDirector();
    bool isActiveTalkBalloonShort();
    void endNPCTalkCamera(bool, s32);
};  // namespace MR
