#include "Game/System/GameDataGalaxyStorage.hpp"
#include "Game/System/SaveBinary.hpp"
#include "Game/System/GameRules.hpp"
#include "Game/System/BinaryDataContentAccessor.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/System/GameDataHolder.hpp"
#include "Game/System/ScenarioDataParser.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/StringUtil.hpp"

namespace {
    inline s32 writeU16(void* pData, u16 value) {
        MR::storeSaveU16(pData, value);
        return sizeof(value);
    }

    s32 sExtraRoom = 0;

    // An attribute the file's header places inside the record, or one it does not list.
    inline bool isAttributeInRecord(const BinaryDataContentAccessor& rAccessor, const char* pName, u32 size, u8* pRecord, u32 recordSize) {
        const u8* ptr = static_cast< const u8* >(rAccessor.getPointer(pName, pRecord));
        return ptr == nullptr || static_cast< u32 >(ptr - pRecord) + size <= recordSize;
    }
};  // namespace

bool GameDataSomeScenarioAccessor::hasPowerStar() const {
    return mSomeGalaxyStorage->hasPowerStar(mScenarioNum - 1);
}

bool GameDataSomeScenarioAccessor::isAlreadyVisited() const {
    return mSomeGalaxyStorage->isAlreadyVisited(mScenarioNum - 1);
}

s32 GameDataSomeScenarioAccessor::getMaxCoinNum() const {
    return MR::clamp(mSomeGalaxyStorage->getMaxCoinNum(mScenarioNum - 1), 0, GameRules::maxCoins());
}

void GameDataSomeScenarioAccessor::setPowerStarFlag(bool isOwned) {
    setBitFlagAccordingToBool(mSomeGalaxyStorage->mPowerStarOwnedFlags, isOwned);
}

void GameDataSomeScenarioAccessor::setFlagAlreadyVisited(bool isVisited) {
    setBitFlagAccordingToBool(mSomeGalaxyStorage->mAlreadyVisitedFlags, isVisited);
}

void GameDataSomeScenarioAccessor::updateMaxCoinNum(int coinNum) {
    if (getMaxCoinNum() < coinNum) {
        s32 scenarioNo = mScenarioNum - 1;
        u16 maxCoins = MR::clamp(coinNum, 0, GameRules::maxCoins());
        mSomeGalaxyStorage->setMaxCoinNum(scenarioNo, maxCoins);
    }
}

void GameDataSomeScenarioAccessor::setBitFlagAccordingToBool(u8* pFlags, bool isOn) {
    const s32 scenarioNo = mScenarioNum - 1;
    if (!mSomeGalaxyStorage->hasSlot(scenarioNo)) {
        return;
    }
    if (isOn) {
        pFlags[scenarioNo >> 3] |= (1 << (scenarioNo & 7));
    } else {
        pFlags[scenarioNo >> 3] &= ~(1 << (scenarioNo & 7));
    }
}

GameDataSomeGalaxyStorage::GameDataSomeGalaxyStorage(const GalaxyStatusAccessor& rAccessor) {
    mGalaxyName = rAccessor.getName();
    mPowerStarNum = rAccessor.getPowerStarNum();
    mSlots = 8;
    if (rAccessor.getScenarioNum() > mSlots) {
        mSlots = rAccessor.getScenarioNum();
    }
    if (mPowerStarNum > mSlots) {
        mSlots = mPowerStarNum;
    }
    const s32 flagBytes = (mSlots + 7) / 8;
    mPowerStarOwnedFlags = new u8[flagBytes];
    mAlreadyVisitedFlags = new u8[flagBytes];
    mMaxCoinNum = new u16[mSlots];
    resetAllData();
}

GameDataSomeGalaxyStorage::GameDataSomeGalaxyStorage()
    : mGalaxyName(""), mPowerStarNum(0), mSlots(0), mPowerStarOwnedFlags(nullptr), mAlreadyVisitedFlags(nullptr), mMaxCoinNum(nullptr) {
}

void GameDataSomeGalaxyStorage::resetAllData() {
    const s32 flagBytes = (mSlots + 7) / 8;
    for (s32 idx = 0; idx < flagBytes; idx++) {
        mPowerStarOwnedFlags[idx] = 0;
        mAlreadyVisitedFlags[idx] = 0;
    }

    for (s32 idx = 0; idx < mSlots; idx++) {
        mMaxCoinNum[idx] = 0;
    }
}

