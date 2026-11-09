#include "Game/NameObj/NameObjHolder.hpp"
#include "Game/NameObj/NameObj.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/ObjUtil.hpp"

NameObjHolder::NameObjHolder(int num)
    : mNames(), mLinks(nullptr), mLinkMax(0), mChains(nullptr), mChainNum(0), mChainMax(0) {
    mObjArray1.init(num);
}

// The arrays grow in the holder's arena, doubling.
template < class T >
T* NameObjHolder::grow(T* pArray, s32 count, s32 max) {
    T* pGrown = new (NebulaArena::findFromRoot(this), 0) T[max];
    for (s32 i = 0; i < count; i++) {
        pGrown[i] = pArray[i];
    }
    delete[] pArray;
    return pGrown;
}

void NameObjHolder::add(NameObj* pObj) {
    const s32 index = mObjArray1.size();
    mObjArray1.push_back(pObj);

    if (index >= mLinkMax) {
        const s32 max = mLinkMax > 0 ? mLinkMax * 2 : 256;
        mLinks = grow(mLinks, mLinkMax, max);
        mLinkMax = max;
    }
    pObj->mNameHolder = this;
    pObj->mHolderPlace = index;
    link(index, pObj->getName());
}

// Same-name objects stay in add order, so a chain's head is what a search in add order finds first.
void NameObjHolder::link(s32 index, const char* pName) {
    Link& rLink = mLinks[index];
    rLink.mPrev = -1;
    rLink.mNext = -1;
    if (pName == nullptr) {
        return;
    }

    u32 chain = mNames.find(pName);
    if (chain == MR::NameIndex::cNoValue) {
        if (mChainNum == mChainMax) {
            const s32 max = mChainMax > 0 ? mChainMax * 2 : 256;
            mChains = grow(mChains, mChainNum, max);
            mChainMax = max;
        }
        chain = static_cast< u32 >(mChainNum++);
        mChains[chain].mHead = -1;
        mChains[chain].mTail = -1;
        mNames.add(pName, chain);
    }

    Chain& rChain = mChains[chain];
    if (rChain.mTail < index) {
        rLink.mPrev = rChain.mTail;
        if (rChain.mTail >= 0) {
            mLinks[rChain.mTail].mNext = index;
        } else {
            rChain.mHead = index;
        }
        rChain.mTail = index;
        return;
    }

    s32 next = rChain.mHead;
    while (next < index) {
        next = mLinks[next].mNext;
    }
    rLink.mNext = next;
    rLink.mPrev = mLinks[next].mPrev;
    if (rLink.mPrev >= 0) {
        mLinks[rLink.mPrev].mNext = index;
    } else {
        rChain.mHead = index;
    }
    mLinks[next].mPrev = index;
}

void NameObjHolder::unlink(s32 index, const char* pName) {
    if (pName == nullptr) {
        return;
    }

    const u32 chain = mNames.find(pName);
    if (chain == MR::NameIndex::cNoValue) {
        return;
    }

    Chain& rChain = mChains[chain];
    const Link& rLink = mLinks[index];
    if (rLink.mPrev >= 0) {
        mLinks[rLink.mPrev].mNext = rLink.mNext;
    } else {
        rChain.mHead = rLink.mNext;
    }
    if (rLink.mNext >= 0) {
        mLinks[rLink.mNext].mPrev = rLink.mPrev;
    } else {
        rChain.mTail = rLink.mPrev;
    }
}

void NameObjHolder::renamed(NameObj* pObj, const char* pOld) {
    const s32 index = pObj->mHolderPlace;
    if (index < 0 || index >= mObjArray1.size() || mObjArray1[index] != pObj) {
        return;
    }

    unlink(index, pOld);
    link(index, pObj->getName());
}

void NameObjHolder::suspendAllObj() {
    for (int i = 0; i < mObjArray1.size(); i++) {
        MR::requestMovementOff(mObjArray1[i]);
    }
}

void NameObjHolder::resumeAllObj() {
    for (int i = 0; i < mObjArray1.size(); i++) {
        MR::requestMovementOn(mObjArray1[i]);
    }
}

void NameObjHolder::syncWithFlags() {
    callMethodAllObj(&NameObj::syncWithFlags);
}

void NameObjHolder::callMethodAllObj(NameObjMethod pMethod) {
    for (auto& item : mObjArray1) {
        (item->*pMethod)();
    }
}

// The objects may be gone already: none is touched. One that outlives this finds its place taken or past the end (renamed).
void NameObjHolder::clearArray() {
    mObjArray1.clear();
    mNames.reset(0);
    mChainNum = 0;
}

NameObj* NameObjHolder::find(const char* pName) {
    const u32 chain = mNames.find(pName);
    if (chain == MR::NameIndex::cNoValue || mChains[chain].mHead < 0) {
        return nullptr;
    }

    return mObjArray1[mChains[chain].mHead];
}
