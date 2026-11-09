#include "Game/System/GameEventValueChecker.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/FindingLuigiEventScheduler.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/SaveBytes.hpp"

namespace {
    const GameEventValue cGameEventValueTable[] = {
        {"ペンギンレース[オーシャンリング]/hi", 0},
        {"ペンギンレース[オーシャンリング]/lo", NebulaFrameClock::stepsOfSeconds(90)},
        {"テレサレース[ファントム]/hi", 0},
        {"テレサレース[ファントム]/lo", NebulaFrameClock::stepsOfSeconds(90)},
        {"テレサレース[デスプロムナード]/hi", 0},
        {"テレサレース[デスプロムナード]/lo", NebulaFrameClock::stepsOfSeconds(90)},
        {"サーフィン[トライアル]/hi", 0},
        {"サーフィン[トライアル]/lo", NebulaFrameClock::stepsOfSeconds(90)},
        {"サーフィン[チャレンジ]/hi", 0},
        {"サーフィン[チャレンジ]/lo", NebulaFrameClock::stepsOfSeconds(90)},
        {"LibraryOpenNewStarCount", 1},
        {"絵本既読章", 0},
        {"LuigiEventState", FindingLuigiEventScheduler::STATE_NULL},
        {"WarpPodSaveBits", 0},
        {"TicoGalaxyAlreadyTalk", 0},
        {"MessageAlreadyRead", 0},
        {"MissPointForLetter", 0},
        {"MissNum", 0},
        {"Comet1Status", 0},
        {"Comet2Status", 0},
        {"Comet3Status", 0},
        {"Comet4Status", 0},
        {"Comet5Status", 0},
        {"Comet6Status", 0},
    };
};  // namespace

GameEventValueChecker::GameEventValueChecker()
    : mValues(new u16[ARRAY_SIZE(::cGameEventValueTable)]), mNumValues(ARRAY_SIZE(::cGameEventValueTable)) {
    initializeData();
}

// A name not in the table (a mod's) reads 0 and keeps nothing.
u32 GameEventValueChecker::getValue(const char* pName) const {
    const s32 index = findIndex(pName);
    return index >= 0 ? mValues[index] : 0;
}

void GameEventValueChecker::setValue(const char* pName, u16 value) {
    const s32 index = findIndex(pName);
    if (index >= 0) {
        mValues[index] = value;
    }
}

u32 GameEventValueChecker::makeHeaderHashCode() const {
    return getSignature();
}

u32 GameEventValueChecker::getSignature() const {
    return 'VLE1';
}

s32 GameEventValueChecker::serialize(u8* pData, u32 maxBufferSize) const {
    SaveWriter stream(pData, maxBufferSize);

    // Each entry: the name's hash (low 16 bits), then the value.
    for (s32 idx = 0; idx < mNumValues; idx++) {
        stream.writeU16(MR::getHashCode(::cGameEventValueTable[idx].mName));
        stream.writeU16(mValues[idx]);
    }

    return stream.getPosition();
}

s32 GameEventValueChecker::deserialize(const u8* pData, u32 maxBufferSize) {
    bool isFailed = false;
    SaveReader stream(pData, maxBufferSize);
    const s32 numEntries = static_cast< s32 >(maxBufferSize) / 2;

    for (s32 idx = 0; idx < numEntries; idx++) {
        const u16 hash = stream.readU16();
        const u16 value = stream.readU16();
        const s32 valueIndex = findIndexFromHashCode(hash);
        if (valueIndex >= 0) {
            mValues[valueIndex] = value;
        } else {
            isFailed = true;
        }
    }

    return isFailed;
}

void GameEventValueChecker::initializeData() {
    for (s32 idx = 0; idx < mNumValues; idx++) {
        mValues[idx] = ::cGameEventValueTable[idx].mDefaultValue;
    }
}

s32 GameEventValueChecker::findIndex(const char* pName) const {
    for (s32 idx = 0; idx < mNumValues; idx++) {
        if (MR::isEqualString(pName, ::cGameEventValueTable[idx].mName)) {
            return idx;
        }
    }

    return -1;
}

s32 GameEventValueChecker::findIndexFromHashCode(u16 hash) const {
    for (s32 idx = 0; idx < mNumValues; idx++) {
        if (hash == static_cast< u16 >(MR::getHashCode(::cGameEventValueTable[idx].mName))) {
            return idx;
        }
    }

    return -1;
}
