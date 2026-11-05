#include "Game/Util/Functor.hpp"

#include "Game/LiveActor/Nerve.hpp"
#include "Game/System/GameSystemFunction.hpp"
#include "Game/System/GameSystemStationedArchiveLoader.hpp"
#include "Game/System/HeapMemoryWatcher.hpp"
#include "Game/System/ResourceHolder.hpp"
#include "Game/System/StationedFileInfo.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/SystemUtil.hpp"
#include "Game/System/NebulaArena.hpp"

namespace {
    NEW_NERVE(GameSystemStationedArchiveLoaderLoadStationedArchivePlayer, GameSystemStationedArchiveLoader, LoadStationedArchivePlayer);
    NEW_NERVE(GameSystemStationedArchiveLoaderLoadStationedArchiveOthers, GameSystemStationedArchiveLoader, LoadStationedArchiveOthers);
    NEW_NERVE(GameSystemStationedArchiveLoaderInitializeGameData, GameSystemStationedArchiveLoader, InitializeGameData);
    NEW_NERVE(GameSystemStationedArchiveLoaderEnd, GameSystemStationedArchiveLoader, End);
    NEW_NERVE(GameSystemStationedArchiveLoaderChangeArchivePlayer, GameSystemStationedArchiveLoader, ChangeArchivePlayer);
};  // namespace

bool ConditionIfIsNotPlayer::isExecute(const MR::StationedFileInfo* pInfo) const {
    return pInfo->mLoadType != MR::StationedFileInfo::LOAD_TYPE_MOUNT_RESOURCE_MARIO &&
           pInfo->mLoadType != MR::StationedFileInfo::LOAD_TYPE_MOUNT_RESOURCE_LUIGI;
}

ConditionUsePlayerHeap::ConditionUsePlayerHeap() : mHeap(), mIsDataMario(true) {
}

bool ConditionUsePlayerHeap::isExecute(const MR::StationedFileInfo* pInfo) const {
    MR::StationedFileInfo::LoadType type = MR::StationedFileInfo::LOAD_TYPE_MOUNT_RESOURCE_LUIGI;

    if (mIsDataMario) {
        type = MR::StationedFileInfo::LOAD_TYPE_MOUNT_RESOURCE_MARIO;
    }

    return pInfo->mLoadType == type;
}

NebulaArena* ConditionUsePlayerHeap::getProperHeap(const MR::StationedFileInfo* pInfo) const {
    return mHeap;
}

PlayerHeapHolder::PlayerHeapHolder() : mCondition(), mHeap(), mIsDataMario(true) {
    ConditionUsePlayerHeap* condition = new ConditionUsePlayerHeap();
    // Room for either player's archives: a change of player loads the other's in the same heaps at any time. Pages not used cost nothing.
    mHeap = NebulaArena::create(MR::getPermanentHeap());
    condition->mHeap = mHeap;
    mCondition = condition;
    condition->mIsDataMario = mIsDataMario;
}

void PlayerHeapHolder::dispose() {
    MR::removeResourceAndFileHolderIfIsEqualHeap(mHeap);
    mHeap->freeAll();
}

void PlayerHeapHolder::setIsDataMario(bool isDataMario) {
    mIsDataMario = isDataMario;
    mCondition->mIsDataMario = isDataMario;
}

GameSystemStationedArchiveLoader::GameSystemStationedArchiveLoader() : NerveExecutor("\217\355\222\223\203\146\201\133\203\136\217\211\212\372\211\273" /* 常駐データ初期化 */), mHeapHolder() {
    initNerve(GET_NERVE_ANON(GameSystemStationedArchiveLoaderLoadStationedArchiveOthers));
}

void GameSystemStationedArchiveLoader::update() {
    updateNerve();
}

bool GameSystemStationedArchiveLoader::isDone() const {
    return isNerve(GET_NERVE_ANON(GameSystemStationedArchiveLoaderEnd));
}

void GameSystemStationedArchiveLoader::waitDone() {
    while (!isDone()) {
        update();
    }
}

