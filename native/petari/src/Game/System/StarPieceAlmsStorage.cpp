#include "Game/System/StarPieceAlmsStorage.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/System/SaveBinary.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"

namespace {
    const static u32 sMaxValueArray[] = {32767, 32767, 32767, 32767, 32767, 32767, 32767, 32767, 400, 400, 600, 1200, 1600, 1000, 800, 32767};
};  // namespace

namespace {
    // The game's sixteen, and past them the slots mods add.
    s32 valueCount() {
        const s32 count = 8 + NebulaMods::hungryLumaSlots();
        return count > 16 ? count : 16;
    }
}  // namespace

StarPieceAlmsStorage::StarPieceAlmsStorage() : mValueArray() {
    mValueArray = new u16[valueCount()];

    for (s32 idx = 0; idx < valueCount(); idx++) {
        mValueArray[idx] = 0;
    }
}

s32 StarPieceAlmsStorage::getValue(int idx) const {
    return mValueArray[idx];
}

s32 StarPieceAlmsStorage::getMaxValue(int idx) const {
    const s32 price = idx >= 8 ? NebulaMods::hungryLumaPrice(idx - 8) : -1;
    return price >= 0 ? price : idx < 16 ? static_cast< s32 >(::sMaxValueArray[idx]) : 32767;
}

void StarPieceAlmsStorage::addValue(int idx, int add) {
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
    for (s32 idx = 0; idx < valueCount(); idx++) {
        MR::storeSaveU16(pData + idx * sizeof(u16), mValueArray[idx]);
    }
    return valueCount() * sizeof(u16);
}

s32 StarPieceAlmsStorage::deserialize(const u8* pData, u32 dataSize) {
    if (dataSize < 16 * sizeof(u16)) {
        return -1;
    }

    for (s32 idx = 0; idx < valueCount() && (idx + 1) * sizeof(u16) <= dataSize; idx++) {
        mValueArray[idx] = MR::loadSaveU16(pData + idx * sizeof(u16));
    }
    return 0;
}

void StarPieceAlmsStorage::initializeData() {
    for (s32 idx = 0; idx < valueCount(); idx++) {
        mValueArray[idx] = 0;
    }
}
