#include "Game/Camera/CameraShakePatternImpl.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_update_f1, "update.f1", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f2, "update.f2", 0.5f);
NEBULA_TUNABLE_NAMED(f64, cNum_update_f3, "update.f3", 0.01);

namespace {
    NEBULA_TUNABLE(s32, sSinglyMaxFrame, 25);
};  // namespace

CameraShakePatternSingly::CameraShakePatternSingly(f32 intensity) : CameraShakePattern(), mIntensity(intensity), mDirection(0.0f, 1.0f) {
}

bool CameraShakePatternSingly::isEnd() const {
    return mFrame >= ::sSinglyMaxFrame;
}

void CameraShakePatternSingly::getOffset(Vec2f* pOffset) const {
    pOffset->set(mOffset);
}

void CameraShakePatternSingly::start() {
    mOffset.zero();
}

void CameraShakePatternSingly::update() {
    s32 framesLeft = ::sSinglyMaxFrame - mFrame;

    if (framesLeft < 0) {
        framesLeft = 0;
    }

    f32 amount = mIntensity * MR::sin(MR::pi() * cNum_update_f1 * framesLeft / ::sSinglyMaxFrame);
    amount *= MR::sin(MR::pi() * cNum_update_f2 * framesLeft / ::sSinglyMaxFrame);

    mOffset.set(mDirection * amount);
}

void CameraShakePatternSingly::setDirection(const Vec2f& rDir) {
    mDirection.set(rDir);
    MR::normalize(&mDirection);
}

CameraShakePatternVerticalSin::CameraShakePatternVerticalSin(f32 intensity, f32 speed) : mIntensity(intensity), mSpeed(speed), mOffset(0.0f, 0.0f) {
}

bool CameraShakePatternVerticalSin::isEnd() const {
    return mFrame >= mSpeed;
}

void CameraShakePatternVerticalSin::getOffset(Vec2f* pOffset) const {
    pOffset->set(mOffset);
}

void CameraShakePatternVerticalSin::start() {
    mOffset.set(0.0f, 0.0f);
}

void CameraShakePatternVerticalSin::update() {
    if (mSpeed < cNum_update_f3) {
        mOffset.zero();
        return;
    }

    f32 amount = mIntensity * -MR::sin(TWO_PI * mFrame / mSpeed);

    mOffset.set(0.0f, amount);
}
