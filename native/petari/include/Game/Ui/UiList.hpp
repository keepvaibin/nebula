#pragma once

#include <NebulaGpu.h>

// An intrusive doubly linked list for the UI's objects: a pane's children, a group's panes, a layout's groups and
// animations, a pane's or material's animation links. Each element type is in one kind of list, so it holds the link
// as a member named mLink (UiLink<T>); the list keeps first, last and a count. Null ends both ways. Walk it with
// range-for (`for (Pane* pChild : rList)`) or First/Next; a walk that unlinks saves Next before it does.
template < class T >
struct UiLink {
    T* mNext = nullptr;
    T* mPrev = nullptr;
};

template < class T >
class UiList {
public:
    template < class P >
    class Iter {
    public:
        explicit Iter(P p) : mElement(p) {}
        P operator*() const { return mElement; }
        Iter& operator++() {
            mElement = mElement->mLink.mNext;
            return *this;
        }
        bool operator!=(const Iter& rOther) const { return mElement != rOther.mElement; }

    private:
        P mElement;
    };

    UiList() = default;
    UiList(const UiList&) = delete;
    UiList& operator=(const UiList&) = delete;
    // Unlinks what is left, so no element keeps pointers into a list that is gone.
    ~UiList() { Clear(); }

    T* First() const { return mFirst; }
    u32 Size() const { return mSize; }
    bool Empty() const { return mFirst == nullptr; }
    static T* Next(const T* p) { return p->mLink.mNext; }

    Iter< T* > begin() { return Iter< T* >(mFirst); }
    Iter< T* > end() { return Iter< T* >(nullptr); }
    Iter< const T* > begin() const { return Iter< const T* >(mFirst); }
    Iter< const T* > end() const { return Iter< const T* >(nullptr); }

    void PushBack(T* p) { InsertBefore(nullptr, p); }

    // Links p in front of pBefore, or at the end when pBefore is null.
    void InsertBefore(T* pBefore, T* p) {
        T* const pPrev = pBefore != nullptr ? pBefore->mLink.mPrev : mLast;
        p->mLink.mPrev = pPrev;
        p->mLink.mNext = pBefore;
        (pPrev != nullptr ? pPrev->mLink.mNext : mFirst) = p;
        (pBefore != nullptr ? pBefore->mLink.mPrev : mLast) = p;
        ++mSize;
    }

    void Erase(T* p) {
        T* const pNext = p->mLink.mNext;
        T* const pPrev = p->mLink.mPrev;
        (pPrev != nullptr ? pPrev->mLink.mNext : mFirst) = pNext;
        (pNext != nullptr ? pNext->mLink.mPrev : mLast) = pPrev;
        p->mLink.mNext = nullptr;
        p->mLink.mPrev = nullptr;
        --mSize;
    }

    void Clear() {
        for (T *p = mFirst, *pNext; p != nullptr; p = pNext) {
            pNext = p->mLink.mNext;
            p->mLink.mNext = nullptr;
            p->mLink.mPrev = nullptr;
        }
        mFirst = nullptr;
        mLast = nullptr;
        mSize = 0;
    }

private:
    T* mFirst = nullptr;
    T* mLast = nullptr;
    u32 mSize = 0;
};
