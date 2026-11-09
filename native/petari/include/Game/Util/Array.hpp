#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>
#include "Game/System/NebulaArena.hpp"

namespace MR {
    /// @brief Room for a list with no limit that can also be a static: no constructor (zero-initialised), it grows to at
    /// least `count` (doubling, from 16), keeps what it held, and never shrinks. It grows in the arena that holds it, so a
    /// member goes with its owner's arena; a static's room is the root arena's. Pointers into it do not survive a call
    /// that grows it.
    template < class T >
    struct GrowBuffer {
        T* mItems;
        u32 mRoom;

        T* room(u32 count) {
            if (count > mRoom) {
                u32 room = mRoom != 0 ? mRoom : 16;
                while (room < count) {
                    room *= 2;
                }
                NebulaArena* const pHome = NebulaArena::findFromRoot(this);
                T* const pNew = new (pHome != nullptr ? pHome : NebulaArena::root(), 0) T[room];
                for (u32 i = 0; i < mRoom; i++) {
                    pNew[i] = mItems[i];
                }
                delete[] mItems;
                mItems = pNew;
                mRoom = room;
            }
            return mItems;
        }

        T& operator[](u32 index) {
            return mItems[index];
        }

        const T& operator[](u32 index) const {
            return mItems[index];
        }
    };

    /// @brief Scratch for `count` items: room for N in itself, from the current arena past that, freed at scope end.
    template < class T, int N >
    class LocalArray {
    public:
        explicit LocalArray(u32 count) : mItems(count <= static_cast< u32 >(N) ? mRoom : new T[count]) {
        }

        ~LocalArray() {
            if (mItems != mRoom) {
                delete[] mItems;
            }
        }

        LocalArray(const LocalArray&) = delete;
        LocalArray& operator=(const LocalArray&) = delete;

        T& operator[](u32 index) {
            return mItems[index];
        }

        T mRoom[N];
        T* const mItems;
    };

    /// @brief A full table: twice the room, in the arena it lies in, and the old one goes. Pointers into it do not survive this.
    template < class T >
    T* growArray(T* pOld, s32 num) {
        T* const pNew = new (NebulaArena::findFromRoot(pOld), 0) T[num * 2];  // num is never 0: see AssignableArray::grow
        for (s32 i = 0; i < num; i++) {
            pNew[i] = pOld[i];
        }
        delete[] pOld;
        return pNew;
    }

    /// @brief An arena array of mMaxSize; grow() doubles it (16 when empty).
    template < class T >
    class AssignableArray {
    public:
        typedef T Item;

        AssignableArray() {
            mArr = 0;
            mMaxSize = 0;
        }

        AssignableArray(s32 num) {
            init(num);
        }

        ~AssignableArray();

        T& operator[](int idx) {
            return mArr[idx];
        }

        const T& operator[](int idx) const {
            return mArr[idx];
        }

        void init(s32 num) {
            mArr = new T[num];
            mMaxSize = num;
        }

        // From empty, in the arena that holds this array (the current one for a static): a scene's list that first gets an
        // item in a frame stays the scene's.
        void grow() {
            if (mMaxSize == 0) {
                delete[] mArr;
                mArr = new (NebulaArena::findFromRoot(this), 0) T[16];
                mMaxSize = 16;
                return;
            }

            mArr = growArray(mArr, mMaxSize);
            mMaxSize *= 2;
        }

        int size() const {
            return mMaxSize;
        }

        T* begin() {
            return &mArr[0];
        }

        const T* begin() const {
            return &mArr[0];
        }

        T* rbegin() {
            return &mArr[mMaxSize - 1];
        }

        const T* rbegin() const {
            return &mArr[mMaxSize - 1];
        }

        T* end() {
            return &mArr[mMaxSize];
        }

        const T* end() const {
            return &mArr[mMaxSize];
        }

        T* rend() {
            return &mArr[0] - 1;
        }

        const T* rend() const {
            return &mArr[0] - 1;
        }

        T* mArr;
        s32 mMaxSize;
    };

    /// @brief A table filled by index, its size set by the level's data: room for N in itself, and at() moves it to twice
    /// the room in the arena it lies in when an index passes the end. Entries start zero. Pointers taken into it last until
    /// the next at().
    template < class T, int N >
    class GrowTable {
    public:
        GrowTable() : mMoved(nullptr), mSize(N) {
            for (s32 i = 0; i < N; i++) {
                mArr[i] = T();
            }
        }

