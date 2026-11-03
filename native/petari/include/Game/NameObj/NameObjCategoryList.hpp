#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/NameObj/NameObjHolder.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/MotionSignal.hpp"

namespace MR {
    class FunctorBase;
};  // namespace MR

namespace {
    template < typename T >
    class NameObjRealDelegator final {
    public:
        NameObjRealDelegator(T pMethod) {
            mNameObjFunc = pMethod;
        }

        void operator()(NameObj* pNameObj);

        T mNameObjFunc;
    };

    template < typename T >
    void NameObjRealDelegator< T >::operator()(NameObj* pNameObj) {
        (pNameObj->*mNameObjFunc)();
    }

    // The draw list is the only one of const methods: each NameObj's draws are named after it.
    template <>
    inline void NameObjRealDelegator< NameObjMethodConst >::operator()(NameObj* pNameObj) {
        MotionSignal::drawNameObj(pNameObj);
        (pNameObj->*mNameObjFunc)();
    }
};  // namespace

struct CategoryListInitialTable {
    u32 mIndex;
    u32 mCount;
};

/// @brief Organizes NameObjs by execution category.
class NameObjCategoryList {
public:
    class CategoryInfo {
    public:
        CategoryInfo();

        ~CategoryInfo();

        MR::Vector< MR::AssignableArray< NameObj* > > mNameObjArr;
        MR::FunctorBase* mExecuteBeforeFunction;
        u32 mCheck;
    };

    NameObjCategoryList(u32, const CategoryListInitialTable*, NameObjMethod, bool, const char*);
    NameObjCategoryList(u32, const CategoryListInitialTable*, NameObjMethodConst, bool, const char*);

    ~NameObjCategoryList();

    void execute(int);
    void incrementCheck(NameObj*, int);
    void allocateBuffer();
    void add(NameObj*, int);
    void remove(NameObj*, int);
    void registerExecuteBeforeFunction(const MR::FunctorBase&, int);
    void initTable(u32, const CategoryListInitialTable*);

    MR::AssignableArray< NameObjCategoryList::CategoryInfo > mCategoryInfo;

    union {
        NameObjRealDelegator< NameObjMethod >* mDelegator;
        NameObjRealDelegator< NameObjMethodConst >* mDelegatorConst;
    };

    bool mBufferAllocated;
    bool mAllocateByCheck;
};
