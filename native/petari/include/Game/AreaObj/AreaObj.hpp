#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/Array.hpp"
#include <nebula_geometry.hpp>

class AreaForm;
class StageSwitchCtrl;

class AreaObj : public NameObj {
public:
    AreaObj(int, const char*);

    virtual void init(const TableIter&) override;

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

#if defined(NEBULA_PLAIN_MODULE)
    // Each area's form bound beside mArray, so find_in passes the far areas without reading them (P-152). Every override
    // of isInVolume is true only where AreaObj::isInVolume is, and that only inside the form.
    struct Bound {
        const AreaObj* mObj;   // the area the entry was made for: a list sorted since makes it again
        Vec3f mCenter;
        f32 mRadiusSquared;  // below 0: no bound
    };

    // setFollowMtx calls since the start: a form that follows a matrix has no bound, so every entry is made again.
    static u32 sFollowChanges;

private:
    void makeBound(u32 index) const;

    mutable MR::GrowBuffer< Bound > mBounds;
    mutable u32 mBoundsMade;    // the entries made, from the first
    mutable u32 mBoundsFollow;  // sFollowChanges when they were
#endif
};
