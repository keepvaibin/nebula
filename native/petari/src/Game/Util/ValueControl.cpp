#include "Game/Util/ValueControl.hpp"

ValueControl::ValueControl(int maxFrame) : mFrame(), mMaxFrame(maxFrame), mDirection(Direction_Forward) {
}

void ValueControl::setDirToOne() {
    mDirection = Direction_Forward;
}

void ValueControl::setDirToOneResetFrame() {
    setDirToOne();
    resetFrame();
}

void ValueControl::setDirToZero() {
    mDirection = Direction_Backward;
}

void ValueControl::setDirToZeroResetFrame() {
    setDirToZero();
    resetFrame();
}

void ValueControl::setMaxFrame(int maxFrame) {
    f32 value = getValue();

    mMaxFrame = maxFrame;
    mFrame = value * mMaxFrame;
}

void ValueControl::update() {
    if (mFrame != (mDirection > 0 ? mMaxFrame : 0)) {
        mFrame += mDirection;
    }
}

void ValueControl::setZero() {
    setDirToZero();
    mFrame = 0;
}

void ValueControl::setOne() {
    setDirToOne();
    mFrame = mMaxFrame;
}

f32 ValueControl::getValue() const {
    // A control of no length (0 frames) is at its end already, not 0 / 0.
    if (mMaxFrame <= 0) {
        return mDirection > 0 ? 1.0f : 0.0f;
    }

    return static_cast< f32 >(mFrame) / mMaxFrame;
}

void ValueControl::resetFrame() {
    if (mDirection > 0) {
        mFrame = 0;
    } else {
        mFrame = mMaxFrame;
    }
}
