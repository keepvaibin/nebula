#include "Game/System/SpinDriverPathStorage.hpp"
#include "Game/System/SaveBinary.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/System/ScenarioDataParser.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/SaveBytes.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_serialize_f1, "serialize.f1", 256.0f);

// Saved layout. A scenario block: u16 size, then per zone a zone byte followed by its entries, then an end byte.
// An entry: a header byte (id and flags), then one draw range byte when Interrupted is set (0 means whole).
namespace {
    enum EntryFlag {
        EntryFlag_Interrupted = 0x80,
        EntryFlag_Complete = 0x40,
        EntryFlag_IdMask = 0x3F,
    };

    enum ScenarioByte {
        ScenarioByte_ZoneStart = 0xC0,
        ScenarioByte_ZoneIdMask = 0x3F,
        ScenarioByte_End = 0xFF,
    };

    const u32 cScenarioHeaderSize = 2;
    NEBULA_TUNABLE(u32, cGalaxyHeaderSize, 6);
}  // namespace

SpinDriverPathStorageOne::SpinDriverPathStorageOne() {
    reset();
}

void SpinDriverPathStorageOne::reset() {
    mZoneId = -1;
    mSpinDriverId = -1;
    mDrawRange = -1.0f;
}

f32 SpinDriverPathStorageOne::registerToUse(int zoneId, int spinDriverId) {
    mZoneId = zoneId;
    mSpinDriverId = spinDriverId;
    mDrawRange = 0.0f;
    return mDrawRange;
}

void SpinDriverPathStorageOne::updateValue(f32 drawRange) {
    mDrawRange = drawRange;
}

s32 SpinDriverPathStorageOne::serialize(u8* pData) const {
    if (MR::isNearZero(mDrawRange)) {
        return 0;
    }

    pData[0] = (mSpinDriverId & EntryFlag_IdMask) | EntryFlag_Interrupted;

    const s32 drawRange = cNum_serialize_f1 * getDrawRange();
    if (drawRange <= 0) {
        return 1;
    }

    pData[1] = drawRange >= 256 ? 0 : static_cast< u8 >(drawRange);
    return 2;
}

s32 SpinDriverPathStorageOne::deserialize(const u8* pData, int zoneId) {
    const u8 header = pData[0];
    s32 size = 1;

    if ((header & EntryFlag_Interrupted) == 0) {
        mDrawRange = (header & EntryFlag_Complete) != 0 ? 1.0f : 0.0f;
    } else {
        const u8 drawRange = pData[1];
        mDrawRange = drawRange == 0 ? 1.0f : drawRange * getDrawRangeScale();
        size = 2;
    }

    mZoneId = zoneId;
    mSpinDriverId = header & EntryFlag_IdMask;
    return size;
}

SpinDriverPathStorageScenario::SpinDriverPathStorageScenario() {
    mOneStorage.init(16);
}

void SpinDriverPathStorageScenario::resetAllData() {
    mOneStorage.clear();
}

s32 SpinDriverPathStorageScenario::setup(int zoneId, int spinDriverId, f32* pDrawRange) {
    // Ids -1 and 0 are never stored.
    if (spinDriverId == -1 || spinDriverId == 0) {
        return -1;
    }

    for (s32 idx = 0; idx < mOneStorage.size(); idx++) {
        const SpinDriverPathStorageOne& rOne = mOneStorage[idx];
        if (0.0f <= rOne.mDrawRange && zoneId == rOne.mZoneId && spinDriverId == rOne.mSpinDriverId) {
            *pDrawRange = rOne.mDrawRange;
            return idx;
        }
    }

    SpinDriverPathStorageOne storage;
    storage.registerToUse(zoneId, spinDriverId);
    mOneStorage.push_back(storage);
    *pDrawRange = 0.0f;
    return mOneStorage.size() - 1;
}

void SpinDriverPathStorageScenario::updateValue(int spinDriverIndex, f32 drawRange) {
    mOneStorage[spinDriverIndex].updateValue(drawRange);
}

s32 SpinDriverPathStorageScenario::serialize(u8* pData, u32) const {
    u8* const pBody = pData + cScenarioHeaderSize;
    s32 size = 0;
    const s32 maxZoneId = calcMaxZoneId();

    for (s32 zone = 0; zone <= maxZoneId; zone++) {
        bool isNewZone = true;
        for (s32 idx = 0; idx < mOneStorage.size(); idx++) {
            if (zone == mOneStorage[idx].mZoneId) {
                if (isNewZone) {
                    pBody[size++] = (zone & ~ScenarioByte_ZoneStart) | ScenarioByte_ZoneStart;
                    isNewZone = false;
                }

                size += mOneStorage[idx].serialize(pBody + size);
            }
        }
    }

    pBody[size] = ScenarioByte_End;
    const u16 blockSize = size + cScenarioHeaderSize + 1;
    MR::storeSaveU16(pData, blockSize);
    return blockSize;
}

