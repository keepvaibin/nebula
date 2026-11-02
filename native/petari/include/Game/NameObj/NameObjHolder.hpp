#pragma once

#include "Game/Util/Array.hpp"
#include <NebulaGpu.h>
#include <nebula_math.h>

class NameObj;

typedef void (NameObj::*NameObjMethod)(void);
typedef void (NameObj::*NameObjMethodConst)(void) const;

class NameObjHolder {
public:
    NameObjHolder(int num);

    void add(NameObj* pObj);
    void suspendAllObj();
    void resumeAllObj();
    void syncWithFlags();
    void callMethodAllObj(NameObjMethod pMethod);
    void clearArray();
    NameObj* find(const char* pName);

private:
    MR::Vector< MR::AssignableArray< NameObj* > > mObjArray1;
    MR::Vector< MR::FixedArray< NameObj*, 16 > > mObjArray2;
};
