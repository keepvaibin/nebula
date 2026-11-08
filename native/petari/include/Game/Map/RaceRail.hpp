#pragma once

#include "Game/Map/RaceManager.hpp"

class ActorCameraInfo;
class RaceManager;

// The player's side of a race: progress is the player's nearest coord on the race rail.
class PlayerRacer : public LiveActor, public AbstractRacer {
public:
    PlayerRacer(const char*);

    virtual void init(const TableIter&);
    virtual void appear();
    virtual void initRacer();
    virtual void prepRacer(const RaceManager*);
    virtual void startRacer();
    virtual bool updateRacer(const RaceManager*);

    virtual bool goalRacer() {
        return true;
    }

    virtual void loseRacer() {
    }

    virtual void resetRacer(const RaceManager*);

    virtual void exitRacer() {
    }

    ActorCameraInfo* mCameraInfo;
};

class RaceRail : public NameObj {
public:
    RaceRail(const char*);

    virtual void init(const TableIter&);

    PlayerRacer* mPlayerRacer;
};
