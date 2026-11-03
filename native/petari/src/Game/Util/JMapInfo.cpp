#include "Game/Util/JMapInfo.hpp"
#include "Game/Util/StringUtil.hpp"
#include <JSystem/JGadget/hashcode.hpp>
#include <cstring>
#include "Game/Util/ByteView.hpp"

JMapInfo::JMapInfo() {
    mData = nullptr;
    mName = "Undifined";
}

JMapInfo::~JMapInfo() {
}

bool JMapInfo::attach(const void* pData) {
    if (pData == nullptr) {
        return false;
    }
    mData = static_cast< const JMapData* >(pData);
    // A table the importer cooked reads its field count below 64 K; one still big-endian, at least 16 M.
    if (static_cast< u32 >(mData->mNumFields) >= 0x10000) {
        const u8* pHead = static_cast< const u8* >(pData);
        NEBULA_PANIC("JMapInfo: a table not in host order (the importer cooks it): %02x%02x%02x%02x %02x%02x%02x%02x "
                              "%02x%02x%02x%02x %02x%02x%02x%02x",
                              pHead[0], pHead[1], pHead[2], pHead[3], pHead[4], pHead[5], pHead[6], pHead[7], pHead[8],
                              pHead[9], pHead[10], pHead[11], pHead[12], pHead[13], pHead[14], pHead[15]);
    }
    return true;
}

void JMapInfo::setName(const char* pName) {
    mName = pName;
}

const char* JMapInfo::getName() const {
    return mName;
}

s32 JMapInfo::searchItemInfo(const char* pKey) const {
    if (!dataExists()) {
        return -1;
    }

    s32 nFields = getNumFields();
    u32 hash = JGadget::getHashCode(pKey);

    for (int i = 0; i < nFields; ++i) {
        if (mData->mItems[i].mHash == hash) {
            return i;
        }
    }

    return -1;
}

s32 JMapInfo::getValueType(const char* pItem) const {
    s32 itemId = searchItemInfo(pItem);

    if (itemId < 0) {
        return JMAP_VALUE_TYPE_NULL;
    }
    return mData->mItems[itemId].mType;
}

bool JMapInfo::getValueFast(int entryIndex, int itemIndex, const char** pValueOut) const {
    const JMapItem* item = &mData->mItems[itemIndex];
    const char* valuePtr = getEntryAddress(mData, mData->mDataOffset, entryIndex) + item->mOffsData;

    switch (item->mType) {
    case JMAP_VALUE_TYPE_STRING_PTR: {
        const char* pStringTable = getEntryAddress(mData, mData->mDataOffset, getNumEntries());
        u32 stringOffset;
        memcpy(&stringOffset, valuePtr, sizeof(stringOffset));  // a table's columns are packed
        *pValueOut = pStringTable + stringOffset;
        break;
    }
    default:
        *pValueOut = valuePtr;
        break;
    }

    return true;
}

bool JMapInfo::getValueFast(int entryIndex, int itemIndex, u32* pValueOut) const {
    const JMapItem* item = &mData->mItems[itemIndex];
    const char* valuePtr = getEntryAddress(mData, mData->mDataOffset, entryIndex) + item->mOffsData;

    u32 rawValue;
    switch (item->mType) {
    case JMAP_VALUE_TYPE_LONG:
    case JMAP_VALUE_TYPE_LONG_2:
        rawValue = readBytes< u32 >(valuePtr);
        break;
    case JMAP_VALUE_TYPE_SHORT:
        rawValue = readBytes< u16 >(valuePtr);
        break;
    case JMAP_VALUE_TYPE_BYTE:
        rawValue = readBytes< u8 >(valuePtr);
        break;
    default:
        return false;
    }

    *pValueOut = (rawValue & item->mMask) >> item->mShift;
    return true;
}

bool JMapInfo::getValueFast(int entryIndex, int itemIndex, s32* pValueOut) const {
    const JMapItem* item = &mData->mItems[itemIndex];
    if (item->mShift != 0) {
        goto FAIL;
    }
    const char* valuePtr;
    valuePtr = getEntryAddress(mData, mData->mDataOffset, entryIndex) + item->mOffsData;

    switch (item->mType) {
    case JMAP_VALUE_TYPE_LONG:
        if (item->mMask == 0xffffffff) {
            *pValueOut = readBytes< s32 >(valuePtr);
            break;
        }
        goto FAIL;
    case JMAP_VALUE_TYPE_SHORT:
        if (item->mMask == 0xffff) {
            *pValueOut = readBytes< s16 >(valuePtr);
            break;
        }
        goto FAIL;
    case JMAP_VALUE_TYPE_BYTE:
        if (item->mMask == 0xff) {
            *pValueOut = readBytes< s8 >(valuePtr);
            break;
        }
        goto FAIL;
    default:
        goto FAIL;
    }
    return true;
FAIL:
    return false;
}

JMapInfoIter MR::findJMapInfoElementNoCase(const JMapInfo* pInfo, const char* key, const char* searchValue, int startIndex) {
    int entryIndex = startIndex;
    const char* value;
    while (entryIndex < pInfo->getNumEntries()) {
        pInfo->getValue< const char* >(entryIndex, key, &value);
        if (MR::isEqualStringCase(value, searchValue)) {
            return JMapInfoIter(pInfo, entryIndex);
        }
        entryIndex++;
    }
    return pInfo->end();
}

JMapInfoIter JMapInfo::findElementBinary(const char* key, const char* value) const {
    int low = 0;
    int high = getNumEntries();

    const char* nextVal;
    while (low < high) {
        int mid = (low + high) / 2;
        getValue< const char* >(mid, key, &nextVal);
        int comparison = strcmp(nextVal, value);
        if (comparison == 0) {
            return JMapInfoIter(this, mid);
        } else if (comparison < 0) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return end();
}
