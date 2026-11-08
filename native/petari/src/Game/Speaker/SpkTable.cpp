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
    mResourceCount = file->mCount;
    mBase = static_cast< const u8* >(pRes);
    mParameters = reinterpret_cast< const SpkParameters* >(mBase + file->mParametersOffset);
    mNameOffsets = reinterpret_cast< const u32* >(mBase + file->mNamesOffset);
    mInitialized = true;
}
