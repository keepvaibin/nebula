#pragma once

#include "Game/NameObj/NameObj.hpp"

// Objects in join order; the array doubles when it fills.
class NameObjGroup : public NameObj {
public:
    // capacity > 0.
    NameObjGroup(const char* pName, int capacity);
    virtual ~NameObjGroup() override;

    NameObj* getObj(int index) const {
        return mObjArray[index];
    }

    s32 getObjNum() const {
        return mObjNum;
    }

    void registerObj(NameObj* pObj);
    void pauseOffAll() const;

private:
    s32 mObjNumMax;
    s32 mObjNum;
    NameObj** mObjArray;
};
