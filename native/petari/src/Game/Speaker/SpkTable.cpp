#include "Game/Speaker/SpkTable.hpp"

SpkTable::SpkTable() {
    mInitialized = false;
    mResourceCount = 0;
    mParameters = nullptr;
    mBase = nullptr;
    mNameOffsets = nullptr;
}

void SpkTable::setResource(void* pRes) {
    const SpkFile* file = static_cast< const SpkFile* >(pRes);
    mResourceCount = file->count;
    mParameters = reinterpret_cast< SpkParameters* >(static_cast< u8* >(pRes) + file->parametersOffset);
    mBase = static_cast< const u8* >(pRes);
    mNameOffsets = reinterpret_cast< const u32* >(mBase + file->namesOffset);
    mInitialized = true;
}
