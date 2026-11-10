#include "Game/AreaObj/ScreenBlurArea.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/System/LumaCore.hpp"

ScreenBlurArea::ScreenBlurArea(int formType, const char* pName) : ImageEffectArea(IMAGE_EFFECT_TYPE_BLUR, formType, pName), mIsPlayerSync() {
    lumacore::setClass(this, lumacore::kClassId< ScreenBlurArea >);
}

void ScreenBlurArea::init(const TableIter& rIter) {
    AreaObj::init(rIter);
    mIsPlayerSync = mObjArg0 > 0;
    MR::createScreenBlur();
}
