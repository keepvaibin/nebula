#pragma once

#include "Game/System/NebulaArena.hpp"

class TalkMessageFuncBase {
public:
    virtual bool operator()(u32) const = 0;
    virtual TalkMessageFuncBase* clone() const = 0;
    virtual TalkMessageFuncBase* clone(NebulaArena*) const = 0;
};

template < typename T, typename U >
class TalkMessageFuncM : public TalkMessageFuncBase {
public:
    inline TalkMessageFuncM(T call, U callee) : mCaller(call), mCallee(callee) {
    }

    inline TalkMessageFuncM() {
    }

    virtual bool operator()(u32 arg) const {
        return (mCaller->*mCallee)(arg);
    }

    virtual TalkMessageFuncM* clone() const {
        return new TalkMessageFuncM(*this);
    }

    virtual TalkMessageFuncM* clone(NebulaArena* pArena) const {
        return new (pArena, 0) TalkMessageFuncM(*this);
    };

    T mCaller;
    U mCallee;
};

template < class T >
static TalkMessageFuncM< T*, bool (T::*)(u32) > TalkMessageFunc(T* pCaller, bool (T::*pFunc)(u32)) {
    return TalkMessageFuncM< T*, bool (T::*)(u32) >(pCaller, pFunc);
}

template < class T >
static inline TalkMessageFuncM< T*, bool (T::*)(u32) > TalkMessageFunc_Inline(T* pCaller, bool (T::*pFunc)(u32)) {
    return TalkMessageFuncM< T*, bool (T::*)(u32) >(pCaller, pFunc);
}
