#pragma once

#include <nebula_types.h>

template < class T >
class NebulaList;

// A node of an intrusive doubly linked list: T derives from NebulaListLink< T >, and the object is the link's T. A link is in at
// most one list; destroying it takes it out.
template < class T >
class NebulaListLink {
public:
    NebulaListLink() = default;
    NebulaListLink(const NebulaListLink&) = delete;
    NebulaListLink& operator=(const NebulaListLink&) = delete;
    ~NebulaListLink() {
        if (mList != nullptr) {
            mList->remove(this);
        }
    }

    T* getObject() {
        return static_cast< T* >(this);
    }
    const T* getObject() const {
        return static_cast< const T* >(this);
    }
    NebulaListLink* getNext() const {
        return mNext;
    }
    NebulaListLink* getPrev() const {
        return mPrev;
    }

private:
    friend class NebulaList< T >;

    NebulaList< T >* mList = nullptr;
    NebulaListLink* mPrev = nullptr;
    NebulaListLink* mNext = nullptr;
};

// Adding a link that is in a list moves it; remove() is false when the link is not in this list.
template < class T >
class NebulaList {
public:
    using Link = NebulaListLink< T >;

    NebulaList() = default;
    NebulaList(const NebulaList&) = delete;
    NebulaList& operator=(const NebulaList&) = delete;
    ~NebulaList() {
        for (Link* link = mHead; link != nullptr; link = link->mNext) {
            link->mList = nullptr;
        }
    }

    Link* getFirst() const {
        return mHead;
    }
    Link* getLast() const {
        return mTail;
    }
    // The link after the last.
    Link* getEnd() const {
        return nullptr;
    }
    u32 getNum() const {
        return mNum;
    }

    void append(Link* pLink) {
        takeOut(pLink);
        pLink->mList = this;
        pLink->mPrev = mTail;
        pLink->mNext = nullptr;
        if (mTail != nullptr) {
            mTail->mNext = pLink;
        } else {
            mHead = pLink;
        }
        mTail = pLink;
        mNum++;
    }

    void prepend(Link* pLink) {
        takeOut(pLink);
        pLink->mList = this;
        pLink->mPrev = nullptr;
        pLink->mNext = mHead;
        if (mHead != nullptr) {
            mHead->mPrev = pLink;
        } else {
            mTail = pLink;
        }
        mHead = pLink;
        mNum++;
    }

    // pLink goes before pBefore (a link of this list, or getEnd() for the back).
    void insert(Link* pBefore, Link* pLink) {
        if (pBefore == mHead) {
            prepend(pLink);
            return;
        }
        if (pBefore == nullptr) {
            append(pLink);
            return;
        }
        takeOut(pLink);
        Link* prev = pBefore->mPrev;
        pLink->mList = this;
        pLink->mPrev = prev;
        pLink->mNext = pBefore;
        prev->mNext = pLink;
        pBefore->mPrev = pLink;
        mNum++;
    }

    bool remove(Link* pLink) {
        if (pLink->mList != this) {
            return false;
        }
        if (pLink->mPrev != nullptr) {
            pLink->mPrev->mNext = pLink->mNext;
        } else {
            mHead = pLink->mNext;
        }
        if (pLink->mNext != nullptr) {
            pLink->mNext->mPrev = pLink->mPrev;
        } else {
            mTail = pLink->mPrev;
        }
        pLink->mList = nullptr;
        mNum--;
        return true;
    }

private:
    static void takeOut(Link* pLink) {
        if (pLink->mList != nullptr) {
            pLink->mList->remove(pLink);
        }
    }

    Link* mHead = nullptr;
    Link* mTail = nullptr;
    u32 mNum = 0;
};
