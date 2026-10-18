#pragma once

#include <revolution/types.h>

// The table's values: object pointers as often as numbers.
#ifdef TARGET_PC
typedef uintptr_t HashSortValue;
#else
typedef u32 HashSortValue;
#endif

namespace MR {
    u32 getHashCode(const char*);
};  // namespace MR

class HashSortTable {
public:
    HashSortTable(u32);

    bool add(const char*, HashSortValue, bool);
    bool add(u32, HashSortValue);
    bool addOrSkip(u32, HashSortValue);
    void sort();
    bool search(u32, HashSortValue*);
    bool search(const char*, HashSortValue*);
    bool search(const char*, const char*, HashSortValue*);
#ifdef TARGET_PC
    // Lookups of numeric values (indices) into u32, as on the console.
    bool search(u32 hash, u32* pValue) {
        HashSortValue value;
        const bool found = search(hash, &value);
        if (pValue != nullptr) {
            *pValue = (u32)value;
        }
        return found;
    }
    bool search(const char* pName, u32* pValue) {
        return search(MR::getHashCode(pName), pValue);
    }
    bool search(const char* pName1, const char* pName2, u32* pValue) {
        return search(MR::getHashCode(pName1) + MR::getHashCode(pName2), pValue);
    }
#endif
    void swap(const char*, const char*);

    /* 0x00 */ bool mHasBeenSorted;
    /* 0x04 */ u32* mHashCodes;
    /* 0x08 */ HashSortValue* _8;
    /* 0x0C */ u16* _C;
    /* 0x10 */ u16* _10;
    /* 0x14 */ u32 mCurrentLength;
    /* 0x18 */ u32 mMaxLength;
};

namespace MR {
    u32 getHashCode(const char*);
    u32 getHashCodeLower(const char*);
};  // namespace MR
