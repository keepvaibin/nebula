#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/NameObj/NameObjHolder.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/MotionSignal.hpp"

namespace MR {
    class FunctorBase;
};  // namespace MR

/// @brief Organizes NameObjs by execution category.
class NameObjCategoryList {
public:
    class CategoryInfo {
    public:
        CategoryInfo();

        ~CategoryInfo();

        MR::Vector< MR::AssignableArray< NameObj* > > mNameObjArr;
        MR::FunctorBase* mExecuteBeforeFunction;
        u32 mCheck;  // objects registered before allocateBuffer
    };

    // placeSlot: the NameObj::mListPlace entry this list keeps (an object is in at most one category of a list).
    NameObjCategoryList(u32 categoryNum, NameObjMethod, s32 placeSlot);
    NameObjCategoryList(u32 categoryNum, NameObjMethodConst, s32 placeSlot);

    void execute(int);
    void incrementCheck(NameObj*, int);
    // Sizes each category to the objects counted into it.
    void allocateBuffer();
    void add(NameObj*, int);
    void remove(NameObj*, int);
    void registerExecuteBeforeFunction(const MR::FunctorBase&, int);

    MR::AssignableArray< NameObjCategoryList::CategoryInfo > mCategoryInfo;

    // One of the two is set. The draw list is the only list of const methods: before each draw it names the object the
    // draws belong to (MotionSignal::drawObject).
    NameObjMethod mMethod;
    NameObjMethodConst mMethodConst;
    s32 mPlaceSlot;
};
