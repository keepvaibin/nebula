#pragma once

#include <cstring>
#include "Game/Util/StringUtil.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

class TableIter;

// A column's kind (crates/nebula-import/src/cook/table.rs): an int reads signed at its width, a field unsigned only, a
// float its bits, a text its word.
enum TableKind {
    TableKind_Int = 0,
    TableKind_Field = 1,
    TableKind_Float = 2,
    TableKind_Text = 3,
};

struct TableColumn {
    u32 mHash;
    u32 mMask;
    u8 mKind;
    u8 mWidth;
};
static_assert(sizeof(TableColumn) == 12, "NTBL column stride");

// NTBL as the importer cooks it: a u32 cell per row and column (masks and shifts applied), words UTF-8.
struct TableData {
    char mMagic[4];
    u32 mRows;
    u32 mColumns;
    u32 mRowsAt;
    u32 mTextAt;
    u32 mTextSize;
    u32 mReserved[2];

    const TableColumn* columns() const {
        return reinterpret_cast< const TableColumn* >(this + 1);
    }

    const u32* cells() const {
        return reinterpret_cast< const u32* >(reinterpret_cast< const char* >(this) + mRowsAt);
    }

    const char* text() const {
        return reinterpret_cast< const char* >(this) + mTextAt;
    }
};

template < typename T >
inline bool compareValues(const T a, const T b) {
    return a == b;
}

template <>
inline bool compareValues< const char* >(const char* pA, const char* pB) {
    return MR::isEqualString(pA, pB);
}

class Table {
public:
    Table();
    ~Table();

    inline bool operator==(const Table& rInfo) const {
        return mData == rInfo.mData;
    }

    inline bool dataExists() const {
        return !!mData;
    }

    inline int getNumEntries() const {
        return dataExists() ? static_cast< s32 >(mData->mRows) : 0;
    }

    inline int getNumFields() const {
        return dataExists() ? static_cast< s32 >(mData->mColumns) : 0;
    }

    bool attach(const void* pData);
    void setName(const char* pName);
    const char* getName() const;
    s32 searchItemInfo(const char* pKey) const;
    // The column's TableKind; -1 for a column the table lacks.
    s32 getColumnKind(const char* pKey) const;

    const u32* cell(int entryIndex, int itemIndex) const {
        return mData->cells() + entryIndex * mData->mColumns + itemIndex;
    }

    bool getValueFast(int entryIndex, int itemIndex, const char** pValueOut) const {
        const u32* pCell = cell(entryIndex, itemIndex);
        *pValueOut = mData->columns()[itemIndex].mKind == TableKind_Text ? mData->text() + *pCell
                                                                         : reinterpret_cast< const char* >(pCell);
        return true;
    }
    bool getValueFast(int entryIndex, int itemIndex, u32* pValueOut) const {
        const u8 kind = mData->columns()[itemIndex].mKind;
        if (kind != TableKind_Int && kind != TableKind_Field) {
            return false;
        }
        *pValueOut = *cell(entryIndex, itemIndex);
        return true;
    }
    bool getValueFast(int entryIndex, int itemIndex, s32* pValueOut) const {
        const TableColumn& rColumn = mData->columns()[itemIndex];
        if (rColumn.mKind != TableKind_Int) {
            return false;
        }
        const u32 value = *cell(entryIndex, itemIndex);
        *pValueOut = rColumn.mWidth == 8    ? static_cast< s8 >(value)
                     : rColumn.mWidth == 16 ? static_cast< s16 >(value)
                                            : static_cast< s32 >(value);
        return true;
    }
    bool getValueFast(int entryIndex, int itemIndex, f32* pValueOut) const {
        __builtin_memcpy(pValueOut, cell(entryIndex, itemIndex), sizeof(f32));
        return true;
    }
    bool getValueFast(int entryIndex, int itemIndex, bool* pValueOut) const {
        *pValueOut = *cell(entryIndex, itemIndex) != 0;
        return true;
    }

    TableIter findElementBinary(const char* pKey, const char* pValue) const;

    template < typename T >
    const bool getValue(int entryIndex, const char* pKey, T* pValueOut) const;

    template < typename T >
    TableIter findElement(const char* pKey, T searchValue, int startIndex) const;

    inline TableIter begin() const;
    inline TableIter end() const;

    const TableData* mData;
    const char* mName;
};

template < typename T >
const bool Table::getValue(int entryIndex, const char* pKey, T* pValueOut) const {
    s32 itemIndex = searchItemInfo(pKey);
    if (itemIndex < 0) {
        return false;
    }

    return getValueFast(entryIndex, itemIndex, pValueOut);
}

class TableIter {
public:
    TableIter() : mInfo(), mIndex(-1) {
    }

    TableIter(const Table* pInfo, s32 index) : mInfo(pInfo), mIndex(index) {
    }

    bool operator==(const TableIter& rIter) const {
        return mIndex == rIter.mIndex && mInfo != nullptr && rIter.mInfo != nullptr && *mInfo == *rIter.mInfo;
    }

    bool operator!=(const TableIter& rIter) const {
        return !(*this == rIter);
    }

    bool isValid() const {
        return mInfo != nullptr && mIndex >= 0 && mIndex < mInfo->getNumEntries();
    }

    template < typename T >
    bool getValue(const char* pKey, T* pValueOut) const {
        // An object made with no placement behind it (a mod's spawn), or a row a find did not find (end()): no value.
        if (!isValid()) {
            return false;
        }
        return mInfo->getValue(mIndex, pKey, pValueOut);
    }

    const Table* mInfo;
    s32 mIndex;
};

// The column is looked up once; a table without it has no such element.
template < typename T >
TableIter Table::findElement(const char* pKey, T searchValue, int startIndex) const {
    const s32 itemIndex = searchItemInfo(pKey);
    if (itemIndex < 0) {
        return end();
    }

    int entryIndex = startIndex;
    T value;

    while (entryIndex < getNumEntries()) {
        getValueFast(entryIndex, itemIndex, &value);
        if (compareValues< T >(value, searchValue)) {
            return TableIter(this, entryIndex);
        }

        entryIndex++;
    }

    return end();
}

TableIter Table::begin() const {
    return TableIter(this, 0);
}

TableIter Table::end() const {
    return TableIter(this, getNumEntries());
}
