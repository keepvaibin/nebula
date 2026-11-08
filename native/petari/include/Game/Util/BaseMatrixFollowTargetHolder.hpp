#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/Array.hpp"
#include <nebula_geometry.hpp>

class PlacementLink;
class LiveActor;

class BaseMatrixFollowValidater {
public:
    virtual bool isValid(s32) const = 0;
};

template < typename T >
class BaseMatrixFollowValidateDelegator : public BaseMatrixFollowValidater {
public:
    typedef bool (T::*Func)(s32) const;

    BaseMatrixFollowValidateDelegator(T* pHost, Func func) : mHost(pHost), mFunc(func) {
    }

    virtual bool isValid(s32 id) const {
        return (mHost->*mFunc)(id);
    }

    T* mHost;
    Func mFunc;
};

class BaseMatrixFollowTarget {
public:
    BaseMatrixFollowTarget(const PlacementLink*);

    void set(LiveActor*, const Pos3f&, const Pos3f*, BaseMatrixFollowValidater*);
    const MtxPtr getHostBaseMtx() const;
    bool isValid(s32) const;

    Mtx34f mInvHostMtx;
    const Pos3f* mHostBaseMtx;
    LiveActor* mActor;
    const PlacementLink* mLinkInfo;
    BaseMatrixFollowValidater* mValidater;
};

class BaseMatrixFollower;

class BaseMatrixFollowTargetHolder : public NameObj {
public:
    BaseMatrixFollowTargetHolder(const char*, s32, s32);

    virtual void initAfterPlacement();
    virtual void movement();

    void addFollower(BaseMatrixFollower* pFollower);
    void setFollowTargetInfo(LiveActor* pActor, const TableIter& rIter, const Pos3f* pBaseMtx, BaseMatrixFollowValidater* pValidater);
    BaseMatrixFollowTarget* findFollowTarget(const PlacementLink* pLinkInfo);
    BaseMatrixFollowTarget* findFollowTarget(const BaseMatrixFollower* pFollower);

    MR::Vector< MR::AssignableArray< BaseMatrixFollowTarget* > > mTargets;
    MR::Vector< MR::AssignableArray< BaseMatrixFollower* > > mFollowers;
};

class BaseMatrixFollower {
public:
    explicit BaseMatrixFollower(const TableIter&);

    virtual void setGravityFollowHost(const NameObj*) {
    }

    virtual void update();

    NameObj* getFollowTargetActor() const;
    void calcFollowMatrix(Pos3f*) const;
    bool isEnableFollow() const;
    bool isValid() const;

    PlacementLink* mLinkInfo;
    BaseMatrixFollowTarget* mFollowTarget;
    s32 mFollowID;
};

namespace MR {
    template < typename T >
    BaseMatrixFollowValidater* createBaseMatrixFollowValidateDelegator(T* pHost, bool (T::*func)(s32) const) {
        return new BaseMatrixFollowValidateDelegator< T >(pHost, func);
    }

    bool isValidFollowID(const TableIter&);
    void addBaseMatrixFollower(BaseMatrixFollower*);
    void addBaseMatrixFollowTarget(LiveActor*, const TableIter&, const Pos3f*, BaseMatrixFollowValidater*);
};  // namespace MR
