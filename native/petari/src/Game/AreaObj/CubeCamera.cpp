#include "Game/AreaObj/CubeCamera.hpp"
#include "Game/AreaObj/AreaObj.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StringUtil.hpp"

#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

CubeCameraArea::CubeCameraArea(int formType, const char* pName) : AreaObj(formType, pName), mCategoryMask(), mZoneID() {
    lumacore::setClass(this, lumacore::kClassId< CubeCameraArea >);
}

s32 CubeCameraArea::sCubeCategory = 0;

void CubeCameraArea::init(const TableIter& rIter) {
    AreaObj::init(rIter);

    const char* valid;

    if (rIter.getValue("Validity", &valid)) {
        if (MR::isEqualString(valid, "Invalid")) {
            mIsValid = false;
        }
    }

    if (isValidSwitchA()) {
        mIsValid = false;
    }

    if (rIter.isValid()) {
        mZoneID = MR::getPlacedZoneId(rIter);
    } else {
        mZoneID = 0;
    }

    mCategoryMask = getCategoryArg();
    if (mCategoryMask < 0) {
        mCategoryMask = 0;
    }

    // Bits 4 and up are a category mask; when they are zero, the low nibble names one category.
    u32 singleCategoryBit = 1 << (mCategoryMask & 0xF);
    mCategoryMask = mCategoryMask >> 4;
    if (mCategoryMask == 0) {
        mCategoryMask |= singleCategoryBit;
    }

    MR::connectToSceneAreaObj(this);
}

void CubeCameraArea::movement() {
    if (!isValid()) {
        if (isValidSwitchA() && isOnSwitchA()) {
            mIsValid = true;
        }
    } else if (isValidSwitchB() && isOnSwitchB()) {
        mIsValid = false;
    }
}

bool CubeCameraArea::isValidCameraID() const {
    return mObjArg0 >= 0;
}

u16 CubeCameraArea::getCameraID() const {
    return mObjArg0;
}

bool CubeCameraArea::isInVolume(const Vec3f& rVec) const {
    return (mCategoryMask & sCubeCategory) != 0 && AreaObj::isInVolume(rVec);
}

void CubeCameraArea::setCurrentCategory(s32 category) {
    sCubeCategory = 1 << category;
}

s32 CubeCameraArea::getCategoryArg() const {
    return mObjArg3;
}

CubeCameraMgr::CubeCameraMgr(s32 firstCapacity, const char* pName) : AreaObjMgr(firstCapacity, pName) {
    lumacore::setClass(this, lumacore::kClassId< CubeCameraMgr >);
    CubeCameraArea::sCubeCategory = 1;
}

void CubeCameraMgr::initAfterLoad() {
    sort();
}

void CubeCameraMgr::sort() {
    if (mArray.size() == 0) {
        return;
    }

    for (s32 i = 0; i < mArray.size() - 1; i++) {
        int swapIndex = i;
        AreaObj* swapObj = getAreaObj(i);
        AreaObj* curObj = swapObj;
        for (s32 j = i + 1; j < mArray.size(); j++) {
            AreaObj* nextObj = getAreaObj(j);
            if (swapObj->mObjArg2 > nextObj->mObjArg2) {
                swapIndex = j;
                swapObj = nextObj;
            }
        }

        if (swapIndex != i) {
            mArray[i] = swapObj;
            mArray[swapIndex] = curObj;
        }
    }
}
