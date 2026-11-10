#include "Game/AreaObj/ImageEffectArea.hpp"
#include "Game/System/LumaCore.hpp"

ImageEffectArea::ImageEffectArea(EImageEffectType effectType, int formType, const char* pName) : AreaObj(formType, pName), mEffectType(effectType) {
    lumacore::setClass(this, lumacore::kClassId< ImageEffectArea >);
}

ImageEffectAreaMgr::ImageEffectAreaMgr(s32 maxNum, const char* pName) : AreaObjMgr(maxNum, pName) {
    lumacore::setClass(this, lumacore::kClassId< ImageEffectAreaMgr >);
}

void ImageEffectAreaMgr::initAfterPlacement() {
    sort();
}

void ImageEffectAreaMgr::sort() {
    if (mArray.size() == 0) {
        return;
    }

    for (s32 i = 0; i < mArray.size() - 1; i++) {
        AreaObj* pOriginal = getAreaObj(i);
        s32 selected = i;
        AreaObj* pSelected = pOriginal;
        for (s32 j = i + 1; j < mArray.size(); j++) {
            AreaObj* pCandidate = getAreaObj(j);
            if (pSelected->mObjArg7 > pCandidate->mObjArg7) {
                selected = j;
                pSelected = pCandidate;
            }
        }

        if (selected != i) {
            mArray[i] = pSelected;
            mArray[selected] = pOriginal;
        }
    }
}
