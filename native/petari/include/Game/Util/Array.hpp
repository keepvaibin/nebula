#pragma once

#include <NebulaGpu.h>
#include <nebula_math.h>
#include "Game/System/NebulaArena.hpp"

namespace MR {
    /// @brief A full table: twice the room, in the heap it lies in, and the old one goes. Pointers into it do not survive this.
    template < class T >
    T* growArray(T* pOld, s32 num) {
        T* const pNew = new (NebulaArena::findFromRoot(pOld), 0) T[num * 2];  // num is never 0: see AssignableArray::grow
        for (s32 i = 0; i < num; i++) {
            pNew[i] = pOld[i];
        }
        delete[] pOld;
        return pNew;
    }

    /// @brief A contiguous reallocatable array.
    template < class T >
    class AssignableArray {
    public:
        typedef T Item;

        /// @brief Creates a new `AssignableArray`.
        AssignableArray() {
            mArr = 0;
            mMaxSize = 0;
        }

        /// @brief Creates a new `AssignableArray`.
        /// @param num The number of elements.
        AssignableArray(s32 num) {
            init(num);
        }

        /// @brief Destroys the `AssignableArray`.
        ~AssignableArray();

        /// @brief Returns the element at the given position.
        /// @param idx The position of the element.
        /// @return The reference to the element.
        T& operator[](int idx) {
            return mArr[idx];
        }

        /// @brief Returns the element at the given position.
        /// @param idx The position of the element.
        /// @return The reference to the element.
        const T& operator[](int idx) const {
            return mArr[idx];
        }

        /// @brief Allocates memory for the array.
        /// @param num The number of elements.
        void init(s32 num) {
            mArr = new T[num];
            mMaxSize = num;
        }

        /// @brief Makes room for more elements.
        void grow() {
            if (mMaxSize == 0) {
                init(16);
                return;
            }

            mArr = growArray(mArr, mMaxSize);
            mMaxSize *= 2;
        }

        /// @brief Returns the number of elements.
        /// @return The number of elements.
        int size() const {
            return mMaxSize;
        }

        /// @brief Returns an iterator to the first element.
        /// @return The pointer to the first element.
        T* begin() {
            return &mArr[0];
        }

        /// @brief Returns an iterator to the first element.
        /// @return The pointer to the first element.
        const T* begin() const {
            return &mArr[0];
        }

        /// @brief Returns an iterator to the reverse first element.
        /// @return The pointer to the reverse first element.
        T* rbegin() {
            return &mArr[mMaxSize - 1];
        }

        /// @brief Returns an iterator to the reverse first element.
        /// @return The pointer to the reverse first element.
        const T* rbegin() const {
            return &mArr[mMaxSize - 1];
        }

        /// @brief Returns an iterator past the last element.
        /// @return The pointer past the last element.
        T* end() {
            return &mArr[mMaxSize];
        }

        /// @brief Returns an iterator past the last element.
        /// @return The pointer past the last element.
        const T* end() const {
            return &mArr[mMaxSize];
        }

        /// @brief Returns an iterator past the reverse last element.
        /// @return The pointer past the reverse last element.
        T* rend() {
            return &mArr[0] - 1;
        }

        /// @brief Returns an iterator past the reverse last element.
        /// @return The pointer past the reverse last element.
        const T* rend() const {
            return &mArr[0] - 1;
        }

        /// @brief The pointer to the array of elements.
        T* mArr;

        /// @brief The number of elements.
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

    /// @brief A contiguous array with room for N in itself; a Vector over it past that moves to the arena it lies in.
    template < class T, int N >
    class FixedArray {
    public:
        typedef T Item;
        template < class X >
        struct RemovePtr {
            typedef X Type;
        };
        template < class X >
        struct RemovePtr< X* > {
            typedef X Type;
        };
        typedef typename RemovePtr< T >::Type Base;

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

        /// @brief Returns the element at the given position.
        /// @param idx The position of the element.
        /// @return The reference to the element.
        T& operator[](int idx) {
            return mMoved != nullptr ? mMoved[idx] : mArr[idx];
        }

        /// @brief Returns the element at the given position.
        /// @param idx The position of the element.
        /// @return The reference to the element.
        const T& operator[](int idx) const {
            return mMoved != nullptr ? mMoved[idx] : mArr[idx];
        }

        /// @brief Returns the number of elements.
        /// @return The number of elements.
        int size() const {
            return mMoved != nullptr ? mMovedSize : N;
        }

        /// @brief Returns an iterator to the first element.
        /// @return The pointer to the first element.
        T* begin() {
            return &(*this)[0];
        }

