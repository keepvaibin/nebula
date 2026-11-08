#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/NameIndex.hpp"

struct MatrixMap {
    const char* mName;
    u32 _4;
    u8* _8;
};

struct MatrixSelectList {
    u8 mCount;
    const char* _4[32];
};

struct MatrixValueTable {
    const char* mName;
    f32 mValue;
};

class MatrixControl : public NameObj {
public:
    MatrixControl(const char*, MatrixMap*, MatrixSelectList*, s32);

    virtual ~MatrixControl();

    u8 getValue(const char*, u8) const;
    bool getValueOrNone(const char*, u8, u8*) const;
    bool getBit(const char*, u8) const;
    bool isExist(const char*) const;
    bool getBitOrNone(const char*, u8) const;

    MatrixMap* _C;
    MatrixSelectList* _10;
    MR::NameIndex mIndex;  // a row's name to its index in _C
    s32 _18;
    bool _1C;
    bool _1D;
};

class MatrixValueGetter : public NameObj {
public:
    MatrixValueGetter(const char*, MatrixValueTable*);

    virtual ~MatrixValueGetter();

    bool getValue(const char*, f32*) const;

    MatrixValueTable* _C;
    MR::NameIndex mIndex;  // a row's name to its index in _C
};