// Each pass reads at least one byte, and every read is checked against the block.
s32 SpinDriverPathStorageScenario::deserialize(const u8* pData, u32 dataSize) {
    resetAllData();

    s32 readOffset = 0;
    s32 zoneId = -1;

    for (;;) {
        // The size, what is read so far and the next byte lie in the block.
        if (static_cast< u32 >(readOffset) + cScenarioHeaderSize + 1 > dataSize) {
            return 0;
        }

        const u8 lead = pData[readOffset + cScenarioHeaderSize];
        if (lead == ScenarioByte_End) {
            return readOffset + cScenarioHeaderSize + 1;
        }

        if ((lead & ScenarioByte_ZoneStart) == ScenarioByte_ZoneStart) {
            zoneId = lead & ScenarioByte_ZoneIdMask;
            readOffset++;
            continue;
        }

        if (zoneId < 0) {
            return 0;
        }

        if ((lead & EntryFlag_Interrupted) != 0 && static_cast< u32 >(readOffset) + cScenarioHeaderSize + 2 > dataSize) {
            return 0;
        }

        SpinDriverPathStorageOne storage;
        readOffset += storage.deserialize(pData + readOffset + cScenarioHeaderSize, zoneId);
        mOneStorage.push_back(storage);
    }
}

s32 SpinDriverPathStorageScenario::calcMaxZoneId() const {
    s32 maxId = 0;
    for (s32 idx = 0; idx < mOneStorage.size(); idx++) {
        if (maxId < mOneStorage[idx].mZoneId) {
            maxId = mOneStorage[idx].mZoneId;
        }
    }
    return maxId;
}

SpinDriverPathStorageGalaxy::SpinDriverPathStorageGalaxy(const GalaxyStatusAccessor& rAccessor)
    : mScenarioStorage(nullptr), mNumScenarios(rAccessor.getScenarioNoMax()), mGalaxyName(rAccessor.getName()) {
    mScenarioStorage = new SpinDriverPathStorageScenario[mNumScenarios > 0 ? mNumScenarios : 1];
}

void SpinDriverPathStorageGalaxy::resetAllData() {
    for (s32 idx = 0; idx < mNumScenarios; idx++) {
        mScenarioStorage[idx].resetAllData();
    }
}

void SpinDriverPathStorageGalaxy::updateValue(int scenarioNo, int spinDriverIndex, f32 drawRange) {
    SpinDriverPathStorageScenario* pScenario = getScenarioStorage(scenarioNo);
    if (pScenario != nullptr && spinDriverIndex >= 0 && spinDriverIndex < pScenario->mOneStorage.size()) {
        pScenario->updateValue(spinDriverIndex, drawRange);
    }
}

// u16 name hash, u16 block size, u8 scenario count, u8 pad, then each scenario's block.
s32 SpinDriverPathStorageGalaxy::serialize(u8* pData, u32 maxBufferSize) const {
    s32 size = 0;
    for (s32 idx = 0; idx < mNumScenarios; idx++) {
        size += mScenarioStorage[idx].serialize(pData + size + cGalaxyHeaderSize, maxBufferSize - size - cGalaxyHeaderSize);
    }

    const u16 blockSize = size + cGalaxyHeaderSize;
    MR::storeSaveU16(pData + 0, MR::getHashCode(mGalaxyName));
    MR::storeSaveU16(pData + 2, blockSize);
    pData[4] = mNumScenarios;
    pData[5] = 0;
    return blockSize;
}

s32 SpinDriverPathStorageGalaxy::deserialize(const u8* pData, u32 maxBufferSize) {
    const s32 blockSize = MR::loadSaveU16(pData + 2);

    if (MR::loadSaveU16(pData + 0) != static_cast< u16 >(MR::getHashCode(mGalaxyName))) {
        return blockSize;
    }

    resetAllData();
    const s32 savedScenarios = pData[4] < mNumScenarios ? pData[4] : mNumScenarios;

    s32 size = 0;
    for (s32 idx = 0; idx < savedScenarios; idx++) {
        size += mScenarioStorage[idx].deserialize(pData + size + cGalaxyHeaderSize, maxBufferSize - size - cGalaxyHeaderSize);
    }
    return blockSize;
}