        /// @brief Returns an iterator to the first element.
        /// @return The pointer to the first element.
        const T* begin() const {
            return &(*this)[0];
        }

        /// @brief Returns an iterator past the last element.
        /// @return The pointer past the last element.
        T* end() {
            return begin() + size();
        }

        /// @brief Returns an iterator past the last element.
        /// @return The pointer past the last element.
        const T* end() const {
            return begin() + size();
        }

        template < class U >
        void callAllFunc(void (U::*pFunction)()) {
            for (T* it = begin(); it != end(); it++) {
                ((*it)->*pFunction)();
            }
        }

        inline int indexOf(T* pItem) const {
            return pItem - begin();
        }

    private:
        /// @brief The array of elements.
        T mArr[N];
        T* mMoved = nullptr;
        s32 mMovedSize = 0;
    };

    /// @brief A contiguous growable array.
    template < class T >
    class Vector {
    public:
        typedef typename T::Item Item;

        /// @brief Creates a new `Vector`.
        Vector();

        /// @brief Destroys the `Vector`.
        ~Vector();

        /// @brief Returns the element at the given position.
        /// @param idx The position of the element.
        /// @return The reference to the element.
        Item& operator[](int idx) {
            return mArray[idx];
        }

        /// @brief Returns the element at the given position.
        /// @param idx The position of the element.
        /// @return The reference to the element.
        const Item& operator[](int idx) const {
            return mArray[idx];
        }

        /// @brief Allocates memory for the array.
        /// @param num The number of elements.
        void init(s32 num) {
            mArray.init(num);
        }

        /// @brief Returns the number of elements.
        /// @return The number of elements.
        int size() const {
            return mCount;
        }

        /// @brief Returns the number of elements that can be stored without reallocating.
        /// @return The number of elements that can be stored.
        int capacity() const {
            return mArray.size();
        }

        /// @brief Appends a copy of `rItem` to the end of the container.
        /// @param rItem The reference to the value to append.
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

        /// @brief Removes the value at the given position from the container.
        /// @param pIter The pointer to the position where the value should be removed.
        /// @return The pointer to the position of the removed value.
        Item* erase(Item* pIter) {
            if (end() - pIter - 1 > 0) {
                for (Item* p = pIter; p + 1 != end(); p++) {
                    *p = *(p + 1);
                }
            }

            mCount--;

            return pIter;
        }

        /// @brief Removes all values from the container.
        void clear() {
            mCount = 0;
        }

        /// @brief Returns an iterator to the first element.
        /// @return The pointer to the first element.
        Item* begin() {
            return &mArray[0];
        }

        /// @brief Returns an iterator to the first element.
        /// @return The pointer to the first element.
        const Item* begin() const {
            return &mArray[0];
        }

        /// @brief Returns an iterator to the reverse first element.
        /// @return The pointer to the reverse first element.
        Item* rbegin() {
            return end() - 1;
        }

        /// @brief Returns an iterator to the reverse first element.
        /// @return The pointer to the reverse first element.
        const Item* rbegin() const {
            return end() - 1;
        }

        /// @brief Returns an iterator past the last element.
        /// @return The pointer past the last element.
        Item* end() {
            return &mArray[mCount];
        }

        /// @brief Returns an iterator past the last element.
        /// @return The pointer past the last element.
        const Item* end() const {
            return &mArray[mCount];
        }

        /// @brief Returns an iterator past the reverse last element.
        /// @return The pointer past the reverse last element.
        Item* rend() {
            return begin() - 1;
        }

        /// @brief Returns an iterator past the reverse last element.
        /// @return The pointer past the reverse last element.
        const Item* rend() const {
            return begin() - 1;
        }

        /// @brief The array of elements.
        T mArray;

        /// @brief The number of valid elements.
        /* 0x? */ s32 mCount;
    };

    template < class T, int S >
    class FixedRingBuffer {
    public:
        class iterator {
        public:
            inline iterator() {
            }

            iterator(T* head, T* tail);

            void operator++();

            T* mHead;
            T* mTail;
            T* mEnd;
        };

        inline FixedRingBuffer() {
        }

        FixedRingBuffer(T* pBuffer, T* pBuffer2) : mHead(pBuffer, pBuffer2), mEnd(pBuffer, pBuffer2), mCount(0) {
        }

        void push_back(const T& val);

        T mBuffer[S];
        /* 0x40 for S=16 */ iterator mHead;
        /* 0x4C for S=16 */ iterator mEnd;
        /* 0x58 for S=16 */ s32 mCount;
    };

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
