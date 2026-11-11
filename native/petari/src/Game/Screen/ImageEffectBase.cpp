#include "Game/Screen/ImageEffectBase.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_calcAnim_f1, "calcAnim.f1", 15.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcAnim_f2, "calcAnim.f2", 15.0f);

ImageEffectBase::ImageEffectBase(const char* pName) : NameObj(pName), mIsOn(), mIsShown(), mFadeLevel() {
    lumacore::setClass(this, lumacore::kClassId< ImageEffectBase >);
}

void ImageEffectBase::calcAnim() {
    if (mIsOn) {
        mIsShown = true;
        mFadeLevel += 1.0f / cNum_calcAnim_f1;

        if (mFadeLevel > 1.0f) {
            mFadeLevel = 1.0f;
        }
    } else if (isSomething()) {
        mFadeLevel -= 1.0f / cNum_calcAnim_f2;

        if (mFadeLevel < 0.0f) {
            mIsShown = false;
            mFadeLevel = 0.0f;
        }
    }

    calcAnimSub();
}
