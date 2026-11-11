#include "Game/Player/MatrixControl.hpp"
#include "Game/System/LumaCore.hpp"

MatrixControl::MatrixControl(const char* pName, MatrixMap* pMap, bool values) : NameObj(pName), mMap(pMap) {
    lumacore::setClass(this, lumacore::kClassId< MatrixControl >);
    u32 size;
    for (size = 0; mMap[size].mName[0] != '\0'; size++) {
        if (values) {
            mMap[size].mValues = new u8[8];

            for (u32 i = 0; i < 8; i++) {
                mMap[size].mValues[i] = (mMap[size].mBits >> ((7 - i) * 4)) & 0xF;
            }
        }
    }

    mIndex.reset(size);
    for (u32 i = 0; i < size; i++) {
        mIndex.add(mMap[i].mName, i);
    }
}

// getValue and getBit take a row the table has.
u8 MatrixControl::getValue(const char* pName, u8 value) const {
    return mMap[mIndex.find(pName)].mValues[value];
}

bool MatrixControl::getValueOrNone(const char* pName, u8 idx, u8* pValue) const {
    const u32 index = mIndex.find(pName);
    if (index == MR::NameIndex::cNoValue) {
        return false;
    }

    if (pValue != nullptr) {
        *pValue = mMap[index].mValues[idx];
    }

    return true;
}

bool MatrixControl::getBit(const char* pName, u8 bit) const {
    return mMap[mIndex.find(pName)].mBits & (1 << (31 - bit));
}

bool MatrixControl::isExist(const char* pName) const {
    return mIndex.find(pName) != MR::NameIndex::cNoValue;
}

bool MatrixControl::getBitOrNone(const char* pName, u8 bit) const {
    const u32 index = mIndex.find(pName);
    if (index == MR::NameIndex::cNoValue) {
        return false;
    }

    return mMap[index].mBits & (1 << (31 - bit));
}

MatrixValueGetter::MatrixValueGetter(const char* pName, MatrixValueTable* pTable) : NameObj(pName), mTable(pTable) {
    lumacore::setClass(this, lumacore::kClassId< MatrixValueGetter >);
    u32 size;
    for (size = 0; mTable[size].mName[0] != '\0'; size++) {
    }

    mIndex.reset(size);
    for (u32 i = 0; i < size; i++) {
        mIndex.add(mTable[i].mName, i);
    }
}

bool MatrixValueGetter::getValue(const char* pName, f32* pValue) const {
    const u32 index = mIndex.find(pName);
    if (index == MR::NameIndex::cNoValue) {
        return false;
    }

    if (pValue != nullptr) {
        *pValue = mTable[index].mValue;
    }

    return true;
}

MatrixControl::~MatrixControl() {
}

MatrixValueGetter::~MatrixValueGetter() {
}
