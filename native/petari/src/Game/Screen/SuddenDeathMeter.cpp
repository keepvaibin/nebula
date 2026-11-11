#include "Game/Screen/SuddenDeathMeter.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/CountUpPaneRumbler.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/LayoutUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 20.0f);

namespace {
    NEBULA_TUNABLE(f32, sMarioWorldOffset, 120.0f);
    NEBULA_TUNABLE(f32, sMarioScreenOffsetX, 32.0f);
    NEBULA_TUNABLE(f32, sMarioScreenOffsetY, -38.0f);
    NEBULA_TUNABLE(s32, sMoveToBasePositionTime, 25);
    NEBULA_TUNABLE(f32, sMinAlpha, 0.3f);
    NEBULA_TUNABLE(f32, sMaxAlpha, 1.2f);
    // static const f32 sHideSpeed = _;
    // static const f32 sShowSpeed = _;
    static const s32 sAppearTime = NebulaFrameClock::stepsOfSeconds(2);
    NEBULA_TUNABLE(s32, sBreakDelayTime, 40);
    static const s32 sPowerUpWaitTime = NebulaFrameClock::stepsOfSeconds(1);
    // static const s32 sRecoveryCountInterval = _;
    // static const s32 sRecoveryCountStartTime = _;
};  // namespace

namespace NrvSuddenDeathMeter {
    NEW_NERVE(SuddenDeathMeterNrvAppear, SuddenDeathMeter, Appear);
    NEW_NERVE(SuddenDeathMeterNrvWait, SuddenDeathMeter, Wait);
    NEW_NERVE(SuddenDeathMeterNrvPowerUp, SuddenDeathMeter, PowerUp);
    NEW_NERVE(SuddenDeathMeterNrvMeterMove, SuddenDeathMeter, MeterMove);
    NEW_NERVE(SuddenDeathMeterNrvCounterMove, SuddenDeathMeter, CounterMove);
    NEW_NERVE(SuddenDeathMeterNrvBreakMeter, SuddenDeathMeter, BreakMeter);
    NEW_NERVE(SuddenDeathMeterNrvZeroMeter, SuddenDeathMeter, ZeroMeter);
    NEW_NERVE(SuddenDeathMeterNrvZeroMeterBreak, SuddenDeathMeter, ZeroMeterBreak);
};  // namespace NrvSuddenDeathMeter

SuddenDeathMeter::SuddenDeathMeter(const char* pName, const char* pArcName)
    : LayoutActor(pName, true), mCountUpPaneRumbler(), mFollowPosW(0.0f, 0.0f), mMeterMoveStartPos(0.0f, 0.0f), mCount(1), mAlpha(0.0f), mShowHideRate(1.0f) {
    lumacore::setClass(this, lumacore::kClassId< SuddenDeathMeter >);
    MR::connectToSceneLayout(this);
    initLayoutManager(pArcName, 3);
}

void SuddenDeathMeter::init(const TableIter& /*rIter*/) {
    MR::createAndAddPaneCtrl(this, "HitPointMeter", 2);
    MR::createAndAddPaneCtrl(this, "HitPointNumber", 1);
    MR::createAndAddPaneCtrl(this, "Font", 2);
    MR::createAndAddPaneCtrl(this, "HitPointW", 1);
    MR::setFollowPos(&mFollowPosW, this, "HitPointW");
    MR::setFollowTypeAdd(this, "HitPointW");
    MR::startAnim(this, "FontPosition2to1", 1);
    MR::startAnim(this, "ShowHide", 2);
    MR::setAnimFrameAndStop(this, cNum_init_f1, 2);
    setCountAnimFrame();
    mCountUpPaneRumbler = new CountUpPaneRumbler(this, "HitPointNumber");
    initNerve(GET_NERVE(SuddenDeathMeter, SuddenDeathMeterNrvWait));
    initEffectKeeper(0, nullptr, nullptr);
    appear();
}

void SuddenDeathMeter::control() {
    if (isNerve(GET_NERVE(SuddenDeathMeter, SuddenDeathMeterNrvAppear))) {
        mAlpha = 1.0f;
    }

    mAlpha = MR::clamp(mAlpha, ::sMinAlpha, ::sMaxAlpha);

    MR::setAnimFrameAndStop(this, MR::normalize(mAlpha, 0.0f, 1.0f) * mShowHideRate * cNum_control_f1, 2);
    mCountUpPaneRumbler->update();
}

void SuddenDeathMeter::requestActive() {
    if (!MR::isDead(this)) {
        return;
    }

    appear();
    mCountUpPaneRumbler->reset();
    MR::showLayout(this);
    setNerve(GET_NERVE(SuddenDeathMeter, SuddenDeathMeterNrvWait));
}

void SuddenDeathMeter::requestDeactivate() {
    if (MR::isDead(this)) {
        return;
    }

    kill();
}

void SuddenDeathMeter::requestPowerUp() {
    setNerve(GET_NERVE(SuddenDeathMeter, SuddenDeathMeterNrvPowerUp));
}

void SuddenDeathMeter::requestForceAppear() {
    requestActive();
    setNerve(GET_NERVE(SuddenDeathMeter, SuddenDeathMeterNrvAppear));
}

void SuddenDeathMeter::requestPlayerMoving() {
}

void SuddenDeathMeter::exeAppear() {
    if (MR::isFirstStep(this)) {
        mFollowPosW.x = mFollowPosW.y = 0.0f;
        setCountAnimFrame();
    }

    if (MR::isGreaterStep(this, ::sAppearTime)) {
        setNerve(GET_NERVE(SuddenDeathMeter, SuddenDeathMeterNrvWait));
    }
}