SpinDriverPathStorage::SpinDriverPathStorage() {
    mGalaxyStorage.init(50);

    for (ScenarioDataIter iter = MR::makeBeginScenarioDataIter(); !iter.isEnd(); iter.goNext()) {
        GalaxyStatusAccessor accessor = iter.makeAccessor();
        mGalaxyStorage.push_back(new SpinDriverPathStorageGalaxy(accessor));
    }
}

void SpinDriverPathStorage::initializeData() {
    for (s32 idx = 0; idx < mGalaxyStorage.size(); idx++) {
        mGalaxyStorage[idx]->resetAllData();
    }
}

// A galaxy or scenario the tables don't have keeps no path (-1: the launch star draws it from the start each time).
s32 SpinDriverPathStorage::setup(const char* pGalaxyName, int scenarioNo, int zoneId, int spinDriverId, f32* pDrawRange) {
    SpinDriverPathStorageGalaxy* pGalaxy = findFromGalaxy(pGalaxyName);
    SpinDriverPathStorageScenario* pScenario = pGalaxy != nullptr ? pGalaxy->getScenarioStorage(scenarioNo) : nullptr;
    if (pScenario == nullptr) {
        *pDrawRange = 0.0f;
        return -1;
    }
    return pScenario->setup(zoneId, spinDriverId, pDrawRange);
}

void SpinDriverPathStorage::updateValue(const char* pGalaxyName, int scenarioNo, int spinDriverIndex, f32 drawRange) {
    SpinDriverPathStorageGalaxy* pGalaxy = findFromGalaxy(pGalaxyName);
    if (pGalaxy != nullptr) {
        pGalaxy->updateValue(scenarioNo, spinDriverIndex, drawRange);
    }
}

u32 SpinDriverPathStorage::makeHeaderHashCode() const {
    return 0x12345679;
}

u32 SpinDriverPathStorage::getSignature() const {
    return 'SPN1';
}

s32 SpinDriverPathStorage::serialize(u8* pData, u32 maxBufferSize) const {
    SaveWriter stream(pData, maxBufferSize);
    stream.writeU8(mGalaxyStorage.size());
    for (s32 idx = 0; idx < mGalaxyStorage.size(); idx++) {
        s32 writeSize = mGalaxyStorage[idx]->serialize(stream.getCursor(), maxBufferSize - stream.getPosition());
        stream.skip(writeSize);
    }
    return stream.getPosition();
}

s32 SpinDriverPathStorage::deserialize(const u8* pData, u32 maxBufferSize) {
    bool isMissingGalaxy = false;

    if (maxBufferSize == 0) {
        return -1;
    }

    SaveReader stream(pData, maxBufferSize);
    const u8 numEntries = stream.readU8();

    for (s32 idx = 0; idx < numEntries; idx++) {
        // A block's header and its whole size in what is left of the chunk.
        const u32 remaining = maxBufferSize - stream.getPosition();
        if (remaining < cGalaxyHeaderSize) {
            return -1;
        }

        const u8* pBlock = stream.getCursor();
        const u32 blockSize = MR::loadSaveU16(pBlock + 2);
        if (blockSize < cGalaxyHeaderSize || blockSize > remaining) {
            return -1;
        }

        SpinDriverPathStorageGalaxy* pStorage = findFromHashCode(MR::loadSaveU16(pBlock));
        if (pStorage != nullptr) {
            pStorage->deserialize(pBlock, blockSize);
        } else {
            isMissingGalaxy = true;
        }
        stream.skip(blockSize);
    }

    return isMissingGalaxy ? 1 : 0;
}

SpinDriverPathStorageGalaxy* SpinDriverPathStorage::findFromGalaxy(const char* pGalaxyName) {
    for (s32 idx = 0; idx < mGalaxyStorage.size(); idx++) {
        if (MR::isEqualString(mGalaxyStorage[idx]->mGalaxyName, pGalaxyName)) {
            return mGalaxyStorage[idx];
        }
    }
    return nullptr;
}

SpinDriverPathStorageGalaxy* SpinDriverPathStorage::findFromHashCode(u16 galaxyNameHash) {
    for (s32 idx = 0; idx < mGalaxyStorage.size(); idx++) {
        if (static_cast< u16 >(MR::getHashCode(mGalaxyStorage[idx]->mGalaxyName)) == galaxyNameHash) {
            return mGalaxyStorage[idx];
        }
    }
    return nullptr;
}
