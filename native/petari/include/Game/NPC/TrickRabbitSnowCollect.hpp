#pragma once

#include "Game/NPC/TrickRabbitSnow.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class ActorCameraInfo;
class BombTimerLayout;
class ModelObj;
class TalkMessageCtrl;

class TrickRabbitSnowCollect : public LiveActor {
public:
    /// @brief Creates a new `TrickRabbitSnowCollect`.
    /// @param pName A pointer to the null-terminated name of the object.
    TrickRabbitSnowCollect(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual MtxPtr getBaseMtx() const;

    void initRabbits(const JMapInfoIter&);
    void initTalk(const JMapInfoIter&);
    void setStartPosition();
    void setFinishPosition();
    void noticeCaught(TrickRabbitSnow*);
    void noticeGiveUp(TrickRabbitSnow*);
    void startSuccessDemo();
    void startFailedDemo();

    void exeTryDemo();
    void exeWait();
    void exeStartWipeOut();
    void exeStartWipeIn();
    void exeStartTalk();
    void exeWaitHideSnow();
    void exeStart();
    void exeFailedWipeOut();
    void exeFailedWipeIn();
    void exeFailedTalk();
    void exeSuccessWipeOut();
    void exeSuccessWipeIn();
    void exeSuccessTalk();
    void exeTakeOutStar();
    void exeAppearPowerStar();
    void exeEnd();

private:
    TPos3f mBaseMtx;
    ActorCameraInfo* mCameraInfo;
    ModelObj* mPowerStarDemoModel;
    TalkMessageCtrl* mMsgCtrl;
    BombTimerLayout* mTimerLayout;
    TrickRabbitSnow** mRabbit;
    s32 mRabbitNum;
    s32 mTimeLimit;
    bool mIsDemo;
};

class TrickRabbitCollectListener : public TrickRabbitSnowListener {
public:
    /// @brief Creates a new `TrickRabbitCollectListener`.
    TrickRabbitCollectListener(TrickRabbitSnowCollect* pHost) : TrickRabbitSnowListener(), mHost(pHost) {
    }

    virtual void noticeStart(TrickRabbitSnow* pSubject) {
    }

    virtual void noticeAppear(TrickRabbitSnow* pSubject) {
    }

    virtual void noticeCaught(TrickRabbitSnow* pSubject) {
        mHost->noticeCaught(pSubject);
    }

    virtual void noticeGiveUp(TrickRabbitSnow* pSubject) {
        mHost->noticeGiveUp(pSubject);
    }

private:
    TrickRabbitSnowCollect* mHost;
};
