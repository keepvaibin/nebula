#include "Game/NameObj/NameObjGroup.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/ObjUtil.hpp"

NameObjGroup::NameObjGroup(const char* pName, int capacity)
    : NameObj(pName), mObjNumMax(capacity), mObjNum(0), mObjArray(new NameObj*[capacity]) {
}

NameObjGroup::~NameObjGroup() {
    delete[] mObjArray;
}

void NameObjGroup::registerObj(NameObj* pObj) {
    if (mObjNum >= mObjNumMax) {
        mObjArray = MR::growArray(mObjArray, mObjNumMax);
        mObjNumMax *= 2;
    }

    mObjArray[mObjNum] = pObj;
    mObjNum++;
}

void NameObjGroup::pauseOffAll() const {
    for (s32 i = 0; i < mObjNum; i++) {
        MR::requestMovementOn(mObjArray[i]);
    }
}
