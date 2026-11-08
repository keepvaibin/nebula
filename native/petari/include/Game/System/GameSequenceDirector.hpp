#pragma once

class GameDataHolder;
class GameDataTemporaryInGalaxy;
class GameSequenceProgress;
class SaveDataHandleSequence;

class GameSequenceDirector {
public:
    GameSequenceDirector();

    void initAfterResourceLoaded();
    void update();
    void draw() const;
    bool isInitializedGameDataHolder() const;
    GameDataHolder* getGameDataHolder();

    GameDataTemporaryInGalaxy* mGameDataTemporaryInGalaxy;
    GameSequenceProgress* mGameSequenceProgress;
    SaveDataHandleSequence* mSaveDataHandleSequence;
};
