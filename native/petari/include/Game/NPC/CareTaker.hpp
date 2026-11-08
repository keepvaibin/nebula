#pragma once

#include "Game/NPC/NPCActor.hpp"

class ActorCameraInfo;
class BombTimerLayout;
class MsgSharedGroup;
class TakeOutStar;

class Caretaker : public NPCActor {
public:
    Caretaker(const char* pName);

    virtual void init(const TableIter& rIter);
    virtual void initAfterPlacement();
    virtual void kill();
    virtual void control();
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);


    bool branchFuncStar(u32);
    bool branchFuncComet(u32);
    bool eventFuncComet(u32);
    bool animeFunc(u32);

    void setAnim(s32);
    void updateCounterSE();

    void exeTakeOutStar();
    void exeReaction();
    void exeTalk();
    void exeWait();
    void exePreTalk();
    void exePreWipeOut();
    void exePreWipeIn();
    void exePreWait();
    void exeCountDown();
    void exeTidy();
    void exeRunaway();
    void exePstWipeOut();
    void exePstWipeIn();
    void exePstWait();
    void exePstTalk();
    void exeAppearedStar();

private:
    TakeOutStar* mTakeOutStar;
    ActorCameraInfo* mCameraInfo;
    BombTimerLayout* mBombTimerLayout;
    s32 mTidyTimeLimit;
};
