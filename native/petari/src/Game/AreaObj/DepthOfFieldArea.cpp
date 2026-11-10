#include "Game/AreaObj/DepthOfFieldArea.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_getIntensity_f1, "getIntensity.f1", 255.0f);

DepthOfFieldArea::DepthOfFieldArea(int formType, const char* pName) : ImageEffectArea(IMAGE_EFFECT_TYPE_DOF, formType, pName), mIsPlayerSync() {
}

void DepthOfFieldArea::init(const TableIter& rIter) {
    AreaObj::init(rIter);

    mIsPlayerSync = mObjArg0 > 0;

    MR::createDepthOfFieldBlur();
}

f32 DepthOfFieldArea::getIntensity() const {
    if (mObjArg1 > -1) {
        return mObjArg1 / cNum_getIntensity_f1;
    }

    return 1.0f;
}

s32 DepthOfFieldArea::getBlurMaxDist() const {
    return mObjArg2;
}

s32 DepthOfFieldArea::getBlurMinDist() const {
    return mObjArg3;
}