void SuddenDeathMeter::exeWait() {
    if (MR::isFirstStep(this)) {
        setCountAnimFrame();
        mFollowPosW.x = mFollowPosW.y = 0.0f;
    }
}

void SuddenDeathMeter::exePowerUp() {
    if (MR::isFirstStep(this)) {
        MR::showLayout(this);
        MR::setAnimFrameAndStop(this, 0.0f, 1);
        setRecoveryCountAnimFrame();
    }

    Vec2f position;
    calcMarioHeadPosition(&position);
    setPowerUpMeterPosition(position);

    if (MR::isGreaterStep(this, ::sPowerUpWaitTime)) {
        setNerve(GET_NERVE(SuddenDeathMeter, SuddenDeathMeterNrvMeterMove));
    }
}

void SuddenDeathMeter::exeMeterMove() {
    if (MR::isFirstStep(this)) {
        mMeterMoveStartPos = mFollowPosW;
    }

    f32 rate = MR::calcNerveEaseInRate(this, ::sMoveToBasePositionTime);

    mFollowPosW = mMeterMoveStartPos * (1.0f - rate);

    if (MR::isGreaterStep(this, ::sMoveToBasePositionTime)) {
        setNerve(GET_NERVE(SuddenDeathMeter, SuddenDeathMeterNrvCounterMove));
    }
}

void SuddenDeathMeter::exeCounterMove() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "FontPosition1to2", 1);
        MR::setTextBoxNumberRecursive(this, "HitPointNumber", 2);
    }
    if (MR::isAnimStopped(this, 1)) {
        setNerve(GET_NERVE(SuddenDeathMeter, SuddenDeathMeterNrvWait));
    }
}

void SuddenDeathMeter::exeBreakMeter() {
    if (MR::isFirstStep(this)) {
        MR::startPaneAnim(this, "HitPointMeter", "Break", 0);
        MR::setTextBoxNumberRecursive(this, "HitPointNumber", mCount);
        MR::startAnim(this, "Damage2", 1);
        mFollowPosW.x = mFollowPosW.y = 0.0f;
    }

    if (MR::isStep(this, ::sBreakDelayTime)) {
        MR::startSystemSE("SE_SY_LIFE_PLATE_DEC");
        MR::startAnim(this, "FontPosition2to1", 1);
        setCountAnimFrame();
        setNerve(GET_NERVE(SuddenDeathMeter, SuddenDeathMeterNrvWait));
    }
}

void SuddenDeathMeter::exeZeroMeter() {
    if (MR::isFirstStep(this)) {
        mFollowPosW.x = mFollowPosW.y = 0.0f;
        setCountAnimFrame();
    }

    if (MR::isAnimStopped(this, 1)) {
        setNerve(GET_NERVE(SuddenDeathMeter, SuddenDeathMeterNrvZeroMeterBreak));
    }
}

void SuddenDeathMeter::exeZeroMeterBreak() {
    if (MR::isFirstStep(this)) {
        mFollowPosW.x = mFollowPosW.y = 0.0f;
    }

    if (MR::isStep(this, ::sBreakDelayTime)) {
        MR::hideLayout(this);
    }
}

void SuddenDeathMeter::initCount(s32 count) {
    mCount = count;
}

void SuddenDeathMeter::setCount(s32 count) {
    s32 prevCount = mCount;

    mCount = count;

    if (count >= prevCount) {
        return;
    }

    if (count == 0) {
        setNerve(GET_NERVE(SuddenDeathMeter, SuddenDeathMeterNrvZeroMeter));
    } else {
        setNerve(GET_NERVE(SuddenDeathMeter, SuddenDeathMeterNrvBreakMeter));
    }
}

void SuddenDeathMeter::calcMarioHeadPosition(Vec2f* pHeadPosition) const {
    Vec2f screenPos;
    MR::calcScreenPosition(&screenPos, *MR::getPlayerCenterPos());
    f32 radius = MR::calcPointRadius2D(*MR::getPlayerCenterPos(), ::sMarioWorldOffset);

    screenPos.y = screenPos.y - radius;
    screenPos.x += ::sMarioScreenOffsetX;
    screenPos.y += ::sMarioScreenOffsetY;

    pHeadPosition->x = screenPos.x;
    pHeadPosition->y = screenPos.y;
}

void SuddenDeathMeter::calcPowerUpMeterBasePosition(Vec2f* pBasePosition) const {
    pBasePosition->x = MR::getPaneTransX(this, "MoveMeterW");
    pBasePosition->y = MR::getPaneTransY(this, "MoveMeterW");
}

void SuddenDeathMeter::setPowerUpMeterPosition(const Vec2f& rPosition) {
    Vec2f basePosition;
    calcPowerUpMeterBasePosition(&basePosition);
    mFollowPosW.set(rPosition - basePosition);
}

void SuddenDeathMeter::setCountAnimFrame() {
    MR::setTextBoxNumberRecursive(this, "HitPointNumber", mCount);
    MR::startPaneAnim(this, "HitPointMeter", "Count", 0);
    MR::setPaneAnimFrameAndStop(this, "HitPointMeter", mCount, 0);
}

void SuddenDeathMeter::setRecoveryCountAnimFrame() {
    MR::setTextBoxNumberRecursive(this, "HitPointNumber", mCount);
    MR::startPaneAnim(this, "HitPointMeter", "RecoveryCount", 0);
    MR::setPaneAnimFrameAndStop(this, "HitPointMeter", mCount, 0);
}
