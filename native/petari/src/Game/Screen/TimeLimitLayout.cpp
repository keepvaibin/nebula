#include "Game/Screen/TimeLimitLayout.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/ValueControl.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    static const Timing sTimingTable[] = {
        {NebulaFrameClock::stepsOfSeconds(300), 100, true, true, true}, {NebulaFrameClock::stepsOfSeconds(180), 100, true, true, true},  {NebulaFrameClock::stepsOfSeconds(60), 100, true, true, true},
        {NebulaFrameClock::stepsOfSeconds(30), 100, true, true, false}, {NebulaFrameClock::stepsOfSeconds(10), NebulaFrameClock::stepsOfSeconds(10), false, false, false},
    };
    const s32 sScaleUpDownFrame = 8;
    const f32 sScaleMaxAdd = 0.5f;
    const s32 sFadeoutBeforeFrame = 90;
    const s32 sFadeinoutFrame = 60;
};  // namespace

void TimeUpLayout::init(const TableIter& /*rIter*/) {
    initLayoutManager("TimeUp", 1);
}

namespace NrvTimeLimitLayout {
    NEW_NERVE(TimeLimitLayoutAppear, TimeLimitLayout, Appear);
    NEW_NERVE(TimeLimitLayoutCountDown, TimeLimitLayout, CountDown);
    NEW_NERVE(TimeLimitLayoutScaleUp, TimeLimitLayout, ScaleUp);
    NEW_NERVE(TimeLimitLayoutScaleKeep, TimeLimitLayout, ScaleKeep);
    NEW_NERVE(TimeLimitLayoutScaleDown, TimeLimitLayout, ScaleDown);
    NEW_NERVE(TimeLimitLayoutFadeout, TimeLimitLayout, Fadeout);
    NEW_NERVE(TimeLimitLayoutTimeUpReady, TimeLimitLayout, TimeUpReady);
};  // namespace NrvTimeLimitLayout

TimeLimitLayout::TimeLimitLayout(u32 timeLimit)
    : LayoutActor("タイムリミット", true), mTime(), mTimeLimit(timeLimit), mScaleControl(), mFadeControl(), mCurrentTiming(), mIsSuspend(), mIsNormalDisplay() {
    lumacore::setClass(this, lumacore::kClassId< TimeLimitLayout >);
    mFlag.mHudPart = NEBULA_HUD_TIMER;
}

void TimeLimitLayout::init(const TableIter& /*rIter*/) {
    initLayoutManager("TimeLimit", 1);

    mScaleControl = new ValueControl(::sScaleUpDownFrame);
    mScaleControl->setZero();

    mFadeControl = new ValueControl(::sFadeinoutFrame);
    mFadeControl->setOne();

    MR::setInfluencedAlphaToChild(this);
    initNerve(GET_NERVE(TimeLimitLayout, TimeLimitLayoutCountDown));
    kill();
}

void TimeLimitLayout::appear() {
    resetFrame();
    LayoutActor::appear();
    NebulaMods::setCountdown(this, true);
    setNerve(GET_NERVE(TimeLimitLayout, TimeLimitLayoutAppear));
}

void TimeLimitLayout::kill() {
    LayoutActor::kill();
    NebulaMods::setCountdown(this, false);
}

void TimeLimitLayout::setTimeLimit(u32 timeLimit) {
    mTimeLimit = timeLimit;
}

void TimeLimitLayout::setDisplayModeOnNormal(bool param1) {
    mIsNormalDisplay = param1;
}

bool TimeLimitLayout::isReadyToTimeUp() const {
    return !MR::isDead(this) && isNerve(GET_NERVE(TimeLimitLayout, TimeLimitLayoutTimeUpReady));
}

void TimeLimitLayout::resetFrame() {
    mTime = 0;

    updateTextBox();

    mIsSuspend = false;

    mScaleControl->setOne();
    mFadeControl->setOne();

    mCurrentTiming = &::sTimingTable[0];
}

namespace {
    inline bool equalTime(const u32& rTime, const u32& rLimit) {
        return rTime == rLimit;
    }
};  // namespace

void TimeLimitLayout::addFrame() {
    if (equalTime(mTime, mTimeLimit)) {
        return;
    }

    mTime++;

    updateTextBox();
}

void TimeLimitLayout::updateTextBox() {
    MR::updateMinuteAndSecondTextBox(this, "Number", (mTimeLimit - mTime) + 59);
}

void TimeLimitLayout::exeAppear() {
    if (MR::isFirstStep(this)) {
        MR::startSystemSE("SE_SY_E3_TIMER_SCALE_UP");
        MR::startAnim(this, "Appear", 0);
    }

    MR::setNerveAtAnimStopped(this, GET_NERVE(TimeLimitLayout, TimeLimitLayoutCountDown), 0);
}

void TimeLimitLayout::exeCountDown() {
    if (updateNormal()) {
        return;
    }

    const Timing* pTiming = getCurrentTiming();

    if (pTiming != nullptr) {
        mCurrentTiming = pTiming;

        MR::startSystemSE("SE_SY_E3_TIMER_SCALE_UP");
        setNerve(GET_NERVE(TimeLimitLayout, TimeLimitLayoutScaleUp));
    }
}

