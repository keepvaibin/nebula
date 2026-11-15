#include "Game/Screen/SubMeterLayout.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    static f32 sScreenOffsetX = 60.0f;
    static f32 sScreenOffsetY = -70.0f;
};  // namespace

SubMeterLayout::SubMeterLayout(const char* pName, const char* pLayoutName) : LayoutActor(pName, true) {
    lumacore::setClass(this, lumacore::kClassId< SubMeterLayout >);
    MR::connectToSceneLayout(this);
    initLayoutManager(pLayoutName, 1);
    MR::createAndAddPaneCtrl(this, "Count", 1);
}

void SubMeterLayout::init(const TableIter& /*rIter*/) {
    MR::startPaneAnim(this, "Count", "Count", 0);
    MR::setPaneAnimFrameAndStop(this, "Count", 0.0f, 0);
    appear();
    MR::hideLayout(this);
    mPhase = 0;
    mFullLifeFrame = 0;
    // Before the first ratio the meter is full.
    mLifeFrame = 0x80;
}

void SubMeterLayout::setLifeRatio(f32 ratio) {
    // A ratio outside 0 to 1, or not a number, stays on the meter's ends (the frame is an unsigned 16 bit number).
    if (!(ratio >= 0.0f)) {
        ratio = 0.0f;
    } else if (ratio > 1.0f) {
        ratio = 1.0f;
    }
    f32 mult = 128.0f * ratio;
    mLifeFrame = (s32)(mult);
    MR::setPaneAnimFrameAndStop(this, "Count", 0x80 - mLifeFrame, 0);
}

void SubMeterLayout::requestDeactivate() {
    MR::hideLayout(this);
    mPhase = 0;
}

void SubMeterLayout::requestAppear() {
    MR::showLayout(this);
    MR::startAnim(this, "Appear", 0);
    mPhase = 1;
    control();
}

void SubMeterLayout::requestFrameIn() {
    MR::showLayout(this);
    MR::startAnim(this, "Appear", 0);
    mPhase = 1;
    control();
}

void SubMeterLayout::requestFrameOut() {
    MR::startAnim(this, "End", 0);
    mPhase = 4;
}

void SubMeterLayout::control() {
    switch (mPhase) {
    case 0:
        break;
    case 1:
        if (MR::isAnimStopped(this, 0)) {
            MR::startAnim(this, "Wait", 0);
            mFullLifeFrame = 0;
            mPhase += 1;
        }
        break;
    case 2:
        if (mLifeFrame < 0x40) {
            MR::startAnim(this, "Danger", 0);
            mPhase = 3;
        }
        break;
    case 3:
        if (mLifeFrame > 0x40) {
            MR::startAnim(this, "Wait", 0);
            MR::setAnimRate(this, 1.0f, 0);
            mPhase = 2;
        } else {
            MR::setAnimRate(this, ((0x40 - mLifeFrame) * 1.0f / 32.0f) + 1.0f, 0);
        }
        break;
    case 4:
        if (MR::isAnimStopped(this, 0)) {
            MR::hideLayout(this);
            mPhase = 0;
        }
        break;
    }

    bool flag = true;

    if (MR::isPlayerInWaterMode()) {
        if (!MR::isPlayerOnWaterSurface()) {
            flag = false;
        }
    } else if (!MR::isOnGroundPlayer()) {
        flag = false;
    }

    if (flag) {
        if (((mPhase + 0xFE) & 0xFF) <= 1u && mLifeFrame == 0x80) {
            if (++mFullLifeFrame == NebulaFrameClock::stepsOfSeconds(1)) {
                MR::startAnim(this, "End", 0);
                mPhase = 5;
            }
        }
    } else if (mPhase == 5) {
        mPhase = 1;
        mFullLifeFrame = 0;
        MR::startAnim(this, "Appear", 0);
    }

    if (!MR::isHiddenLayout(this)) {
        Vec2f screenPos;
        MR::calcScreenPosition(&screenPos, *MR::getPlayerPos());
        screenPos.x += ::sScreenOffsetX;
        screenPos.y += ::sScreenOffsetY;
        setTrans(screenPos);
    }
}
