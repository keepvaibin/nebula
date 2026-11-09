#pragma once

#include "Game/Util/Array.hpp"
#include "Game/Util/NameIndex.hpp"
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
    // The first object added that is called pName now; null for none.
    NameObj* find(const char* pName);
    // pObj, called pOld until now, has taken its current name (NameObj::setName).
    void renamed(NameObj* pObj, const char* pOld);

private:
    // An object's neighbours of the same name, in add order; -1 at the ends.
    struct Link {
        s32 mPrev;
        s32 mNext;
    };

    // The objects of one name: first and last in add order; -1 when none is called it now.
    struct Chain {
        s32 mHead;
        s32 mTail;
    };

    void link(s32 index, const char* pName);
    void unlink(s32 index, const char* pName);
    template < class T >
    T* grow(T* pArray, s32 count, s32 max);

    MR::Vector< MR::AssignableArray< NameObj* > > mObjArray1;
    MR::NameIndex mNames;  // name -> its chain in mChains
    Link* mLinks;          // by index in mObjArray1
    s32 mLinkMax;
    Chain* mChains;
    s32 mChainNum;
    s32 mChainMax;
};
