#include "Game/System/GameDataHolder.hpp"
#include "Game/System/GameDataGalaxyStorage.hpp"
#include "Game/System/GameDataPlayerStatus.hpp"
#include "Game/System/GameEventFlagChecker.hpp"
#include "Game/System/GameEventFlagStorage.hpp"
#include "Game/System/GameEventFlagTable.hpp"
#include "Game/System/GameEventValueChecker.hpp"
#include "Game/System/LumaSaveChunk.hpp"
#include "Game/System/SpinDriverPathStorage.hpp"
#include "Game/System/StarPieceAlmsStorage.hpp"
#include "Game/System/StoryProgressTable.hpp"
#include "Game/System/UserFile.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/StringUtil.hpp"

namespace {
    const char cPictureBookChapterSuffix[] = "ABCDEFGHI";
};  // namespace

GameDataHolder::GameDataHolder(const UserFile* pUserFile)
    : mEventFlagChecker(), mEventValueChecker(), mPlayerStatus(), mAllGalaxyStorage(), mSpinDriverPathStorage(), mStarPieceAlmsStorage(),
      mMapInfo(), mChunkHolder(), mUserFile(pUserFile), mLumaChunk() {
    mAllGalaxyStorage = new GameDataAllGalaxyStorage();
    mEventFlagChecker = new GameEventFlagChecker(this);
    mEventValueChecker = new GameEventValueChecker();
    mPlayerStatus = new GameDataPlayerStatus();
    mSpinDriverPathStorage = new SpinDriverPathStorage();
    mStarPieceAlmsStorage = new StarPieceAlmsStorage();

    mMapInfo = new Table();
    mMapInfo->attach(NebulaDataTables::getStoryProgressTable());

    mChunkHolder = new BinaryDataChunkHolder();
    mChunkHolder->addChunk(mPlayerStatus);
    mChunkHolder->addChunk(mEventFlagChecker->getChunk());
    mChunkHolder->addChunk(mStarPieceAlmsStorage);
    mChunkHolder->addChunk(mSpinDriverPathStorage);
    mChunkHolder->addChunk(mEventValueChecker);
    mChunkHolder->addChunk(mAllGalaxyStorage);
    // Last: a file of a build without it reads as before, and its signature is skipped there.
    mLumaChunk = new LumaSaveChunk(this);
    mChunkHolder->addChunk(mLumaChunk);

    resetAllData();

    mIsMario = true;
    mUser = 1;
}

bool GameDataHolder::isDataMario() const {
    return mIsMario;
}

s32 GameDataHolder::getGalaxyNumCanOpen() const {
    s32 canOpen = 0;

    for (s32 idx = 0; idx < mAllGalaxyStorage->getGalaxyNum(); idx++) {
        GameDataSomeGalaxyStorage* pSomeGalaxyStorage = mAllGalaxyStorage->getGalaxyStorage(idx);

        if (canOnGameEventFlag(pSomeGalaxyStorage->mGalaxyName) && !isOnGameEventFlag(pSomeGalaxyStorage->mGalaxyName)) {
            canOpen++;
        }
    }

    return canOpen;
}

bool GameDataHolder::canOnGameEventFlag(const char* pFlagName) const {
    return mEventFlagChecker->canOn(pFlagName);
}

bool GameDataHolder::isOnGameEventFlag(const char* pFlagName) const {
    return mEventFlagChecker->isOn(pFlagName);
}

void GameDataHolder::tryOnGameEventFlag(const char* pFlagName) {
    mEventFlagChecker->tryOn(pFlagName);
}

s32 GameDataHolder::getGameEventValue(const char* pName) const {
    u16 value = mEventValueChecker->getValue(pName);

    return value;
}

void GameDataHolder::setGameEventValue(const char* pName, u16 value) {
    mEventValueChecker->setValue(pName, value);
}

bool GameDataHolder::isOnGameEventValueForBit(const char* pName, int bit) const {
    u16 value = mEventValueChecker->getValue(pName);

    // The value is 16 bits: a bit outside it (a mod's stage with more pods or Tico than bits) is off.
    return bit >= 0 && bit < 16 && (value & 1 << bit) != 0;
}