void GameSystemStationedArchiveLoader::requestChangeArchivePlayer(bool isDataMario) {
    if (mHeapHolder == nullptr) {
        return;
    }

    if (isNerve(GET_NERVE_ANON(GameSystemStationedArchiveLoaderChangeArchivePlayer))) {
        return;
    }

    if (isDataMario == mHeapHolder->mIsDataMario) {
        return;
    }

    mHeapHolder->setIsDataMario(isDataMario);
    setNerve(GET_NERVE_ANON(GameSystemStationedArchiveLoaderChangeArchivePlayer));
}

void GameSystemStationedArchiveLoader::exeLoadStationedArchivePlayer() {
    if (MR::isFirstStep(this)) {
        mHeapHolder = new PlayerHeapHolder();

        startToLoadStationedArchivePlayer(true);
        createAndAddPlayerArchives(mHeapHolder->mIsDataMario);

        if (mHeapHolder != nullptr) {
        }

        setNerve(GET_NERVE_ANON(GameSystemStationedArchiveLoaderInitializeGameData));
    }
}

void GameSystemStationedArchiveLoader::exeLoadStationedArchiveOthers() {
    if (MR::isFirstStep(this)) {
        startToLoadStationedArchiveOthers();
        createAndAddOtherArchives();
        setNerve(GET_NERVE_ANON(GameSystemStationedArchiveLoaderLoadStationedArchivePlayer));
    }
}

void GameSystemStationedArchiveLoader::exeInitializeGameData() {
    GameSystemFunction::initAfterStationedResourceLoaded();
    SingletonHolder< HeapMemoryWatcher >::get()->setCurrentHeapToGameHeap();
    GameSystemFunction::setSceneNameObjHolderToNameObjRegister();
    setNerve(GET_NERVE_ANON(GameSystemStationedArchiveLoaderEnd));
}

void GameSystemStationedArchiveLoader::exeEnd() {
}

void GameSystemStationedArchiveLoader::exeChangeArchivePlayer() {
    if (MR::isFirstStep(this)) {
        mHeapHolder->dispose();
        bool isMario = mHeapHolder->mIsDataMario;
        startToLoadStationedArchivePlayer(isMario);
        createAndAddPlayerArchives(mHeapHolder->mIsDataMario);
        setNerve(GET_NERVE_ANON(GameSystemStationedArchiveLoaderEnd));
    }
}

void GameSystemStationedArchiveLoader::startToLoadStationedArchivePlayer(bool a1) {
    StationedArchiveLoader::loadResourcesFromTable(*mHeapHolder->mCondition);
}

void GameSystemStationedArchiveLoader::startToLoadStationedArchiveOthers() {
    StationedArchiveLoader::loadResourcesFromTable(ConditionIfIsNotPlayer());
    StationedArchiveLoader::loadScenarioData(MR::getPermanentHeap());
}

void GameSystemStationedArchiveLoader::createAndAddPlayerArchives(bool isDataMario) {
    ConditionIsEqualType cond(isDataMario);
    StationedArchiveLoader::createAndAddResourcesFromTable(cond);

    ResourceHolder* pMarioAnimeHolder = MR::createAndAddResourceHolder("MarioAnime.arc");

    if (isDataMario) {
        pMarioAnimeHolder->bindMaterialAnimations(
            static_cast< J3DModelData* >(MR::createAndAddResourceHolder("Mario.arc")->mModelResTable->getRes("mario")));
    } else {
        pMarioAnimeHolder->bindMaterialAnimations(
            static_cast< J3DModelData* >(MR::createAndAddResourceHolder("Luigi.arc")->mModelResTable->getRes("luigi")));
    }
}

void GameSystemStationedArchiveLoader::createAndAddOtherArchives() {
    ConditionIfIsNotPlayer cond;
    StationedArchiveLoader::createAndAddResourcesFromTable(cond);
}

bool ConditionIsEqualType::isExecute(const MR::StationedFileInfo* pStationedFileInfo) const {
    return pStationedFileInfo->mLoadType == mLoadType;
}
