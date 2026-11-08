#include "Game/Screen/ImageEffectBase.hpp"

ImageEffectBase::ImageEffectBase(const char* pName) : NameObj(pName), _C(), _D(), mFadeLevel() {
}

void ImageEffectBase::calcAnim() {
    if (_C) {
        _D = true;
        mFadeLevel += 1.0f / 15.0f;

        if (mFadeLevel > 1.0f) {
            mFadeLevel = 1.0f;
        }
    } else if (isSomething()) {
        mFadeLevel -= 1.0f / 15.0f;

        if (mFadeLevel < 0.0f) {
            _D = false;
            mFadeLevel = 0.0f;
        }
    }

    calcAnimSub();
}
