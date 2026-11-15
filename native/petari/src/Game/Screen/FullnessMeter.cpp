#include "Game/Screen/FullnessMeter.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/SimpleLayout.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvFullnessMeter {
    NEW_NERVE(FullnessMeterNrvAppear, FullnessMeter, Appear);
    NEW_NERVE(FullnessMeterNrvWait, FullnessMeter, Wait);
    NEW_NERVE(FullnessMeterNrvEnd, FullnessMeter, End);
};  // namespace NrvFullnessMeter

FullnessMeter::FullnessMeter(LiveActor* pHost, s32 a2, s32 a3) : LayoutActor("満腹計", true), mNumber(a3), mNumberMax(a2), mHost(pHost) {
    lumacore::setClass(this, lumacore::kClassId< FullnessMeter >);
    MR::connectToSceneTalkLayout(this);
    initLayoutManager("StarPieceTargetMeter", 1);
    MR::createAndAddPaneCtrl(this, "TargetMeter", 1);
    MR::createAndAddPaneCtrl(this, "PicFrameShine", 1);
    mTargetCounter = MR::createSimpleLayoutTalkParts("満腹カウンター", "StarPieceTargetCounter", 1);
    MR::setTextBoxNumberRecursive(mTargetCounter, "NumberMax", mNumberMax);
    setNumber(a3);
    initNerve(GET_NERVE(FullnessMeter, FullnessMeterNrvAppear));
}

void FullnessMeter::init(const TableIter& /*rIter*/) {
    MR::startPaneAnim(this, "TargetMeter", "Count", 0);
    MR::setPaneAnimFrameAndStop(this, "TargetMeter", 0.0f, 0);
}

void FullnessMeter::control() {
    setTrans(MR::getStarPointerScreenPositionOrEdge(0));
}

void FullnessMeter::setNumber(s32 num) {
    mNumber = num;

    // A target of 0 or less (placement data) is already full, not a division by 0.
    const f32 fullness = mNumberMax > 0 ? static_cast< f32 >(mNumber) / mNumberMax : 1.0f;
    s32 frame = 128 - (u16)(s32)((128.0f * (1.0f - fullness)));

    MR::startPaneAnim(this, "TargetMeter", "Count", 0);
    MR::setPaneAnimFrameAndStop(this, "TargetMeter", frame, 0);
    MR::startPaneAnim(this, "PicFrameShine", "Shine", 0);

    if (MR::isAnimStopped(mTargetCounter, 0)) {
        MR::startAnim(mTargetCounter, "Flash", 0);
    }

    MR::setTextBoxNumberRecursive(mTargetCounter, "Number", mNumber);
}

void FullnessMeter::requestAppear() {
    if (!MR::isDead(this)) {
        return;
    }

    appear();
    mTargetCounter->appear();
    MR::showScreen(this);
    setNerve(GET_NERVE(FullnessMeter, FullnessMeterNrvAppear));
    MR::startStarPointerModeStarPieceTarget(this);
    MR::startSystemSE("SE_SY_TICOFAT_POINT");
}

void FullnessMeter::requestDisappear() {
    if (MR::isDead(this) || isNerve(GET_NERVE(FullnessMeter, FullnessMeterNrvEnd))) {
        return;
    }

    setNerve(GET_NERVE(FullnessMeter, FullnessMeterNrvEnd));
    MR::hideScreen(this);
    MR::startAnim(mTargetCounter, "End", 0);
    MR::endStarPointerMode(this);
}

void FullnessMeter::pauseOff() {
    MR::requestMovementOn(this);
    MR::requestMovementOn(mTargetCounter);
}

void FullnessMeter::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(mTargetCounter, "Appear", 0);
    }

    if (MR::isAnimStopped(mTargetCounter, 0)) {
        setNerve(GET_NERVE(FullnessMeter, FullnessMeterNrvWait));
    }
}

void FullnessMeter::exeWait() {
}

void FullnessMeter::exeEnd() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(mTargetCounter, "End", 0);
    }

    if (MR::isAnimStopped(mTargetCounter, 0)) {
        kill();
        mTargetCounter->kill();
    }
}
