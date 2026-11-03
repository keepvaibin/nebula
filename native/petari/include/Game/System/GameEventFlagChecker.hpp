#pragma once


class GameDataHolder;
struct GameEventFlag;
class GameEventFlagStorage;

class GameEventFlagChecker {
public:
    GameEventFlagChecker(GameDataHolder*);
    bool canOn(const char*) const;
    bool isOn(const char*) const;
    bool tryOn(const char*);
    void reset();
    GameEventFlagStorage* getChunk();
    bool isOnGalaxy(const char*) const;
    bool isOnGalaxyDepended(const char*) const;
    bool isOnComet(const GameEventFlag*) const;

    GameDataHolder* mDataHolder;
    GameEventFlagStorage* mFlagStorage;
};
