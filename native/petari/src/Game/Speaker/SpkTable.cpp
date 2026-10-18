#include "Game/Speaker/SpkTable.hpp"

SpkTable::SpkTable() {
    mInitialized = false;
    mResourceCount = 0;
    mParameters = nullptr;
    mNames = nullptr;
}

void SpkTable::setResource(void* pRes) {
    mInitialized = false;

    NEBULA_BE(s32)* cursor = (NEBULA_BE(s32)*)pRes;

    s32 resourceCount = *cursor++;
    s32 entryOff = *cursor++;
    s32 dataOffsetsStartOff = *cursor++;
    // Word 3: 0 in the file, TRUE once the name offsets are relocated (nonzero in either byte order).
    NEBULA_BE(s32)* pIsDataOffsetsInitialized = cursor;
    BOOL isDataOffsetsInitialized = *cursor++;

    mResourceCount = resourceCount;

#ifdef TARGET_PC
    SpkParameters* entryOffset = (SpkParameters*)((u8*)pRes + entryOff);
    mParameters = entryOffset;
    NEBULA_SELFREL(const char)* names = (NEBULA_SELFREL(const char)*)((u8*)pRes + dataOffsetsStartOff);
    if (!isDataOffsetsInitialized) {
        for (s32 i = 0; i < mResourceCount; i++) {
            names[i].relocateFrom(pRes);
        }
    }
#else
    SpkParameters* entryOffset = (SpkParameters*)((s32)pRes + entryOff);
    mParameters = entryOffset;
    const char** names = (const char**)((s32)pRes + dataOffsetsStartOff);
    if (!isDataOffsetsInitialized) {
        for (s32 i = 0; i < mResourceCount; i++) {
            names[i] += (s32)pRes;
        }
    }
#endif

    mNames = names;
    *pIsDataOffsetsInitialized = TRUE;
    mInitialized = true;
}
