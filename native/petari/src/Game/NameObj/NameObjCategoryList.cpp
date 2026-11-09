#include "Game/NameObj/NameObjCategoryList.hpp"
#include <algorithm>

NameObjCategoryList::NameObjCategoryList(u32 categoryNum, NameObjMethod pMethod) : mCategoryInfo(categoryNum), mMethod(pMethod), mMethodConst(nullptr) {
}

NameObjCategoryList::NameObjCategoryList(u32 categoryNum, NameObjMethodConst pMethod)
    : mCategoryInfo(categoryNum), mMethod(nullptr), mMethodConst(pMethod) {
}

void NameObjCategoryList::execute(int idx) {
    CategoryInfo* pCategoryInfo = &mCategoryInfo[idx];

    if (pCategoryInfo->mNameObjArr.size() == 0) {
        return;
    }

    if (pCategoryInfo->mExecuteBeforeFunction != nullptr) {
        (*pCategoryInfo->mExecuteBeforeFunction)();
    }

    // end() is read each pass: a call may add to or remove from this category.
    if (mMethodConst != nullptr) {
        for (NameObj** pNameObj = pCategoryInfo->mNameObjArr.begin(); pNameObj != pCategoryInfo->mNameObjArr.end(); pNameObj++) {
            MotionSignal::drawObject((*pNameObj)->mDrawSlot, (*pNameObj)->mMotionEpoch);
            ((*pNameObj)->*mMethodConst)();
        }
    } else {
        for (NameObj** pNameObj = pCategoryInfo->mNameObjArr.begin(); pNameObj != pCategoryInfo->mNameObjArr.end(); pNameObj++) {
            ((*pNameObj)->*mMethod)();
        }
    }
}

void NameObjCategoryList::incrementCheck(NameObj*, int index) {
    mCategoryInfo[index].mCheck++;
}

void NameObjCategoryList::allocateBuffer() {
    for (CategoryInfo& rInfo : mCategoryInfo) {
        if (rInfo.mCheck != 0) {
            rInfo.mNameObjArr.mArray.init(static_cast< s32 >(rInfo.mCheck));
        }
    }
}

void NameObjCategoryList::add(NameObj* pObj, int idx) {
    mCategoryInfo[idx].mNameObjArr.push_back(pObj);
}

void NameObjCategoryList::remove(NameObj* pObj, int idx) {
    MR::Vector< MR::AssignableArray< NameObj* > >& rArray = mCategoryInfo[idx].mNameObjArr;
    s32 size = rArray.size();
    NameObj** pBegin = rArray.begin();
    NameObj** pFound = std::find(pBegin, pBegin + size, pObj);
    rArray[pFound - pBegin] = rArray[rArray.size() - 1];
    rArray.pop_back();
}

void NameObjCategoryList::registerExecuteBeforeFunction(const MR::FunctorBase& rFunc, int idx) {
    NameObjCategoryList::CategoryInfo* pCategoryInfo = &mCategoryInfo[idx];

    pCategoryInfo->mExecuteBeforeFunction = rFunc.clone(nullptr);
}

NameObjCategoryList::CategoryInfo::CategoryInfo() : mNameObjArr(), mExecuteBeforeFunction(nullptr), mCheck(0) {
}

NameObjCategoryList::CategoryInfo::~CategoryInfo() {
}