void GameDataHolder::setGameEventValueForBit(const char* pName, int bit, bool reset) {
    if (bit < 0 || bit >= 16) {
        return;
    }

    u32 mask;
    u16 value = mEventValueChecker->getValue(pName);
    mask = 1 << bit;
    u16 result = value & ~mask;

    if (reset) {
        result = value | mask;
    }

    setGameEventValue(pName, result);
}

s32 GameDataHolder::getPictureBookChapterCanRead() const {
    s32 canRead = 0;

    for (u32 idx = 0; idx < sizeof(::cPictureBookChapterSuffix) - 1; idx++) {
        if (!isOnGameEventFlag(MR::FormattedName("PictureBook%c", ::cPictureBookChapterSuffix[idx]).c_str())) {
            break;
        }

        canRead++;
    }

    return canRead;
}

s32 GameDataHolder::getPictureBookChapterAlreadyRead() const {
    return mEventValueChecker->getValue("絵本既読章");
}

void GameDataHolder::setPictureBookChapterAlreadyRead(int value) {
    setGameEventValue("絵本既読章", value);
}

void GameDataHolder::setRaceBestTime(const char* pName, u32 value) {
    MR::FormattedName hiName("%s/hi", pName);
    setGameEventValue(hiName.c_str(), value >> 16);

    MR::FormattedName loName("%s/lo", pName);
    setGameEventValue(loName.c_str(), value);
}

u32 GameDataHolder::getRaceBestTime(const char* pName) const {
    MR::FormattedName hiName("%s/hi", pName);
    u32 hiValue = mEventValueChecker->getValue(hiName.c_str());

    MR::FormattedName loName("%s/lo", pName);
    u32 loValue = mEventValueChecker->getValue(loName.c_str());

    return hiValue << 16 | loValue;
}

void GameDataHolder::addMissPoint(int points) {
    u32 value = mEventValueChecker->getValue("MissPointForLetter");
    u32 newValue = MR::clamp(value + points, 0, 20);

    setGameEventValue("MissPointForLetter", newValue);
}

void GameDataHolder::resetMissPoint() {
    setGameEventValue("MissPointForLetter", 0);
}

bool GameDataHolder::isPointCollectForLetter() const {
    return !(mEventValueChecker->getValue("MissPointForLetter") < 20);
}

void GameDataHolder::incPlayerMissNum() {
    s32 newValue = MR::clamp(getGameEventValue("MissNum") + 1, 0, 9999);

    setGameEventValue("MissNum", newValue);
}

s32 GameDataHolder::getPlayerMissNum() const {
    return MR::clamp(getGameEventValue("MissNum"), 0, 9999);
}

bool GameDataHolder::hasPowerStar(const char* pGalaxyName, s32 scenarioNum) const {
    return makeGalaxyScenarioAccessor(pGalaxyName, scenarioNum).hasPowerStar();
}

bool GameDataHolder::hasGrandStar(int index) const {
    return isOnGameEventFlag(MR::FormattedName("SpecialStarGrand%1d", index).c_str());
}

void GameDataHolder::setPowerStar(const char* pGalaxyName, s32 scenarioNum, bool set) {
    makeGalaxyScenarioAccessor(pGalaxyName, scenarioNum).setPowerStarFlag(set);
}

s32 GameDataHolder::getPowerStarNumOwned(const char* pGalaxyName) const {
    return mAllGalaxyStorage->getPowerStarNumOwned(pGalaxyName);
}

s32 GameDataHolder::calcCurrentPowerStarNum() const {
    return mAllGalaxyStorage->calcCurrentPowerStarNum();
}

GameDataSomeScenarioAccessor GameDataHolder::makeGalaxyScenarioAccessor(const char* pGalaxyName, s32 scenarioNum) {
    return mAllGalaxyStorage->makeAccessor(pGalaxyName, scenarioNum);
}

GameDataSomeScenarioAccessor GameDataHolder::makeGalaxyScenarioAccessor(const char* pGalaxyName, s32 scenarioNum) const {
    return mAllGalaxyStorage->makeAccessor(pGalaxyName, scenarioNum);
}

bool GameDataHolder::isOnGalaxyScenarioFlagAlreadyVisited(const char* pGalaxyName, s32 scenarioNum) const {
    if (!mAllGalaxyStorage->isExistAccessor(pGalaxyName, scenarioNum)) {
        return true;
    }

    GameDataSomeScenarioAccessor accessor = makeGalaxyScenarioAccessor(pGalaxyName, scenarioNum);
    return accessor.isAlreadyVisited();
}

