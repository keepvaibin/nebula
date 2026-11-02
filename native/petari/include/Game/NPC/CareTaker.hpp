#pragma once

#include "Game/NPC/NPCActor.hpp"

class ActorCameraInfo;
class BombTimerLayout;
class MsgSharedGroup;
class TakeOutStar;

class Caretaker : public NPCActor {
public:
    /// @brief Creates a new `Caretaker`.
    /// @param pName A pointer to the null-terminated name of the object.
    Caretaker(const char* pName);

    virtual void init(const JMapInfoIter& rIter);
    virtual void initAfterPlacement();
    virtual void kill();
    virtual void control();
    virtual bool receiveMsgPlayerAttack(u32 msg, HitSensor* pSender, HitSensor* pReceiver);

    static void makeArchiveList(NameObjArchiveListCollector*, const JMapInfoIter&);

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
    MsgSharedGroup* _168;
    s32 mObjArg0;
    s32 mObjArg4;
    s32 mBodyColorFrame;
    s32 mTidyTimeLimit;
};
