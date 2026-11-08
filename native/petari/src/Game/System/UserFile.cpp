#include "Game/System/NebulaCalendar.hpp"
#include "Game/System/UserFile.hpp"
#include "Game/System/ConfigDataHolder.hpp"
#include "Game/System/GameDataHolder.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Mii/Mii.hpp"

namespace {
    // A Mii name and its terminator.
    const u32 cUserNameSize = Mii::NameLength + 1;
}  // namespace

UserFile::UserFile()
    : mGameDataHolder(nullptr), mConfigDataHolder(nullptr), mIsPlayerMario(true), mIsGameDataCorrupted(false), mIsConfigDataCorrupted(false),
      mUserName(nullptr) {
    mGameDataHolder = new GameDataHolder(this);
    mConfigDataHolder = new ConfigDataHolder();
    mUserName = new char16_t[::cUserNameSize];

    MR::zeroMemory(mUserName, ::cUserNameSize * sizeof(char16_t));
}

bool UserFile::isCreated() const {
    return mConfigDataHolder->isCreated();
}

s32 UserFile::getPowerStarNum() const {
    return mGameDataHolder->calcCurrentPowerStarNum();
}

s32 UserFile::getStarPieceNum() const {
    return mGameDataHolder->getStockedStarPieceNum();
}

s32 UserFile::getPlayerMissNum() const {
    return mGameDataHolder->getPlayerMissNum();
}

bool UserFile::getMiiId(void* pMiiId) const {
    return mConfigDataHolder->getMiiId(pMiiId);
}

bool UserFile::getIconId(u32* pIconId) const {
    return mConfigDataHolder->getIconId(pIconId);
}

bool UserFile::isLastLoadedMario() const {
    return mConfigDataHolder->isLastLoadedMario();
}

bool UserFile::isOnCompleteEndingMarioAndLuigi() const {
    return mConfigDataHolder->isOnCompleteEndingMario() && mConfigDataHolder->isOnCompleteEndingLuigi();
}

NebulaCalendar::SaveTime UserFile::getLastModified() const {
    return mConfigDataHolder->getLastModified();
}

void UserFile::setCreated() {
    mConfigDataHolder->setIsCreated(true);
}

void UserFile::setMiiOrIconId(const void* pMiiId, const u32* pIconId) {
    mConfigDataHolder->setMiiOrIconId(pMiiId, pIconId);
}

void UserFile::setLastLoadedMario(bool lastLoadedMario) {
    mConfigDataHolder->setLastLoadedMario(lastLoadedMario);
}

void UserFile::onCompleteEndingCurrentPlayer() {
    if (mIsPlayerMario) {
        mConfigDataHolder->onCompleteEndingMario();
    } else {
        mConfigDataHolder->onCompleteEndingLuigi();
    }
}

void UserFile::updateLastModified() {
    mConfigDataHolder->updateLastModified();
}

void UserFile::setUserName(const char16_t* pUserName) {
    MR::copyMemory(mUserName, pUserName, ::cUserNameSize * sizeof(char16_t));
}

SaveFileId UserFile::getGameDataFile() const {
    return mGameDataHolder->getFileId();
}

u32 UserFile::makeGameDataBinary(u8* pBuffer, u32 size) const {
    return mGameDataHolder->makeFileBinary(pBuffer, size);
}

void UserFile::loadFromGameDataBinary(SaveFileId file, const u8* pBuffer, u32 size) {
    mIsGameDataCorrupted = !mGameDataHolder->loadFromFileBinary(file, pBuffer, size);
}

SaveFileId UserFile::getConfigDataFile() const {
    return mConfigDataHolder->getFileId();
}

u32 UserFile::makeConfigDataBinary(u8* pBuffer, u32 size) const {
    return mConfigDataHolder->makeFileBinary(pBuffer, size);
}

void UserFile::loadFromConfigDataBinary(SaveFileId file, const u8* pBuffer, u32 size) {
    mIsConfigDataCorrupted = !mConfigDataHolder->loadFromFileBinary(file, pBuffer, size);
}

void UserFile::resetAllData() {
    mGameDataHolder->resetAllData();
    mConfigDataHolder->resetAllData();

    mIsGameDataCorrupted = false;
    mIsConfigDataCorrupted = false;
}

bool UserFile::isViewNormalEnding() const {
    return mGameDataHolder->isOnGameEventFlag("ViewNormalEnding");
}

bool UserFile::isViewCompleteEnding() const {
    return mGameDataHolder->isOnGameEventFlag("ViewCompleteEnding");
}

bool UserFile::isPowerStarGetFinalChallengeGalaxy() const {
    return mGameDataHolder->isOnGameEventFlag("SpecialStarFinalChallenge");
}