s32 GameDataSomeGalaxyStorage::getPowerStarNumOwned() const {
    s32 numOwned = 0;
    for (s32 scenarioNo = 1; scenarioNo <= mPowerStarNum; scenarioNo++) {
        if (hasPowerStar(scenarioNo - 1)) {
            numOwned++;
        }
    }
    return numOwned;
}

s32 GameDataSomeGalaxyStorage::deserialize(const BinaryDataContentAccessor& rAccessor, const u8* pData) {
    u8* powerStarFlags = (u8*)rAccessor.getPointer("mPowerStarFlag", (u8*)pData);
    mPowerStarOwnedFlags[0] = powerStarFlags != nullptr ? *powerStarFlags : 0;

    u8* firstPlayFlags = (u8*)rAccessor.getPointer("mFirstPlayFlag", (u8*)pData);
    mAlreadyVisitedFlags[0] = firstPlayFlags != nullptr ? *firstPlayFlags : 0;

    const u8* maxCoinNums = (const u8*)rAccessor.getPointer("mMaxCoinNum", (u8*)pData);
    for (s32 idx = 0; idx < 8; idx++) {
        if (maxCoinNums != nullptr) {
            mMaxCoinNum[idx] = MR::loadSaveU16(maxCoinNums + idx * sizeof(u16));
        } else {
            mMaxCoinNum[idx] = 0;
        }
    }

    if (powerStarFlags == nullptr || firstPlayFlags == nullptr || maxCoinNums == nullptr) {
        return 1;
    }

    // Past eight, if the save has them: a count, then that many missions' flags and coins. A save can have fewer or more than this mod now does.
    const u8* more = (const u8*)rAccessor.getPointer("mStarEx", (u8*)pData);
    if (more != nullptr) {
        // The tables lie in the record or the save is damaged: the count is the file's, the room is the header's.
        const u32 recordSize = rAccessor.getDataSize();
        const u32 at = static_cast< u32 >(more - pData);
        const u32 available = at <= recordSize ? recordSize - at : 0;
        if (available < 2) {
            return 1;
        }
        const s32 extra = MR::loadSaveU16(more);
        const s32 bytes = (extra + 7) / 8;
        if (2u + 2u * static_cast< u32 >(bytes) + 2u * static_cast< u32 >(extra) > available) {
            return 1;
        }
        for (s32 idx = 0; idx < extra; idx++) {
            const s32 scenarioNo = idx + 8;
            if (scenarioNo >= mSlots) {
                break;
            }
            if ((more[2 + (idx >> 3)] & (1 << (idx & 7))) != 0) {
                mPowerStarOwnedFlags[scenarioNo >> 3] |= (1 << (scenarioNo & 7));
            }
            if ((more[2 + bytes + (idx >> 3)] & (1 << (idx & 7))) != 0) {
                mAlreadyVisitedFlags[scenarioNo >> 3] |= (1 << (scenarioNo & 7));
            }
            mMaxCoinNum[scenarioNo] = MR::loadSaveU16(more + 2 + 2 * bytes + idx * sizeof(u16));
        }
    }

    return 0;
}

GameDataAllGalaxyStorage::GameDataAllGalaxyStorage() : mHeaderSerializer(nullptr), mNoGalaxy(new GameDataSomeGalaxyStorage()) {
    mHeaderSerializer = new BinaryDataContentHeaderSerializer(new u8[64], 64);
    mSomeGalaxyStorages.init(64);

    ScenarioDataIter iter = MR::makeBeginScenarioDataIter();
    while (!iter.isEnd()) {
        GalaxyStatusAccessor accessor = iter.makeAccessor();
        if (accessor.getPowerStarNum() != 0) {
            mSomeGalaxyStorages.push_back(new GameDataSomeGalaxyStorage(accessor));
        }
        iter.goNext();
    }

    // First galaxy of a name, and of a save hash, in list order (as the scans found them).
    mNames.reset(static_cast< u32 >(getGalaxyNum()));
    mSaveHashes.reset(static_cast< u32 >(getGalaxyNum()));
    for (s32 idx = 0; idx < getGalaxyNum(); idx++) {
        const char* pName = mSomeGalaxyStorages[idx]->mGalaxyName;
        mNames.add(pName, static_cast< u32 >(idx));
        mSaveHashes.add(pName, static_cast< u16 >(MR::getHashCode(pName)), static_cast< u32 >(idx));
    }
    initHeaderSerializer();
}

