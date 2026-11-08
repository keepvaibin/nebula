#include "Game/System/GameSequenceDirector.hpp"
#include "Game/System/FindingLuigiEventScheduler.hpp"
#include "Game/System/GameDataTemporaryInGalaxy.hpp"
#include "Game/System/GameSequenceProgress.hpp"
#include "Game/System/SaveDataHandleSequence.hpp"
#include "Game/System/NebulaArena.hpp"

GameSequenceDirector::GameSequenceDirector() {
    mSaveDataHandleSequence = new SaveDataHandleSequence();
    mGameSequenceProgress = new GameSequenceProgress();
    mGameDataTemporaryInGalaxy = new GameDataTemporaryInGalaxy();
}

void GameSequenceDirector::initAfterResourceLoaded() {
    mSaveDataHandleSequence->initAfterResourceLoaded();
    mGameSequenceProgress->initAfterResourceLoaded();
}

void GameSequenceDirector::update() {
    mSaveDataHandleSequence->update();
    mGameSequenceProgress->update();
}

void GameSequenceDirector::draw() const {
    mSaveDataHandleSequence->draw();
    mGameSequenceProgress->draw();
}

bool GameSequenceDirector::isInitializedGameDataHolder() const {
    return mSaveDataHandleSequence->isInitializedGameDataHolder();
}

GameDataHolder* GameSequenceDirector::getGameDataHolder() {
    return mSaveDataHandleSequence->getHolder();
}
