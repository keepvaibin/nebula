#include "Game/NameObj/NameObjCategoryList.hpp"

NameObjCategoryList::NameObjCategoryList(u32 categoryNum, NameObjMethod pMethod, s32 placeSlot)
    : mCategoryInfo(categoryNum), mMethod(pMethod), mMethodConst(nullptr), mPlaceSlot(placeSlot) {
}

NameObjCategoryList::NameObjCategoryList(u32 categoryNum, NameObjMethodConst pMethod, s32 placeSlot)
    : mCategoryInfo(categoryNum), mMethod(nullptr), mMethodConst(pMethod), mPlaceSlot(placeSlot) {
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
    MR::Vector< MR::AssignableArray< NameObj* > >& rArray = mCategoryInfo[idx].mNameObjArr;
    pObj->mListPlace[mPlaceSlot] = rArray.size();
    rArray.push_back(pObj);
}

// The last object takes the removed one's place.
void NameObjCategoryList::remove(NameObj* pObj, int idx) {
    MR::Vector< MR::AssignableArray< NameObj* > >& rArray = mCategoryInfo[idx].mNameObjArr;
    const s32 size = rArray.size();
    s32 place = pObj->mListPlace[mPlaceSlot];
    if (place < 0 || place >= size || rArray[place] != pObj) {
        for (place = 0; place < size && rArray[place] != pObj; place++) {
        }
        if (place == size) {
            return;
        }
    }

    NameObj* pLast = rArray[size - 1];
    rArray[place] = pLast;
    pLast->mListPlace[mPlaceSlot] = place;
    pObj->mListPlace[mPlaceSlot] = -1;
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
