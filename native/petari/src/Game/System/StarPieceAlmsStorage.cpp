#include "Game/System/StarPieceAlmsStorage.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/System/SaveBinary.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"

namespace {
    const u32 sMaxValueArray[] = {32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 400, 400, 600, 1200, 1600, 1000, 800, 32767};

    // The game's sixteen, and past them the slots mods add.
    s32 valueCount() {
        const s32 count = 8 + NebulaMods::hungryLumaSlots();
        return count > 16 ? count : 16;
    }
};  // namespace

StarPieceAlmsStorage::StarPieceAlmsStorage() : mValueArray(nullptr), mValueNum(valueCount()) {
    mValueArray = new u16[mValueNum]();
}

s32 StarPieceAlmsStorage::getValue(int idx) const {
    if (idx < 0 || idx >= mValueNum) {
        return 0;
    }

    return mValueArray[idx];
}

s32 StarPieceAlmsStorage::getMaxValue(int idx) const {
    const s32 price = idx >= 8 ? NebulaMods::hungryLumaPrice(idx - 8) : -1;
    return price >= 0 ? price : idx < 16 ? static_cast< s32 >(::sMaxValueArray[idx]) : 32767;
}

void StarPieceAlmsStorage::addValue(int idx, int add) {
    if (idx < 0 || idx >= mValueNum) {
        return;
    }

    s32 value = mValueArray[idx];
    s32 maxValue = getMaxValue(idx);

    mValueArray[idx] = MR::clamp(value + add, 0, maxValue);
}

u32 StarPieceAlmsStorage::makeHeaderHashCode() const {
    return MR::getHashCode("StarPieceAlmsStorage") << 5;
}

u32 StarPieceAlmsStorage::getSignature() const {
    return 'PCE1';
}

s32 StarPieceAlmsStorage::serialize(u8* pData, u32) const {
    // Sixteen u16 values in the save file, and any more that mods add.
    for (s32 idx = 0; idx < mValueNum; idx++) {
        MR::storeSaveU16(pData + idx * sizeof(u16), mValueArray[idx]);
    }
    return mValueNum * sizeof(u16);
}

s32 StarPieceAlmsStorage::deserialize(const u8* pData, u32 dataSize) {
    if (dataSize < 16 * sizeof(u16)) {
        return -1;
    }

    for (s32 idx = 0; idx < mValueNum && (idx + 1) * sizeof(u16) <= dataSize; idx++) {
        mValueArray[idx] = MR::loadSaveU16(pData + idx * sizeof(u16));
    }
    return 0;
}

void StarPieceAlmsStorage::initializeData() {
    for (s32 idx = 0; idx < mValueNum; idx++) {
        mValueArray[idx] = 0;
    }
}
