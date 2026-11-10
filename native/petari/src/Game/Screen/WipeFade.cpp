#include "Game/Screen/WipeFade.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_draw_f1, "draw.f1", 255.0f);

namespace {
    NEBULA_TUNABLE(s32, sFadeFrame, 30);
};  // namespace

WipeFade::WipeFade(const char* pName, const Color8& rFillColor)
    : WipeLayoutBase(pName), mIsWipeIn(false), mFrame(::sFadeFrame), mStep(::sFadeFrame), mFillColor(rFillColor) {
}

void WipeFade::init(const TableIter& /*rIter*/) {
    kill();
}

void WipeFade::control() {
    mStep++;

    if (isOpen()) {
        kill();
    }
}

void WipeFade::draw() const {
    if (MR::isDead(this)) {
        return;
    }

    f32 rate = static_cast< f32 >(mStep) / mFrame;
    f32 alpha = MR::clamp(rate, 0.0f, 1.0f);

    if (mIsWipeIn) {
        alpha = 1.0f - alpha;
    }

    NebulaColor8 fillColor;
    fillColor.r = mFillColor.r;
    fillColor.g = mFillColor.g;
    fillColor.b = mFillColor.b;
    fillColor.a = (s32)(cNum_draw_f1 * alpha);
    MR::fillScreen(fillColor);
}

void WipeFade::wipe(s32 frame) {
    mIsWipeIn = !mIsWipeIn;

    if (frame < 0) {
        mFrame = ::sFadeFrame;
    } else if (frame == 0) {
        mFrame = 1;
    } else {
        mFrame = frame;
    }

    mStep = 0;

    if (MR::isDead(this)) {
        appear();
    }
}

void WipeFade::forceClose() {
    mIsWipeIn = false;
    mStep = mFrame;

    if (MR::isDead(this)) {
        appear();
    }
}

void WipeFade::forceOpen() {
    mIsWipeIn = true;
    mStep = mFrame;

    kill();
}

bool WipeFade::isOpen() const {
    return mIsWipeIn && mStep >= mFrame;
}

bool WipeFade::isClose() const {
    return !mIsWipeIn && mStep >= mFrame;
}

bool WipeFade::isWipeIn() const {
    return mIsWipeIn && mStep < mFrame;
}

bool WipeFade::isWipeOut() const {
    return !mIsWipeIn && mStep < mFrame;
}
