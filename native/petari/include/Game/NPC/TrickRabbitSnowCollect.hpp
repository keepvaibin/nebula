#pragma once

#include "Game/NPC/TrickRabbitSnow.hpp"
#include <nebula_geometry.hpp>

class ActorCameraInfo;
class BombTimerLayout;
class ModelObj;
class TalkMessageCtrl;

class TrickRabbitSnowCollect : public LiveActor {
public:
    TrickRabbitSnowCollect(const char* pName);

    virtual void init(const TableIter& rIter) override;
    virtual MtxPtr getBaseMtx() const override;

    void initRabbits(const TableIter&);
    void initTalk(const TableIter&);
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
    Pos3f mBaseMtx;
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
    TrickRabbitCollectListener(TrickRabbitSnowCollect* pHost) : TrickRabbitSnowListener(), mHost(pHost) {
    }

    virtual void noticeStart(TrickRabbitSnow* /*pSubject*/) override {
    }

    virtual void noticeAppear(TrickRabbitSnow* /*pSubject*/) override {
    }

    virtual void noticeCaught(TrickRabbitSnow* pSubject) override {
        mHost->noticeCaught(pSubject);
    }

    virtual void noticeGiveUp(TrickRabbitSnow* pSubject) override {
        mHost->noticeGiveUp(pSubject);
    }

private:
    TrickRabbitSnowCollect* mHost;
};
