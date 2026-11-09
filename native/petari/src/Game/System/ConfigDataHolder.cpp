#include "Game/System/NebulaCalendar.hpp"
#include "Game/System/ConfigDataHolder.hpp"
#include "Game/System/ConfigDataMii.hpp"
#include "Game/System/ConfigDataMisc.hpp"
#include "Game/System/SaveBytes.hpp"

ConfigDataCreateChunk::ConfigDataCreateChunk() : mIsCreated() {
    initializeData();
}

u32 ConfigDataCreateChunk::makeHeaderHashCode() const {
    return 0x2432DA;
}

u32 ConfigDataCreateChunk::getSignature() const {
    return 'CONF';
}

s32 ConfigDataCreateChunk::serialize(u8* pBuffer, u32 size) const {
    SaveWriter stream(pBuffer, size);

    // Saved as 0xFF when created.
    u8 isCreated = mIsCreated ? 0xFF : 0;
    stream.write(&isCreated, sizeof(isCreated));

    return stream.getPosition();
}

s32 ConfigDataCreateChunk::deserialize(const u8* pBuffer, u32 size) {
    initializeData();

    SaveReader stream(pBuffer, size);

    // A short save keeps the initial values.
    u8 isCreated = 0;
    stream.read(&isCreated, sizeof(mIsCreated));
    mIsCreated = isCreated != 0;

    return 0;
}

void ConfigDataCreateChunk::initializeData() {
    mIsCreated = false;
}

void ConfigDataHolder::setIsCreated(bool isCreated) {
    mCreateChunk->mIsCreated = isCreated;
}

bool ConfigDataHolder::isCreated() const {
    return mCreateChunk->mIsCreated;
}

void ConfigDataHolder::setLastLoadedMario(bool lastLoadedMario) {
    mMisc->setLastLoadedMario(lastLoadedMario);
}

bool ConfigDataHolder::isLastLoadedMario() const {
    return mMisc->isLastLoadedMario();
}

void ConfigDataHolder::onCompleteEndingMario() {
    mMisc->onCompleteEndingMario();
}

void ConfigDataHolder::onCompleteEndingLuigi() {
    mMisc->onCompleteEndingLuigi();
}

bool ConfigDataHolder::isOnCompleteEndingMario() {
    return mMisc->isOnCompleteEndingMario();
}

bool ConfigDataHolder::isOnCompleteEndingLuigi() {
    return mMisc->isOnCompleteEndingLuigi();
}

void ConfigDataHolder::updateLastModified() {
    mMisc->updateLastModified();
}

NebulaCalendar::SaveTime ConfigDataHolder::getLastModified() const {
    return mMisc->getLastModified();
}

void ConfigDataHolder::setMiiOrIconId(const void* pMiiId, const u32* pIconId) {
    mMii->setMiiOrIconId(pMiiId, pIconId);
}

bool ConfigDataHolder::getMiiId(void* pMiiId) const {
    return mMii->getMiiId(pMiiId);
}

bool ConfigDataHolder::getIconId(u32* pIconId) const {
    return mMii->getIconId(pIconId);
}

void ConfigDataHolder::resetAllData() {
    mCreateChunk->initializeData();
    mMii->initializeData();
    mMisc->initializeData();
}

s32 ConfigDataHolder::makeFileBinary(u8* pBuffer, u32 size) {
    return mChunkHolder->makeFileBinary(pBuffer, size);
}

ConfigDataHolder::ConfigDataHolder() : mChunkHolder(), mCreateChunk(), mMii(), mMisc() {
    mChunkHolder = new BinaryDataChunkHolder();
    mCreateChunk = new ConfigDataCreateChunk();
    mMii = new ConfigDataMii();
    mMisc = new ConfigDataMisc();

    mChunkHolder->addChunk(mCreateChunk);
    mChunkHolder->addChunk(mMii);
    mChunkHolder->addChunk(mMisc);
    resetAllData();
    mUser = 1;
}

bool ConfigDataHolder::loadFromFileBinary(SaveFileId file, const u8* pBuffer, u32 size) {
    mUser = file.mUser;

    return mChunkHolder->loadFromFileBinary(pBuffer, size);
}
