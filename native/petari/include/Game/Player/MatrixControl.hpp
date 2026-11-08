#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/NameIndex.hpp"

struct MatrixMap {
    const char* mName;
    u32 mBits;      // one nibble per column, the first column highest
    u8* mValues;    // the nibbles unpacked (tables built with values)
};

struct MatrixValueTable {
    const char* mName;
    f32 mValue;
};

class MatrixControl : public NameObj {
public:
    // `values`: the rows' nibbles are values (getValue); otherwise only bits (getBit).
    MatrixControl(const char* pName, MatrixMap* pMap, bool values);

    virtual ~MatrixControl();

    u8 getValue(const char*, u8) const;
    bool getValueOrNone(const char*, u8, u8*) const;
    bool getBit(const char*, u8) const;
    bool isExist(const char*) const;
    bool getBitOrNone(const char*, u8) const;

    MatrixMap* mMap;
    MR::NameIndex mIndex;  // a row's name to its index in mMap
};

class MatrixValueGetter : public NameObj {
public:
    MatrixValueGetter(const char*, MatrixValueTable*);

    virtual ~MatrixValueGetter();

    bool getValue(const char*, f32*) const;

    MatrixValueTable* mTable;
    MR::NameIndex mIndex;  // a row's name to its index in mTable
};
