#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/NameObj/NameObjHolder.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/MotionSignal.hpp"

namespace MR {
    class FunctorBase;
};  // namespace MR

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

    void execute(int);
    void incrementCheck(NameObj*, int);
    void allocateBuffer();
    void add(NameObj*, int);
    void remove(NameObj*, int);
    void registerExecuteBeforeFunction(const MR::FunctorBase&, int);
    void initTable(u32, const CategoryListInitialTable*);

    MR::AssignableArray< NameObjCategoryList::CategoryInfo > mCategoryInfo;

    // One of the two is set. The draw list is the only list of const methods: before each draw it names the object the
    // draws belong to (MotionSignal::drawObject).
    NameObjMethod mMethod;
    NameObjMethodConst mMethodConst;

    bool mBufferAllocated;
    bool mAllocateByCheck;
};
