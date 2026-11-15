#include "Game/AreaObj/DepthOfFieldArea.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/System/LumaCore.hpp"

DepthOfFieldArea::DepthOfFieldArea(int formType, const char* pName) : ImageEffectArea(IMAGE_EFFECT_TYPE_DOF, formType, pName), mIsPlayerSync() {
    lumacore::setClass(this, lumacore::kClassId< DepthOfFieldArea >);
}

void DepthOfFieldArea::init(const TableIter& rIter) {
    AreaObj::init(rIter);

    mIsPlayerSync = mObjArg0 > 0;

    MR::createDepthOfFieldBlur();
}

f32 DepthOfFieldArea::getIntensity() const {
    if (mObjArg1 > -1) {
        return mObjArg1 / 255.0f;
    }

    return 1.0f;
}

s32 DepthOfFieldArea::getBlurMaxDist() const {
    return mObjArg2;
}

s32 DepthOfFieldArea::getBlurMinDist() const {
    return mObjArg3;
}