bool GameDataAllGalaxyStorage::isExistAccessor(const char* pGalaxyName, int scenarioNum) const {
    if (findIndex(pGalaxyName) < 0) {
        return false;
    }

    GalaxyStatusAccessor accessor = MR::makeGalaxyStatusAccessor(pGalaxyName);
    if (scenarioNum >= 1 && scenarioNum <= accessor.getScenarioNum()) {
        return true;
    }

    return false;
}

// A galaxy the save keeps no storage for (no stars, or not a galaxy) reads and keeps nothing.
GameDataSomeScenarioAccessor GameDataAllGalaxyStorage::makeAccessor(const char* pGalaxyName, int scenarioNum) const {
    const s32 index = findIndex(pGalaxyName);
    return GameDataSomeScenarioAccessor(index >= 0 ? mSomeGalaxyStorages[index] : mNoGalaxy, scenarioNum);
}

s32 GameDataAllGalaxyStorage::calcCurrentPowerStarNum() const {
    s32 powerStarNum = 0;
    for (s32 idx = 0; idx < getGalaxyNum(); idx++) {
        powerStarNum += mSomeGalaxyStorages[idx]->getPowerStarNumOwned();
    }
    return powerStarNum;
}

s32 GameDataAllGalaxyStorage::getPowerStarNumOwned(const char* pGalaxyName) const {
    const s32 index = findIndex(pGalaxyName);
    return index >= 0 ? mSomeGalaxyStorages[index]->getPowerStarNumOwned() : 0;
}

s32 GameDataAllGalaxyStorage::getGalaxyNum() const {
    return mSomeGalaxyStorages.size();
}

GameDataSomeGalaxyStorage* GameDataAllGalaxyStorage::getGalaxyStorage(int index) {
    return mSomeGalaxyStorages[index];
}

u32 GameDataAllGalaxyStorage::makeHeaderHashCode() const {
    return 0xBF0640EE;
}

u32 GameDataAllGalaxyStorage::getSignature() const {
    return 'GALA';
}

s32 GameDataAllGalaxyStorage::deserialize(const u8* pData, u32 dataSize) {
    // The galaxy count, then the header's two counts.
    if (dataSize < 6) {
        return -1;
    }

    s32 attributeNum = MR::loadSaveU16(pData + 0);
    const char* name;
    BinaryDataContentAccessor accessor((u8*)(pData + 2));
    u32 readOffset = accessor.getHeaderSize() + 2;
    const u32 recordSize = accessor.getDataSize();

    if (readOffset > dataSize) {
        return -1;
    }

    u8* first = (u8*)(pData + readOffset);
    name = "mGalaxyName";
    if (accessor.getPointer(name, first) == nullptr || !isAttributeInRecord(accessor, name, sizeof(u16), first, recordSize) ||
        !isAttributeInRecord(accessor, "mPowerStarFlag", sizeof(u8), first, recordSize) ||
        !isAttributeInRecord(accessor, "mFirstPlayFlag", sizeof(u8), first, recordSize) ||
        !isAttributeInRecord(accessor, "mMaxCoinNum", sizeof(u16) * 8, first, recordSize)) {
        return -1;
    }

    bool readError = false;

    for (s32 idx = 0; idx < attributeNum; idx++) {
        if (recordSize > dataSize - readOffset) {
            return -1;
        }

        u8* ptr = (u8*)(pData + readOffset);
        s32 galaxyIndex = findIndex(MR::loadSaveU16(accessor.getPointer(name, ptr)));
        if (galaxyIndex >= 0) {
            switch (mSomeGalaxyStorages[galaxyIndex]->deserialize(accessor, ptr)) {
            case 0:
                break;
            case 1:
                readError = true;
                break;
            default:
                break;
            }
        } else {
            readError = true;
        }
        readOffset += accessor.getDataSize();
    }

    if (readError) {
        return 1;
    }

    return 0;
}

void GameDataAllGalaxyStorage::initializeData() {
    for (s32 idx = 0; idx < getGalaxyNum(); idx++) {
        getGalaxyStorage(idx)->resetAllData();
    }
}

