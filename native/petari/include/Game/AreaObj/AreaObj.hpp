#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/Array.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class AreaForm;
class StageSwitchCtrl;

class AreaObj : public NameObj {
public:
    AreaObj(int, const char*);

    virtual void init(const JMapInfoIter&);

    virtual bool isInVolume(const TVec3f&) const;

    virtual const char* getManagerName() const {
        return mName;
    }

    void onSwitchA();
    void offSwitchA();
    bool isOnSwitchA() const;
    bool isOnSwitchB() const;
    bool isValidSwitchA() const;
    bool isValidSwitchB() const;
    void setFollowMtx(const TPos3f*);
    TPos3f* getFollowMtx() const;

    void validate() {
        mIsValid = true;
    }

    void invalidate() {
        mIsValid = false;
    }

    bool isValid() const {
        return mIsValid && _15 && mIsAwake;
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
    bool _15;
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
    AreaObjMgr(s32, const char*);

    void entry(AreaObj*);
    AreaObj* find_in(const TVec3f&) const;

    AreaObj* getAreaObj(int index) {
        return mArray[index];
    }

    s32 getNumAreaObj() const {
        return mArray.size();
    }

    MR::Vector< MR::AssignableArray< AreaObj* > > mArray;
    s32 _18;
};
