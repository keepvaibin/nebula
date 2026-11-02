#pragma once

#include "Game/Map/RaceManager.hpp"
#include "Game/NPC/NPCActor.hpp"

class ActorCameraInfo;
class TakeOutStar;

class TeresaRacer : public NPCActor, AbstractRacer {
public:
    TeresaRacer(const char*);

    virtual void init(const JMapInfoIter&);
    virtual void control();
    virtual void initRacer();
    virtual void prepRacer(const RaceManager* pRaceManager);
    virtual void startRacer();
    virtual bool updateRacer(const RaceManager* pRaceManager);
    virtual bool goalRacer();
    virtual void loseRacer();
    virtual void resetRacer(const RaceManager* pRaceManager);
    virtual void exitRacer();

    bool branchFunc(u32);
    bool animeFunc(u32);
    bool eventFunc(u32);

    void exeWait();
    void exePre();
    void exeReady();
    void exeMove();
    void exeGoal();
    void exePost();
    void exeTalk();
    void exeTakeOutStar();

    void changeBgmState();

    TakeOutStar* mTakeOutStar;
    ActorCameraInfo* mCameraInfo;
    bool mIsGoal;
    s32 mBgmState;
    s32 mRacerId;  // -1: Ghostly Galaxy, 0: Boo's Boneyard - Mario, 1: Boo's Boneyard - Luigi
};
