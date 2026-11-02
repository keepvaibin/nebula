#pragma once


// The table's values: object pointers as often as numbers.
typedef uintptr_t HashSortValue;

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
    // Lookups of numeric values (indices) into u32.
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
    void swap(const char*, const char*);

    bool mHasBeenSorted;
    u32* mHashCodes;
    HashSortValue* _8;
    u16* _C;
    u16* _10;
    u32 mCurrentLength;
    u32 mMaxLength;
};

namespace MR {
    u32 getHashCode(const char*);
    u32 getHashCodeLower(const char*);
};  // namespace MR
