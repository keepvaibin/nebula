#pragma once

#include <cstring>

template < typename T >
class JASPtrListAdaptor {
public:
    // TEMP
    JASPtrListAdaptor(T* pData) : mNext(), mData(pData) {};

    ~JASPtrListAdaptor() {};

    JASPtrListAdaptor< T >* getNext() const {
        return mNext;
    }

    T* get() const {
        return mData;
    }

    JASPtrListAdaptor< T >* mNext;
    T* mData;
};

template < typename T >
class JASPtrList {
public:
    JASPtrList() : mList() {};

    JASPtrList(JASPtrListAdaptor< T >* pAdapter) {
        mList = pAdapter;
    }

    bool isEmpty() const {
        return mList == nullptr;
    }

    ~JASPtrList() {};

    JASPtrListAdaptor< T >* begin() const {
        return mList;
    }

    JASPtrListAdaptor< T >* mList;
};

template < typename T >
class JASPtrTable {
public:
    JASPtrTable(T** ptrTable, u32 len) : mPtrTable(ptrTable), mLen(len) {
        memset(mPtrTable, 0, len * sizeof(T*));
    }

    T* get(u32 idx) const {
        if (idx >= mLen) {
            return nullptr;
        }

        return mPtrTable[idx];
    }

    void set(u32 idx, T* value) {
        mPtrTable[idx] = value;
    }

    T** mPtrTable;
    u32 mLen;
};

template < typename T, u32 LEN >
class JASPtrArray : public JASPtrTable< T > {
public:
    JASPtrArray() : JASPtrTable< T >(mPtrArray, LEN) {
    }

    T* mPtrArray[LEN];
};