void GameDataHolder::onGalaxyScenarioFlagAlreadyVisited(const char* pGalaxyName, s32 scenarioNum) {
    if (mAllGalaxyStorage->isExistAccessor(pGalaxyName, scenarioNum)) {
        GameDataSomeScenarioAccessor accessor = makeGalaxyScenarioAccessor(pGalaxyName, scenarioNum);
        accessor.setFlagAlreadyVisited(true);
    }
}

bool GameDataHolder::isAppearGalaxy(const char* pGalaxyName) const {
    MR::FormattedName name("Appear%s", pGalaxyName);

    if (!GameEventFlagTable::isExist(name.c_str())) {
        return true;
    }

    return isOnGameEventFlag(name.c_str());
}

s32 GameDataHolder::getPlayerLeft() const {
    return mPlayerStatus->getPlayerLeft();
}

void GameDataHolder::addPlayerLeft(int num) {
    mPlayerStatus->addPlayerLeft(num);
}

bool GameDataHolder::isPlayerLeftSupply() const {
    return mPlayerStatus->isPlayerLeftSupply();
}

void GameDataHolder::offPlayerLeftSupply() {
    mPlayerStatus->offPlayerLeftSupply();
}

s32 GameDataHolder::getStockedStarPieceNum() const {
    return mPlayerStatus->mStockedStarPiece;
}

void GameDataHolder::addStockedStarPiece(int num) {
    mPlayerStatus->addStockedStarPiece(num);
}

s32 GameDataHolder::setupSpinDriverPathStorage(const char* pGalaxyName, int scenarioNo, int zoneId, int spinDriverId, f32* pDrawRange) {
    return mSpinDriverPathStorage->setup(pGalaxyName, scenarioNo, zoneId, spinDriverId, pDrawRange);
}

void GameDataHolder::updateSpinDriverPathStorage(const char* pGalaxyName, int scenarioNo, int spinDriverIndex, f32 drawRange) {
    mSpinDriverPathStorage->updateValue(pGalaxyName, scenarioNo, spinDriverIndex, drawRange);
}

s32 GameDataHolder::getStarPieceNumGivingToTicoSeed(int index) const {
    return mStarPieceAlmsStorage->getValue(index);
}

s32 GameDataHolder::getStarPieceNumMaxGivingToTicoSeed(int index) const {
    return mStarPieceAlmsStorage->getMaxValue(index);
}

void GameDataHolder::addStarPieceGivingToTicoSeed(int index, int value) {
    mStarPieceAlmsStorage->addValue(index, value);
}

bool GameDataHolder::isCompleteMarioAndLuigi() const {
    return mUserFile->isOnCompleteEndingMarioAndLuigi();
}

bool GameDataHolder::isPassedStoryEvent(const char* pEventName) const {
    TableIter iter = mMapInfo->findElement("name", pEventName, 0);

    u32 progress = 0;
    iter.getValue("progress", &progress);
    return progress <= mPlayerStatus->mStoryProgress;
}

void GameDataHolder::followStoryEventByName(const char* pEventName) {
    TableIter iter = mMapInfo->findElement("name", pEventName, 0);

    u32 progress = 0;
    iter.getValue("progress", &progress);
    mPlayerStatus->mStoryProgress = progress;
}

void GameDataHolder::resetAllData() {
    mAllGalaxyStorage->initializeData();
    mEventFlagChecker->reset();
    mEventValueChecker->initializeData();
    mPlayerStatus->initializeData();
    mSpinDriverPathStorage->initializeData();
    mStarPieceAlmsStorage->initializeData();
    mLumaChunk->initializeData();
}

u32 GameDataHolder::makeFileBinary(u8* pData, u32 capacity) {
    return mChunkHolder->makeFileBinary(pData, capacity);
}

bool GameDataHolder::loadFromFileBinary(SaveFileId file, const u8* pData, u32 dataSize) {
    mIsMario = file.mKind == SaveFileId::Kind_Mario;
    mUser = file.mUser;
    // A file without the chunk (saved before mods had values) must not keep the last file's.
    mLumaChunk->initializeData();
    return mChunkHolder->loadFromFileBinary(pData, dataSize);
}

u32 GameDataHolder::getLumaChunkRoom() const {
    return BinaryDataChunk::cDataOffset + mLumaChunk->getDataSize();
}
