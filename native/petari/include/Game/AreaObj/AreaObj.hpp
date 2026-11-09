#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/Array.hpp"
#include <nebula_geometry.hpp>

class AreaForm;
class StageSwitchCtrl;

class AreaObj : public NameObj {
public:
    AreaObj(int, const char*);

    virtual void init(const TableIter&);

    virtual bool isInVolume(const Vec3f&) const;

    virtual const char* getManagerName() const {
        return mName;
    }

    void onSwitchA();
    void offSwitchA();
    bool isOnSwitchA() const;
    bool isOnSwitchB() const;
    bool isValidSwitchA() const;
    bool isValidSwitchB() const;
    void setFollowMtx(const Pos3f*);
    Pos3f* getFollowMtx() const;

    void validate() {
        mIsValid = true;
    }

    void invalidate() {
        mIsValid = false;
    }

    bool isValid() const {
        return mIsValid && mIsFollowValid && mIsAwake;
    }

    void awake() {
        mIsAwake = true;
    }

    void sleep() {
        mIsAwake = false;
    }

    template < typename T >
    T* getForm() const {
        return static_cast< T* >(mForm);
    }

    AreaForm* mForm;
    int mFormType;
    bool mIsValid;
    bool mIsFollowValid;
    bool mIsAwake;
    s32 mObjArg0;
    s32 mObjArg1;
    s32 mObjArg2;
    s32 mObjArg3;
    s32 mObjArg4;
    s32 mObjArg5;
    s32 mObjArg6;
    s32 mObjArg7;
    StageSwitchCtrl* mSwitchCtrl;
};

class AreaObjMgr : public NameObj {
public:
    AreaObjMgr(s32 firstCapacity, const char* pName);

    void entry(AreaObj*);
    AreaObj* find_in(const Vec3f&) const;

    AreaObj* getAreaObj(int index) {
        return mArray[index];
    }

    s32 getNumAreaObj() const {
        return mArray.size();
    }

    MR::Vector< MR::AssignableArray< AreaObj* > > mArray;
    s32 mFirstCapacity;  // entry grows past it
};
