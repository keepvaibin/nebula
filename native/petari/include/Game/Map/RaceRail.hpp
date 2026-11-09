#pragma once

#include "Game/Map/RaceManager.hpp"

class ActorCameraInfo;
class RaceManager;

// The player's side of a race: progress is the player's nearest coord on the race rail.
class PlayerRacer : public LiveActor, public AbstractRacer {
public:
    PlayerRacer(const char*);

    virtual void init(const TableIter&) override;
    virtual void appear() override;
    virtual void initRacer() override;
    virtual void prepRacer(const RaceManager*) override;
    virtual void startRacer() override;
    virtual bool updateRacer(const RaceManager*) override;

    virtual bool goalRacer() override {
        return true;
    }

    virtual void loseRacer() override {
    }

    virtual void resetRacer(const RaceManager*) override;

    virtual void exitRacer() override {
    }

    ActorCameraInfo* mCameraInfo;
};

class RaceRail : public NameObj {
public:
    RaceRail(const char*);

    virtual void init(const TableIter&) override;

    PlayerRacer* mPlayerRacer;
};