        T& at(s32 idx) {
            while (idx >= mSize) {
                T* const pNew = new (NebulaArena::findFromRoot(this), 0) T[mSize * 2];
                for (s32 i = 0; i < mSize; i++) {
                    pNew[i] = begin()[i];
                }
                for (s32 i = mSize; i < mSize * 2; i++) {
                    pNew[i] = T();
                }
                delete[] mMoved;
                mMoved = pNew;
                mSize *= 2;
            }
            return begin()[idx];
        }

        s32 size() const {
            return mSize;
        }

        T* begin() {
            return mMoved != nullptr ? mMoved : mArr;
        }

        const T* begin() const {
            return mMoved != nullptr ? mMoved : mArr;
        }

        operator T*() {
            return begin();
        }

        operator const T*() const {
            return begin();
        }

    private:
        T mArr[N];
        T* mMoved;
        s32 mSize;
    };

    /// @brief A contiguous array with room for N in itself; grow() moves it to twice the room in the arena it lies in (an
    /// InlineVector does when full).
    template < class T, int N >
    class InlineArray {
    public:
        typedef T Item;

        void grow() {
            const s32 num = size();
            T* const pNew = new (NebulaArena::findFromRoot(this), 0) T[num * 2];
            for (s32 i = 0; i < num; i++) {
                pNew[i] = (*this)[i];
            }
            delete[] mMoved;
            mMoved = pNew;
            mMovedSize = num * 2;
        }

        T& operator[](int idx) {
            return mMoved != nullptr ? mMoved[idx] : mArr[idx];
        }

        const T& operator[](int idx) const {
            return mMoved != nullptr ? mMoved[idx] : mArr[idx];
        }

        int size() const {
            return mMoved != nullptr ? mMovedSize : N;
        }

        T* begin() {
            return &(*this)[0];
        }

        const T* begin() const {
            return &(*this)[0];
        }

        T* end() {
            return begin() + size();
        }

        const T* end() const {
            return begin() + size();
        }

        inline int indexOf(T* pItem) const {
            return pItem - begin();
        }

    private:
        T mArr[N];
        T* mMoved = nullptr;
        s32 mMovedSize = 0;
    };

    /// @brief mCount items over a storage T (AssignableArray or InlineArray); push_back and insert grow the storage when full.
    template < class T >
    class Vector {
    public:
        typedef typename T::Item Item;

        Vector();

        ~Vector();

        Item& operator[](int idx) {
            return mArray[idx];
        }

        const Item& operator[](int idx) const {
            return mArray[idx];
        }

        void init(s32 num) {
            mArray.init(num);
        }

        int size() const {
            return mCount;
        }

        int capacity() const {
            return mArray.size();
        }

        void push_back(const Item& rItem);

        Item* insert(Item* pIter, const Item& rItem) {
            if (mCount >= mArray.size()) {
                const s32 index = pIter - begin();
                mArray.grow();
                pIter = begin() + index;
            }

            for (Item* p = end(); p != pIter; p--) {
                *p = *(p - 1);
            }

            *pIter = rItem;
            mCount++;
            return pIter;
        }

        void pop_back() {
            mCount--;
        }

        // Keeps the order; returns the position that now holds the next item.
        Item* erase(Item* pIter) {
            if (end() - pIter - 1 > 0) {
                for (Item* p = pIter; p + 1 != end(); p++) {
                    *p = *(p + 1);
                }
            }

            mCount--;

            return pIter;
        }

        void clear() {
            mCount = 0;
        }

        Item* begin() {
            return &mArray[0];
        }

        const Item* begin() const {
            return &mArray[0];
        }

        Item* rbegin() {
            return end() - 1;
        }

        const Item* rbegin() const {
            return end() - 1;
        }

        Item* end() {
            return &mArray[mCount];
        }

        const Item* end() const {
            return &mArray[mCount];
        }

        Item* rend() {
            return begin() - 1;
        }

        const Item* rend() const {
            return begin() - 1;
        }

        T mArray;
        s32 mCount;
    };

    /// @brief A Vector with room for N in itself before it moves to the arena it lies in.
    template < class T, int N >
    using InlineVector = Vector< InlineArray< T, N > >;

    template < class T >
    AssignableArray< T >::~AssignableArray() {
        if (mArr != nullptr) {
            delete[] mArr;
        }
    }

    template < class T >
    Vector< T >::Vector() {
        clear();
    }

    template < class T >
    Vector< T >::~Vector() {
    }

    template < class T >
    void Vector< T >::push_back(const Item& rItem) {
        if (mCount >= mArray.size()) {
            const Item item = rItem;  // it may lie in the array
            mArray.grow();
            mArray[mCount++] = item;
            return;
        }

        mArray[mCount++] = rItem;
    }
};  // namespace MR
