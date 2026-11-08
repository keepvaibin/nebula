#include "Game/Player/MatrixControl.hpp"

MatrixControl::MatrixControl(const char* pName, MatrixMap* pMap, MatrixSelectList* pSelectList, s32 selectCount)
    : NameObj(pName), _C(pMap), _10(pSelectList), _18(selectCount), _1C(true), _1D() {
    for (u32 i = 0; i < selectCount; i++) {
        if (_10[i + 1].mCount > 2) {
            _1C = false;
            break;
        }
    }

    u32 size;
    for (size = 0; _C[size].mName[0] != '\0'; size++) {
        if (!_1C) {
            _C[size]._8 = new u8[8];

            for (u32 i = 0; i < 8; i++) {
                _C[size]._8[i] = (_C[size]._4 >> ((7 - i) * 4)) & 0xF;
            }
        }
    }

    mIndex.reset(size);
    for (u32 i = 0; i < size; i++) {
        mIndex.add(_C[i].mName, i);
    }
}

// getValue and getBit take a row the table has.
u8 MatrixControl::getValue(const char* pName, u8 value) const {
    return _C[mIndex.find(pName)]._8[value];
}

bool MatrixControl::getValueOrNone(const char* pName, u8 idx, u8* pValue) const {
    const u32 index = mIndex.find(pName);
    if (index == MR::NameIndex::cNoValue) {
        return false;
    }

    if (pValue != nullptr) {
        *pValue = _C[index]._8[idx];
    }

    return true;
}

bool MatrixControl::getBit(const char* pName, u8 bit) const {
    return _C[mIndex.find(pName)]._4 & (1 << (31 - bit));
}

bool MatrixControl::isExist(const char* pName) const {
    return mIndex.find(pName) != MR::NameIndex::cNoValue;
}

bool MatrixControl::getBitOrNone(const char* pName, u8 bit) const {
    const u32 index = mIndex.find(pName);
    if (index == MR::NameIndex::cNoValue) {
        return _1D;
    }

    return _C[index]._4 & (1 << (31 - bit));
}

MatrixValueGetter::MatrixValueGetter(const char* pName, MatrixValueTable* pTable) : NameObj(pName), _C(pTable) {
    u32 size;
    for (size = 0; _C[size].mName[0] != '\0'; size++) {
    }

    mIndex.reset(size);
    for (u32 i = 0; i < size; i++) {
        mIndex.add(_C[i].mName, i);
    }
}

bool MatrixValueGetter::getValue(const char* pName, f32* pValue) const {
    const u32 index = mIndex.find(pName);
    if (index == MR::NameIndex::cNoValue) {
        return false;
    }

    if (pValue != nullptr) {
        *pValue = _C[index].mValue;
    }

    return true;
}

MatrixControl::~MatrixControl() {
}

MatrixValueGetter::~MatrixValueGetter() {
}