void GameDataAllGalaxyStorage::initHeaderSerializer() {
    mHeaderSerializer->addAttribute("mGalaxyName", sizeof(u16));
    mHeaderSerializer->addAttribute("mPowerStarFlag", sizeof(u8));
    mHeaderSerializer->addAttribute("mFirstPlayFlag", sizeof(u8));
    mHeaderSerializer->addAttribute("mMaxCoinNum", sizeof(u16) * 8);
    // The game's save, to the byte, unless a galaxy has more than eight.
    if (getExtraStars() > 0) {
        mHeaderSerializer->addAttribute("mStarEx", 2 + 2 * ((getExtraStars() + 7) / 8) + getExtraStars() * sizeof(u16));
    }
    mHeaderSerializer->flush();
}

s32 GameDataAllGalaxyStorage::getExtraStars() const {
    s32 extra = 0;
    for (s32 idx = 0; idx < getGalaxyNum(); idx++) {
        if (mSomeGalaxyStorages[idx]->mSlots - 8 > extra) {
            extra = mSomeGalaxyStorages[idx]->mSlots - 8;
        }
    }
    return extra;
}

s32 GameDataAllGalaxyStorage::findIndex(const char* pGalaxyName) const {
    const u32 index = mNames.find(pGalaxyName);
    return index == MR::NameIndex::cNoValue ? -1 : static_cast< s32 >(index);
}

s32 GameDataAllGalaxyStorage::findIndex(u16 galaxyNameHash) const {
    const u32 index = mSaveHashes.findHash(galaxyNameHash);
    return index == MR::NameIndex::cNoValue ? -1 : static_cast< s32 >(index);
}

void GameDataSomeGalaxyStorage::serialize(const BinaryDataContentAccessor& rAccessor, u8* pData) const {
    u8* powerStarFlags = (u8*)rAccessor.getPointer("mPowerStarFlag", pData);
    *powerStarFlags = mPowerStarOwnedFlags[0];

    u8* firstPlayFlags = (u8*)rAccessor.getPointer("mFirstPlayFlag", pData);
    *firstPlayFlags = mAlreadyVisitedFlags[0];

    u8* maxCoinNums = (u8*)rAccessor.getPointer("mMaxCoinNum", pData);
    for (s32 idx = 0; idx < 8; idx++) {
        MR::storeSaveU16(maxCoinNums + idx * sizeof(u16), mMaxCoinNum[idx]);
    }

    u8* more = (u8*)rAccessor.getPointer("mStarEx", pData);
    if (more != nullptr) {
        // Every record has the same room: for the galaxy with the most.
        const s32 room = sExtraRoom;
        const s32 bytes = (room + 7) / 8;
        MR::storeSaveU16(more, room);
        for (s32 idx = 0; idx < 2 * bytes; idx++) {
            more[2 + idx] = 0;
        }
        for (s32 idx = 0; idx < room; idx++) {
            const s32 scenarioNo = idx + 8;
            const bool inRoom = scenarioNo < mSlots;
            if (inRoom && hasPowerStar(scenarioNo)) {
                more[2 + (idx >> 3)] |= (1 << (idx & 7));
            }
            if (inRoom && isAlreadyVisited(scenarioNo)) {
                more[2 + bytes + (idx >> 3)] |= (1 << (idx & 7));
            }
            MR::storeSaveU16(more + 2 + 2 * bytes + idx * sizeof(u16), inRoom ? mMaxCoinNum[scenarioNo] : 0);
        }
    }
}

s32 GameDataAllGalaxyStorage::serialize(u8* pData, u32 /*dataSize*/) const {

    const char* name;
    s32 writeOffset = 0;
    writeOffset += ::writeU16(pData + writeOffset, getGalaxyNum());
    name = reinterpret_cast< const char* >(getSerializer()->getBuffer());
    MR::copyMemory(pData + writeOffset, name, getSerializer()->getHeaderSize());
    writeOffset += getSerializer()->getHeaderSize();

    BinaryDataContentAccessor accessor(static_cast< u8* >(getSerializer()->getBuffer()));
    sExtraRoom = getExtraStars();
    for (s32 idx = 0; idx < getGalaxyNum(); idx++) {
        GameDataSomeGalaxyStorage* storage = mSomeGalaxyStorages[idx];
        name = "mGalaxyName";
        u8* record = pData + writeOffset;
        void* writePtr = accessor.getPointer(name, record);
        ::writeU16(writePtr, MR::getHashCode(storage->mGalaxyName));
        storage->serialize(accessor, record);
        writeOffset += getSerializer()->getDataSize();
    }

    return writeOffset;
}
