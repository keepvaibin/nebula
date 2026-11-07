#include "Game/AreaObj/ScreenBlurArea.hpp"
#include "Game/Util/ScreenUtil.hpp"

ScreenBlurArea::ScreenBlurArea(int formType, const char* pName) : ImageEffectArea(IMAGE_EFFECT_TYPE_BLUR, formType, pName), mIsPlayerSync() {
}

void ScreenBlurArea::init(const TableIter& rIter) {
    AreaObj::init(rIter);
    mIsPlayerSync = mObjArg0 > 0;
    MR::createScreenBlur();
}
