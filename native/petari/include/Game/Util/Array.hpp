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
        /* 0x0 */ T* mArr;

        /// @brief The number of elements.
        /* 0x4 */ s32 mMaxSize;
    };

    /// @brief A contiguous fixed-size array.
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

        /// @brief A fixed array cannot grow: it says so, where a write past its end would corrupt what follows it.
        void grow() {
            NEBULA_PANIC("a table of %d is full", N);
        }

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

        /// @brief Returns the number of elements.
        /// @return The number of elements.
        int size() const {
            return N;
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

        /// @brief Returns an iterator past the last element.
        /// @return The pointer past the last element.
        T* end() {
            return &mArr[N];
        }

        /// @brief Returns an iterator past the last element.
        /// @return The pointer past the last element.
        const T* end() const {
            return &mArr[N];
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
        /* 0x0 */ T mArr[N];
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
        /* 0x0 */ T mArray;

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

            /* 0x0 */ T* mHead;
            /* 0x4 */ T* mTail;
            /* 0x8 */ T* mEnd;
        };

        inline FixedRingBuffer() {
        }

        FixedRingBuffer(T* pBuffer, T* pBuffer2) : mHead(pBuffer, pBuffer2), mEnd(pBuffer, pBuffer2), mCount(0) {
        }

        void push_back(const T& val);

        /* 0x00 */ T mBuffer[S];
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
