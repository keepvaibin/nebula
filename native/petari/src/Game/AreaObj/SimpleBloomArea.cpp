#include "Game/AreaObj/SimpleBloomArea.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/System/LumaCore.hpp"

SimpleBloomArea::SimpleBloomArea(int formType, const char* pName)
    : ImageEffectArea(IMAGE_EFFECT_TYPE_SIMPLE_BLOOM, formType, pName), mMaskFilterColor(), mThreshold(128), mIntensity(76) {
    lumacore::setClass(this, lumacore::kClassId< SimpleBloomArea >);
}

void SimpleBloomArea::init(const TableIter& rIter) {
    AreaObj::init(rIter);
    MR::createSimpleBloom();

    if (mObjArg0 >= 0) {
        mMaskFilterColor = static_cast< u8 >(mObjArg0);
    }

    if (mObjArg1 >= 0) {
        mThreshold = mObjArg1;
    }

    if (mObjArg2 >= 0) {
        mIntensity = mObjArg2;
    }
}
