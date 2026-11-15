#include "Game/Screen/ImageEffectBase.hpp"
#include "Game/System/LumaCore.hpp"

ImageEffectBase::ImageEffectBase(const char* pName) : NameObj(pName), mIsOn(), mIsShown(), mFadeLevel() {
    lumacore::setClass(this, lumacore::kClassId< ImageEffectBase >);
}

void ImageEffectBase::calcAnim() {
    if (mIsOn) {
        mIsShown = true;
        mFadeLevel += 1.0f / 15.0f;

        if (mFadeLevel > 1.0f) {
            mFadeLevel = 1.0f;
        }
    } else if (isSomething()) {
        mFadeLevel -= 1.0f / 15.0f;

        if (mFadeLevel < 0.0f) {
            mIsShown = false;
            mFadeLevel = 0.0f;
        }
    }

    calcAnimSub();
}
