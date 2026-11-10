#include "Game/NameObj/NameObjGroup.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

NameObjGroup::NameObjGroup(const char* pName, int capacity)
    : NameObj(pName), mObjNumMax(capacity), mObjNum(0), mObjArray(new NameObj*[capacity]) {
    lumacore::setClass(this, lumacore::kClassId< NameObjGroup >);
}

NameObjGroup::~NameObjGroup() {
    delete[] mObjArray;
}

void NameObjGroup::registerObj(NameObj* pObj) {
    if (mObjNum >= mObjNumMax) {
        // A group made for 0 (ExterminationChecker with no children, a mod's FlipPanel group size) starts at 16.
        if (mObjNumMax == 0) {
            delete[] mObjArray;
            mObjArray = new (NebulaArena::findFromRoot(this), 0) NameObj*[16];
            mObjNumMax = 16;
        } else {
            mObjArray = MR::growArray(mObjArray, mObjNumMax);
            mObjNumMax *= 2;
        }
    }

    mObjArray[mObjNum] = pObj;
    mObjNum++;
}

void NameObjGroup::pauseOffAll() const {
    for (s32 i = 0; i < mObjNum; i++) {
        MR::requestMovementOn(mObjArray[i]);
    }
}