void TimeLimitLayout::exeScaleUp() {
    if (MR::isFirstStep(this)) {
        mScaleControl->setDirToOneResetFrame();
        mFadeControl->setOne();
    }

    if (updateNormal()) {
        return;
    }

    if (mScaleControl->mFrame == mScaleControl->mMaxFrame) {
        setNerve(GET_NERVE(TimeLimitLayout, TimeLimitLayoutScaleKeep));
    }
}

void TimeLimitLayout::exeScaleKeep() {
    if (MR::isFirstStep(this)) {
        if (mCurrentTiming->mIsScaleUp) {
            MR::startAnim(this, "TimeSignal", 0);
        }
    }

    if (updateNormal()) {
        return;
    }

    if (MR::isGreaterStep(this, mCurrentTiming->mScaleKeepFrame)) {
        if (mCurrentTiming->mIsScaleDown) {
            setNerve(GET_NERVE(TimeLimitLayout, TimeLimitLayoutScaleDown));
        } else {
            setNerve(GET_NERVE(TimeLimitLayout, TimeLimitLayoutCountDown));
        }
    }
}

void TimeLimitLayout::exeScaleDown() {
    if (MR::isFirstStep(this)) {
        mScaleControl->setDirToZeroResetFrame();
    }

    if (updateNormal()) {
        return;
    }

    if (mScaleControl->mFrame == mScaleControl->mMaxFrame) {
        if (mCurrentTiming->mIsFadeOut &&!mIsNormalDisplay) {
            setNerve(GET_NERVE(TimeLimitLayout, TimeLimitLayoutFadeout));
        } else {
            setNerve(GET_NERVE(TimeLimitLayout, TimeLimitLayoutCountDown));
        }
    }
}

void TimeLimitLayout::exeFadeout() {
    if (MR::isStep(this, ::sFadeoutBeforeFrame)) {
        mFadeControl->setDirToZeroResetFrame();
    }

    if (updateNormal()) {
        return;
    }

    if (mFadeControl->mFrame == 0) {
        setNerve(GET_NERVE(TimeLimitLayout, TimeLimitLayoutCountDown));
    }
}

void TimeLimitLayout::exeTimeUpReady() {
}

void TimeLimitLayout::control() {
    mFadeControl->update();
    mScaleControl->update();

    f32 paneScale = 1.0f + MR::getEaseInOutValue(mScaleControl->getValue(), 0.0f, 1.0f, 1.0f) * ::sScaleMaxAdd;

    MR::setPaneScale(this, paneScale, paneScale, "TimeLimit");
    MR::setLayoutAlphaFloat(this, MR::getEaseInOutValue(mFadeControl->getValue(), 0.0f, 1.0f, 1.0f));
}

const Timing* TimeLimitLayout::getCurrentTiming() const {
    for (s32 i = 0; i < ARRAY_SIZE(::sTimingTable); i++) {
        u32 scaleStartFrame = ::sTimingTable[i].mScaleStartFrame;
        u32 timeLimit = mTimeLimit;
        u32 frame = scaleStartFrame - ::sTimingTable[i].mScaleKeepFrame;

        if (timeLimit >= frame && scaleStartFrame >= timeLimit) {
            continue;
        }

        u32 timeLeft = timeLimit - mTime;

        if (frame > timeLeft || timeLeft >= scaleStartFrame) {
            continue;
        }

        return &::sTimingTable[i];
    }

    return nullptr;
}

bool TimeLimitLayout::updateNormal() {
    if (mIsSuspend || NebulaMods::sCountdownHeld) {
        return false;
    }

    addFrame();

    u32 timeLeft = mTimeLimit - mTime;

    if (timeLeft <= NebulaFrameClock::stepsOfSeconds(2)) {
        if (timeLeft % NebulaFrameClock::stepsOfSeconds(1) == 0 && timeLeft != 0) {
            MR::startSystemSE("SE_SY_E3_TIMER_COUNT_4");
        }
    } else if (timeLeft <= NebulaFrameClock::stepsOfSeconds(6)) {
        if (timeLeft % NebulaFrameClock::stepsOfSeconds(1) == 0) {
            MR::startSystemSE("SE_SY_E3_TIMER_COUNT_2");
        }
    } else if (timeLeft <= NebulaFrameClock::stepsOfSeconds(10)) {
        if (timeLeft % NebulaFrameClock::stepsOfSeconds(1) == 0) {
            MR::startSystemSE("SE_SY_E3_TIMER_COUNT_1");
        }
    }

    if (mTimeLimit - mTime == NebulaFrameClock::stepsOfSeconds(10)) {
        MR::startAnim(this, "TimeLimit", 0);
    }

    if (mTime == mTimeLimit) {
        setNerve(GET_NERVE(TimeLimitLayout, TimeLimitLayoutTimeUpReady));

        return true;
    }

    return false;
}
