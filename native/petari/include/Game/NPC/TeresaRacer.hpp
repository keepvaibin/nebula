#pragma once

#include "Game/Map/RaceManager.hpp"
#include "Game/NPC/NPCActor.hpp"

class ActorCameraInfo;
class TakeOutStar;

class TeresaRacer : public NPCActor, AbstractRacer {
public:
    TeresaRacer(const char*);

    virtual void init(const TableIter&) override;
    virtual void control() override;
    virtual void initRacer() override;
    virtual void prepRacer(const RaceManager* pRaceManager) override;
    virtual void startRacer() override;
    virtual bool updateRacer(const RaceManager* pRaceManager) override;
    virtual bool goalRacer() override;
    virtual void loseRacer() override;
    virtual void resetRacer(const RaceManager* pRaceManager) override;
    virtual void exitRacer() override;

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
