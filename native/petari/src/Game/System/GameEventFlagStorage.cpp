#include "Game/System/GameEventFlagStorage.hpp"
#include "Game/System/GameEventFlag.hpp"
#include "Game/System/GameEventFlagTable.hpp"
#include "Game/Util/BitArray.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/System/SaveBytes.hpp"

GameEventFlagStorage::GameEventFlagStorage() : mFlagBitArray(nullptr) {
    mFlagBitArray = new MR::BitArray(GameEventFlagTable::getTableSize());
}

void GameEventFlagStorage::set(const GameEventFlag* pFlag, bool isOn) {
    mFlagBitArray->set(GameEventFlagTable::getIndex(pFlag), isOn);
}

bool GameEventFlagStorage::isOn(const GameEventFlag* pFlag) const {
    return mFlagBitArray->isOn(GameEventFlagTable::getIndex(pFlag));
}

u32 GameEventFlagStorage::getSignature() const {
    return 'FLG1';
}

s32 GameEventFlagStorage::serialize(u8* pData, u32 size) const {
    SaveWriter stream(pData, size);

    for (GameEventFlagIter iter = GameEventFlagTable::getBeginIter(); !iter.isEnd(); iter.goNext()) {
        if ((iter.getFlag()->mSaveFlag & 0x1) != 0) {
            continue;
        }

        GameEventFlagAccessor accessor(iter.getFlag());
        u16 hash = GameEventFlagTable::getSaveHash(accessor.getName());
        u16 data = hash | (isOn(iter.getFlag()) ? 0x8000 : 0);
        stream.write(&data, sizeof(data));
    }

    return stream.getPosition();
}

s32 GameEventFlagStorage::deserialize(const u8* pData, u32 size) {
    bool isFailed = false;
    SaveReader stream(pData, size);
    const s32 num = static_cast< s32 >(size) / static_cast< s32 >(sizeof(u16));

    // Each u16: the flag's save hash in the low 15 bits, on in the top bit.
    for (s32 i = 0; i < num; i++) {
        u16 data;
        stream.read(&data, sizeof(data));

        const s32 index = GameEventFlagTable::getIndexFromHashCode(data & 0x7FFF);
        if (index >= 0) {
            mFlagBitArray->set(index, (data & 0x8000) != 0);
        } else {
            isFailed = true;
        }
    }

    return isFailed;
}

void GameEventFlagStorage::initializeData() {
    for (s32 i = 0; i < mFlagBitArray->size(); i++) {
        mFlagBitArray->set(i, false);
    }
}

u32 GameEventFlagStorage::makeHeaderHashCode() const {
    return MR::getHashCode("2bytes/flag");
}
