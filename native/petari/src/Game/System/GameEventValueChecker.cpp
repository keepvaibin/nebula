#include "Game/System/GameEventValueChecker.hpp"
#include "Game/System/FindingLuigiEventScheduler.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <JSystem/JSupport/JSUMemoryInputStream.hpp>
#include <JSystem/JSupport/JSUMemoryOutputStream.hpp>

namespace {
    const GameEventValue cGameEventValueTable[] = {
        {"\203\171\203\223\203\115\203\223\203\214\201\133\203\130[\203\111\201\133\203\126\203\203\203\223\203\212\203\223\203\117]/hi" /* ペンギンレース[オーシャンリング]/hi */, 0},
        {"\203\171\203\223\203\115\203\223\203\214\201\133\203\130[\203\111\201\133\203\126\203\203\203\223\203\212\203\223\203\117]/lo" /* ペンギンレース[オーシャンリング]/lo */, 90 * 60},
        {"\203\145\203\214\203\124\203\214\201\133\203\130[\203\164\203\100\203\223\203\147\203\200]/hi" /* テレサレース[ファントム]/hi */, 0},
        {"\203\145\203\214\203\124\203\214\201\133\203\130[\203\164\203\100\203\223\203\147\203\200]/lo" /* テレサレース[ファントム]/lo */, 90 * 60},
        {"\203\145\203\214\203\124\203\214\201\133\203\130[\203\146\203\130\203\166\203\215\203\200\203\151\201\133\203\150]/hi" /* テレサレース[デスプロムナード]/hi */, 0},
        {"\203\145\203\214\203\124\203\214\201\133\203\130[\203\146\203\130\203\166\203\215\203\200\203\151\201\133\203\150]/lo" /* テレサレース[デスプロムナード]/lo */, 90 * 60},
        {"\203\124\201\133\203\164\203\102\203\223[\203\147\203\211\203\103\203\101\203\213]/hi" /* サーフィン[トライアル]/hi */, 0},
        {"\203\124\201\133\203\164\203\102\203\223[\203\147\203\211\203\103\203\101\203\213]/lo" /* サーフィン[トライアル]/lo */, 90 * 60},
        {"\203\124\201\133\203\164\203\102\203\223[\203\140\203\203\203\214\203\223\203\127]/hi" /* サーフィン[チャレンジ]/hi */, 0},
        {"\203\124\201\133\203\164\203\102\203\223[\203\140\203\203\203\214\203\223\203\127]/lo" /* サーフィン[チャレンジ]/lo */, 90 * 60},
        {"LibraryOpenNewStarCount", 1},
        {"\212\107\226\173\212\371\223\307\217\315" /* 絵本既読章 */, 0},
#if (VERSION == RMGK01)
        {"MsgLedPattern", 1},
#endif
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

GameEventValueChecker::GameEventValueChecker() : mValues(), mNumValues() {
    mValues = new u16[ARRAY_SIZE(::cGameEventValueTable)];
    mNumValues = ARRAY_SIZE(::cGameEventValueTable);
    initializeData();
}

u32 GameEventValueChecker::getValue(const char* pName) const {
    return mValues[findIndex(pName)];
}

void GameEventValueChecker::setValue(const char* pName, u16 value) {
    mValues[findIndex(pName)] = value;
}

u32 GameEventValueChecker::makeHeaderHashCode() const {
    return getSignature();
}

u32 GameEventValueChecker::getSignature() const {
    return 'VLE1';
}

s32 GameEventValueChecker::serialize(u8* pData, u32 maxBufferSize) const {
    JSUMemoryOutputStream stream(pData, maxBufferSize);

    s32 hash;

    for (s32 idx = 0; idx < mNumValues; idx++) {
        hash = MR::getHashCode(::cGameEventValueTable[idx].mName);
        const u16 value = mValues[idx];

        stream.writeU16(hash);
        stream.writeU16(value);
    }

    return stream.mPosition;
}

s32 GameEventValueChecker::deserialize(const u8* pData, u32 maxBufferSize) {
    s32 readError = 0;

    JSUMemoryInputStream stream(pData, maxBufferSize);

    s32 numEntries = static_cast< s32 >(maxBufferSize) / 2;

    for (s32 idx = 0; idx < numEntries; idx++) {
        u16 hash = stream.readU16();
        u16 value = stream.readU16();

        s32 valueIndex = findIndexFromHashCode(hash);

        if (valueIndex >= 0) {
            mValues[valueIndex] = value;
        } else {
            readError = 1;
        }
    }

    if (stream.mState != JSUIosBase::IO_OK && stream.getState(JSUIosBase::IO_MEMORY_ERROR) == 0) {
        stream.mState = JSUIosBase::IO_OK;
    }

    return readError ? 1 : 0;
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
