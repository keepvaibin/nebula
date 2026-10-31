#include "Game/System/GameSequenceDirector.hpp"
#include "Game/System/FindingLuigiEventScheduler.hpp"
#include "Game/System/GameDataTemporaryInGalaxy.hpp"
#include "Game/System/GameSequenceProgress.hpp"
#include "Game/System/SaveDataHandleSequence.hpp"
#include "Game/Util/Functor.hpp"
#include <JSystem/JKernel/JKRHeap.hpp>

GameSequenceDirector::GameSequenceDirector()
    : mGameDataTemporaryInGalaxy(nullptr), mGameSequenceProgress(nullptr), mSaveDataHandleSequence(nullptr) {
    mSaveDataHandleSequence = new SaveDataHandleSequence();
    mSaveDataHandleSequence->registerFunctorOnSaveSuccess(MR::Functor(this, &GameSequenceDirector::executeOnSaveSuccess));
    mSaveDataHandleSequence->registerFunctorJustBeforeSave(MR::Functor(this, &GameSequenceDirector::executeJustBeforeSave));

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

void GameSequenceDirector::executeOnSaveSuccess() {
}

void GameSequenceDirector::executeJustBeforeSave() {
}
