#include "Game/Util/Table.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include <cstring>

Table::Table() : mData(nullptr), mName("Undifined") {
}

Table::~Table() {
}

bool Table::attach(const void* pData) {
    if (pData == nullptr) {
        return false;
    }
    mData = static_cast< const TableData* >(pData);
    if (memcmp(mData->mMagic, "NTBL", 4) != 0) {
        const u8* pHead = static_cast< const u8* >(pData);
        NEBULA_PANIC("Table: not a cooked table (the importer cooks every table to NTBL): %02x%02x%02x%02x %02x%02x%02x%02x",
                     pHead[0], pHead[1], pHead[2], pHead[3], pHead[4], pHead[5], pHead[6], pHead[7]);
    }
    return true;
}

void Table::setName(const char* pName) {
    mName = pName;
}

const char* Table::getName() const {
    return mName;
}

s32 Table::searchItemInfo(const char* pKey) const {
    if (!dataExists()) {
        return -1;
    }

    const s32 nFields = getNumFields();
    const u32 hash = MR::getHashCode(pKey);
    const TableColumn* pColumns = mData->columns();

    for (int i = 0; i < nFields; ++i) {
        if (pColumns[i].mHash == hash) {
            return i;
        }
    }

    return -1;
}

s32 Table::getColumnKind(const char* pKey) const {
    const s32 itemId = searchItemInfo(pKey);
    return itemId < 0 ? -1 : mData->columns()[itemId].mKind;
}

TableIter Table::findElementBinary(const char* key, const char* value) const {
    // A null name is in no row.
    if (key == nullptr || value == nullptr) {
        return end();
    }

    int low = 0;
    int high = getNumEntries();

    const char* nextVal = nullptr;
    while (low < high) {
        int mid = (low + high) / 2;
        // A table without the key column has no such row.
        if (!getValue< const char* >(mid, key, &nextVal)) {
            return end();
        }

        const u8* pA = reinterpret_cast< const u8* >(nextVal);
        const u8* pB = reinterpret_cast< const u8* >(value);
        while (*pA != 0 && *pA == *pB) {
            pA++;
            pB++;
        }
        int comparison = static_cast< int >(*pA) - static_cast< int >(*pB);
        if (comparison == 0) {
            return TableIter(this, mid);
        } else if (comparison < 0) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return end();
}
