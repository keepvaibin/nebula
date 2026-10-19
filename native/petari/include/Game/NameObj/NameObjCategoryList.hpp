#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/NameObj/NameObjHolder.hpp"
#include "Game/Util/Array.hpp"
#ifdef TARGET_PC
#include "Game/Util/MotionSignal.hpp"
#endif

namespace MR {
    class FunctorBase;
};  // namespace MR

namespace {
    template < typename T >
    class NameObjRealDelegator {
    public:
        NameObjRealDelegator(T pMethod) {
            mNameObjFunc = pMethod;
        }

        virtual void operator()(NameObj* pNameObj);

        /* 0x04 */ T mNameObjFunc;
    };

    template < typename T >
    void NameObjRealDelegator< T >::operator()(NameObj* pNameObj) {
        (pNameObj->*mNameObjFunc)();
    }

#ifdef TARGET_PC
    // The draw list is the only one of const methods: each NameObj's draws are named after it.
    template <>
    inline void NameObjRealDelegator< NameObjMethodConst >::operator()(NameObj* pNameObj) {
        MotionSignal::drawNameObj(pNameObj);
        (pNameObj->*mNameObjFunc)();
    }
#endif
};  // namespace

struct CategoryListInitialTable {
    /* 0x00 */ u32 mIndex;
    /* 0x04 */ u32 mCount;
};

/// @brief Organizes NameObjs by execution category.
class NameObjCategoryList {
public:
    class CategoryInfo {
    public:
        CategoryInfo();

        ~CategoryInfo();

        /* 0x00 */ MR::Vector< MR::AssignableArray< NameObj* > > mNameObjArr;
        /* 0x0C */ MR::FunctorBase* mExecuteBeforeFunction;
        /* 0x10 */ u32 mCheck;
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

    /* 0x00 */ MR::AssignableArray< NameObjCategoryList::CategoryInfo > mCategoryInfo;

    /* 0x08 */ union {
        NameObjRealDelegator< NameObjMethod >* mDelegator;
        NameObjRealDelegator< NameObjMethodConst >* mDelegatorConst;
    };

    /* 0x0C */ bool mBufferAllocated;
    /* 0x0D */ bool mAllocateByCheck;
};
