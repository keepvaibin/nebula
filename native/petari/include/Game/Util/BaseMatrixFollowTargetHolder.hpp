#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/Array.hpp"
#include <JSystem/JGeometry/TMatrix.hpp>

class JMapLinkInfo;
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
    BaseMatrixFollowTarget(const JMapLinkInfo*);

    void set(LiveActor*, const TPos3f&, const TPos3f*, BaseMatrixFollowValidater*);
    const MtxPtr getHostBaseMtx() const;
    bool isValid(s32) const;

    TMtx34f _0;
    const TPos3f* _30;
    LiveActor* mActor;
    const JMapLinkInfo* mLinkInfo;
    BaseMatrixFollowValidater* mValidater;
};

class BaseMatrixFollower;

class BaseMatrixFollowTargetHolder : public NameObj {
public:
    BaseMatrixFollowTargetHolder(const char*, s32, s32);

    virtual ~BaseMatrixFollowTargetHolder();

    virtual void initAfterPlacement();
    virtual void movement();

    void addFollower(BaseMatrixFollower* pFollower);
    void setFollowTargetInfo(LiveActor* pActor, const JMapInfoIter& rIter, const TPos3f* pBaseMtx, BaseMatrixFollowValidater* pValidater);
    BaseMatrixFollowTarget* findFollowTarget(const JMapLinkInfo* pLinkInfo);
    BaseMatrixFollowTarget* findFollowTarget(const BaseMatrixFollower* pFollower);

    MR::Vector< MR::AssignableArray< BaseMatrixFollowTarget* > > mTargets;
    MR::Vector< MR::AssignableArray< BaseMatrixFollower* > > mFollowers;
};

class BaseMatrixFollower {
public:
    BaseMatrixFollower(NameObj*, const JMapInfoIter&);

    virtual void setGravityFollowHost(const NameObj*) {
    }

    virtual void update();

    NameObj* getFollowTargetActor() const;
    void calcFollowMatrix(TPos3f*) const;
    bool isEnableFollow() const;
    bool isValid() const;

    JMapLinkInfo* mLinkInfo;
    NameObj* mFollowerObj;
    BaseMatrixFollowTarget* mFollowTarget;
    s32 mFollowID;
};

namespace MR {
    template < typename T >
    BaseMatrixFollowValidater* createBaseMatrixFollowValidateDelegator(T* pHost, bool (T::*func)(s32) const) {
        return new BaseMatrixFollowValidateDelegator< T >(pHost, func);
    }

    bool isValidFollowID(const JMapInfoIter&);
    void addBaseMatrixFollower(BaseMatrixFollower*);
    void addBaseMatrixFollowTarget(LiveActor*, const JMapInfoIter&, const TPos3f*, BaseMatrixFollowValidater*);
};  // namespace MR
