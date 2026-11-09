#pragma once

#include "Game/NPC/PenguinRacer.hpp"

class ActorCameraInfo;
class TakeOutStar;

class PenguinRacerLeader : public PenguinRacer {
public:
    PenguinRacerLeader(const char*);

    virtual void init(const TableIter& rIter) override;
    virtual void resetRacer(const RaceManager* pRaceManager) override;
    virtual void exitRacer() override;

    bool branchFunc(u32);
    bool eventFunc(u32);

    void exeWait();
    void exePre();
    void exeReady();
    void exePost();
    void exeTalk();
    void exeTakeOutStar();

    TakeOutStar* mTakeOutStar;
    ActorCameraInfo* mCameraInfo;
};
