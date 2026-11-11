#include "Game/Screen/MeterLayout.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Player/MarioAccess.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/GameRules.hpp"
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
NEBULA_TUNABLE_NAMED(f32, cNum_control_f1, "control.f1", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f2, "control.f2", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_control_f3, "control.f3", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_requestActive_f1, "requestActive.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_requestActive_f2, "requestActive.f2", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_requestPowerUp_f1, "requestPowerUp.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_requestPlayerMoving_f1, "requestPlayerMoving.f1", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_requestPlayerStopped_f1, "requestPlayerStopped.f1", 0.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f1, "exeWait.f1", 50.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_calcMarioHeadPosition_f1, "calcMarioHeadPosition.f1", 120.0f);

// The numbers this file's functions give the game's motion and animation calls, as tunables: "<file>.<function>.<call>.<k>".
NEBULA_TUNABLE_NAMED(f32, cNum_setAnimBase_setAnimRate_1, "setAnimBase.setAnimRate.1", 4.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setAnimBase_setAnimRate_2, "setAnimBase.setAnimRate.2", 2.0f);

namespace {
    inline void calcFollowPosW(const MeterLayout* pLayout, Vec2f* pFollowPosW, Vec2f* pBasePos) {
        Vec2f headPos;

        pLayout->calcMarioHeadPosition(&headPos);
        pLayout->calcPowerUpMeterBasePosition(pBasePos);
        pFollowPosW->set(headPos - *pBasePos);
    }

    // static const f32 sMarioWorldOffset = ...;
    NEBULA_TUNABLE(f32, sMarioScreenOffsetX, 32.0f);
    NEBULA_TUNABLE(f32, sMarioScreenOffsetY, -38.0f);
    NEBULA_TUNABLE(s32, sMoveToBasePositionTime, 25);
    // static const s32 sWarningSignLifeLV1 = ...;
    // static const s32 sWarningSignLifeLV2 = ...;
    // static const f32 sWarningSignSpeedLV1 = ...;
    // static const f32 sWarningSignSpeedLV2 = ...;
    // static const s32 sMaxHPHideTime = ...;
    NEBULA_TUNABLE(f32, sMinAlpha, 0.3f);
    NEBULA_TUNABLE(f32, sMaxAlpha, 1.2f);
    // static const f32 sHideSpeed = ...;
    // static const f32 sShowSpeed = ...;
    static const s32 sAppearTime = NebulaFrameClock::stepsOfSeconds(2);
    static const s32 sWaitStartTime = NebulaFrameClock::stepsOfSeconds(4);
    NEBULA_TUNABLE(s32, sBreakDelayTime, 40);
    static const s32 sPowerUpWaitTime = NebulaFrameClock::stepsOfSeconds(1);
    NEBULA_TUNABLE(s32, sRecoveryCountInterval, 5);
};  // namespace

namespace {
    // The most the player can have now. The meter has six parts: a larger amount is shown in proportion, the number exact.
    s32 mostHealth() {
        if (!MR::isExistSceneObj(SceneObj_MarioHolder)) {
            return 6;
        }
        const MarioActor* pPlayer = MarioAccess::getPlayerActor();
        return pPlayer != nullptr ? static_cast< s32 >(pPlayer->mMaxHealth) : 6;
    }

    s32 meterPart(s32 count) {
        const s32 most = mostHealth();
        return most <= 6 || count <= 0 ? count : (count * 6 + most - 1) / most;
    }

    // The meter steps aside when the player has what they start with, unless that is the last.
    bool isStartingHealth(s32 count) {
        const s32 starting = GameRules::playerHealth();
        return count == starting && count != 1;
    }
};  // namespace

namespace NrvMeterLayout {
    NEW_NERVE(MeterLayoutNrvHide, MeterLayout, Hide);
    NEW_NERVE(MeterLayoutNrvAppear, MeterLayout, Appear);
    NEW_NERVE(MeterLayoutNrvWait, MeterLayout, Wait);
    NEW_NERVE(MeterLayoutNrvWaitStart, MeterLayout, WaitStart);
    NEW_NERVE(MeterLayoutNrvPowerUp, MeterLayout, PowerUp);
    NEW_NERVE(MeterLayoutNrvMeterMove, MeterLayout, MeterMove);
    NEW_NERVE(MeterLayoutNrvCounterMove, MeterLayout, CounterMove);
    NEW_NERVE(MeterLayoutNrvRecover, MeterLayout, Recover);
    NEW_NERVE(MeterLayoutNrvDamage, MeterLayout, Damage);
    NEW_NERVE(MeterLayoutNrvBreakMeter, MeterLayout, BreakMeter);
    NEW_NERVE(MeterLayoutNrvZeroMeter, MeterLayout, ZeroMeter);
    NEW_NERVE(MeterLayoutNrvZeroMeterBreak, MeterLayout, ZeroMeterBreak);
};  // namespace NrvMeterLayout

MeterLayout::MeterLayout(const char* pName, const char* pArcName)
    : LayoutActor(pName, true), mCountUpPaneRumbler(), mFollowPos(0.0f, 0.0f), mFollowPosW(0.0f, 0.0f), mMeterMoveStartPos(0.0f, 0.0f), mCount(3), mAlpha(),
      mVisibleRate(1.0f) {
    lumacore::setClass(this, lumacore::kClassId< MeterLayout >);
    MR::connectToSceneLayout(this);
    initLayoutManager(pArcName, 3);
}

void MeterLayout::init(const TableIter& /*rIter*/) {
    MR::createAndAddPaneCtrl(this, "HitPointMeter", 2);
    MR::createAndAddPaneCtrl(this, "HitPointNumber", 1);
    MR::createAndAddPaneCtrl(this, "Font", 2);
    MR::createAndAddPaneCtrl(this, "HPMeter", 1);
    MR::setFollowPos(&mFollowPos, this, "HPMeter");
    MR::setFollowTypeAdd(this, "HPMeter");
    MR::createAndAddPaneCtrl(this, "HitPointW", 1);
    MR::setFollowPos(&mFollowPosW, this, "HitPointW");
    MR::setFollowTypeAdd(this, "HitPointW");
    MR::startAnim(this, "FontPosition2to1", 1);
    MR::startAnim(this, "ShowHide", 2);
    MR::setAnimFrameAndStop(this, mAlpha * mVisibleRate * cNum_init_f1, 2);
    setCountAnimFrame();
    mCountUpPaneRumbler = new CountUpPaneRumbler(this, "HitPointNumber");
    initNerve(GET_NERVE(MeterLayout, MeterLayoutNrvWait));
    initEffectKeeper(0, nullptr, nullptr);
    appear();
}

void MeterLayout::control() {
    if (isNerve(GET_NERVE(MeterLayout, MeterLayoutNrvAppear))) {
        mAlpha = 1.0f;
    } else if (MR::isStarPointerPointingPaneForMeterLayout(this, "PicBase", 0, false, nullptr)) {
        mAlpha -= cNum_control_f1;
    } else {
        mAlpha += cNum_control_f2;
    }

    mAlpha = MR::clamp(mAlpha, ::sMinAlpha, ::sMaxAlpha);

    MR::setAnimFrameAndStop(this, mVisibleRate * MR::normalize(mAlpha, 0.0f, 1.0f) * cNum_control_f3, 2);
    mCountUpPaneRumbler->update();
}

void MeterLayout::requestActive() {
    if (!MR::isDead(this)) {
        return;
    }

    mCountUpPaneRumbler->reset();

    if (::isStartingHealth(mCount)) {
        mVisibleRate = 0.0f;
        mFollowPos.x = MR::getEaseInOutValue(0.0f, 1.0f, 0.0f, 1.0f) * cNum_requestActive_f1;
    } else {
        mVisibleRate = 1.0f;
        mFollowPos.y = 0.0f;
        mFollowPos.x = 0.0f;
    }

    f32 tmp = mVisibleRate;
    MR::setAnimFrameAndStop(this, tmp * cNum_requestActive_f2, 2);
    mFollowPosW.y = 0.0f;
    mFollowPosW.x = 0.0f;
    appear();
    setNerve(GET_NERVE(MeterLayout, MeterLayoutNrvWait));
}

void MeterLayout::requestDeactivate() {
    kill();
}

void MeterLayout::requestPowerUp() {
    mVisibleRate = 1.0f;
    MR::setAnimFrameAndStop(this, mVisibleRate * cNum_requestPowerUp_f1, 2);
    mFollowPos.y = 0.0f;
    mFollowPos.x = 0.0f;
    mFollowPosW.y = 0.0f;
    mFollowPosW.x = 0.0f;
    appear();
    setNerve(GET_NERVE(MeterLayout, MeterLayoutNrvPowerUp));
}

void MeterLayout::requestForceAppear() {
    if (mCount != 1) {
        return;
    }

    requestActive();
    setNerve(GET_NERVE(MeterLayout, MeterLayoutNrvAppear));
}

void MeterLayout::requestPlayerMoving() {
    if (!isNerve(GET_NERVE(MeterLayout, MeterLayoutNrvWait))) {
        return;
    }

    if (::isStartingHealth(mCount)) {
        mVisibleRate -= cNum_requestPlayerMoving_f1;

        if (mVisibleRate < 0.0f) {
            mVisibleRate = 0.0f;
        }
    } else {
        mVisibleRate = 1.0f;
    }
}

void MeterLayout::requestPlayerStopped() {
    if (!isNerve(GET_NERVE(MeterLayout, MeterLayoutNrvWait))) {
        return;
    }

    if (::isStartingHealth(mCount)) {
        mVisibleRate += cNum_requestPlayerStopped_f1;

        if (mVisibleRate > 1.0f) {
            mVisibleRate = 1.0f;
        }
    } else {
        mVisibleRate = 1.0f;
    }
}

void MeterLayout::initCount(s32 count) {
    mCount = count;
}

void MeterLayout::setCount(s32 count) {
    s32 prevCount = mCount;

    mCount = count;

    if (count < prevCount) {
        if (count == 0) {
            setNerve(GET_NERVE(MeterLayout, MeterLayoutNrvZeroMeter));
        } else if (::meterPart(prevCount) >= 4 && ::meterPart(count) < 4) {
            setNerve(GET_NERVE(MeterLayout, MeterLayoutNrvBreakMeter));
        } else {
            setNerve(GET_NERVE(MeterLayout, MeterLayoutNrvDamage));
        }
    } else if (prevCount < count) {
        setNerve(GET_NERVE(MeterLayout, MeterLayoutNrvRecover));
    }
}

void MeterLayout::exeHide() {
}

void MeterLayout::exeAppear() {
    if (MR::isFirstStep(this)) {
        mFollowPosW.y = 0.0f;
        mFollowPosW.x = 0.0f;
        setAnimBase();
        mVisibleRate = 1.0f;
    }

    mFollowPos.y = 0.0f;
    mFollowPos.x = 0.0f;

    if (MR::isGreaterStep(this, ::sAppearTime)) {
        setNerve(GET_NERVE(MeterLayout, MeterLayoutNrvWait));
    }
}

void MeterLayout::exeWaitStart() {
    if (MR::isFirstStep(this)) {
        setAnimBase();
        mVisibleRate = 1.0f;
    }

    mFollowPos.y = 0.0f;
    mFollowPos.x = 0.0f;
    mFollowPosW.y = 0.0f;
    mFollowPosW.x = 0.0f;

    if (MR::isGreaterStep(this, ::sWaitStartTime)) {
        setNerve(GET_NERVE(MeterLayout, MeterLayoutNrvWait));
    }
}

void MeterLayout::exeWait() {
    if (MR::isFirstStep(this)) {
        setAnimBase();
        mFollowPos.y = 0.0f;
        mFollowPos.x = 0.0f;
    }

    mFollowPos.x = MR::getEaseInOutValue(mVisibleRate, 1.0f, 0.0f, 1.0f) * cNum_exeWait_f1;
    mFollowPosW.y = 0.0f;
    mFollowPosW.x = 0.0f;
}

void MeterLayout::exePowerUp() {
    Vec2f basePos;

    mFollowPos.y = 0.0f;
    mFollowPos.x = 0.0f;

    calcFollowPosW(this, &mFollowPosW, &basePos);

    if (MR::isFirstStep(this)) {
        MR::showLayout(this);
        MR::startAnim(this, "Danger1", 1);
        MR::setAnimFrameAndStop(this, 0.0f, 1);
        setRecoveryCountAnimFrame();
        mVisibleRate = 1.0f;
    }

    if (mCount < ::mostHealth() && MR::isGreaterStep(this, 0) && MR::isIntervalStep(this, ::sRecoveryCountInterval)) {
        mCount++;
        setRecoveryCountAnimFrame();

        if (mCount >= 4) {
            MR::setTextBoxNumberRecursive(this, "HitPointNumber", 3);
        } else {
            mCountUpPaneRumbler->start();
            MR::startPaneAnim(this, "Font", "Flash", 0);
        }
    }

    if (MR::isGreaterStep(this, ::sPowerUpWaitTime)) {
        setNerve(GET_NERVE(MeterLayout, MeterLayoutNrvMeterMove));
    }
}

void MeterLayout::exeMeterMove() {
    if (MR::isFirstStep(this)) {
        mMeterMoveStartPos = mFollowPosW;
        mVisibleRate = 1.0f;
    }

    f32 rate = MR::calcNerveEaseInRate(this, ::sMoveToBasePositionTime);

    mFollowPosW = mMeterMoveStartPos * (1.0f - rate);

    if (MR::isGreaterStep(this, ::sMoveToBasePositionTime)) {
        setNerve(GET_NERVE(MeterLayout, MeterLayoutNrvCounterMove));
    }
}

void MeterLayout::exeCounterMove() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "FontPosition1to2", 1);
        mVisibleRate = 1.0f;
    }

    for (s32 count = 4; count <= mCount; count++) {
        if (MR::isStep(this, ::sRecoveryCountInterval * (count - 4))) {
            MR::setTextBoxNumberRecursive(this, "HitPointNumber", count);
        }
    }

    if (MR::isAnimStopped(this, 1)) {
        setNerve(GET_NERVE(MeterLayout, MeterLayoutNrvWaitStart));
    }
}

void MeterLayout::exeRecover() {
    if (MR::isFirstStep(this)) {
        mCountUpPaneRumbler->start();
        MR::startPaneAnim(this, "Font", "Flash", 0);
        setCountAnimFrame();
        mFollowPos.y = 0.0f;
        mFollowPos.x = 0.0f;
        mFollowPosW.y = 0.0f;
        mFollowPosW.x = 0.0f;
        mVisibleRate = 1.0f;
    }

    if (MR::isPaneAnimStopped(this, "Font", 0)) {
        setNerve(GET_NERVE(MeterLayout, MeterLayoutNrvWaitStart));
    }
}

void MeterLayout::exeDamage() {
    if (MR::isFirstStep(this)) {
        if (::meterPart(mCount) < 3) {
            MR::startAnim(this, "Damage1", 1);
        } else {
            MR::startAnim(this, "Damage2", 1);
        }

        mFollowPos.y = 0.0f;
        mFollowPos.x = 0.0f;
        mFollowPosW.y = 0.0f;
        mFollowPosW.x = 0.0f;
        setCountAnimFrame();
        mVisibleRate = 1.0f;
    }

    if (MR::isAnimStopped(this, 1)) {
        setNerve(GET_NERVE(MeterLayout, MeterLayoutNrvWaitStart));
    }
}

void MeterLayout::exeBreakMeter() {
    if (MR::isFirstStep(this)) {
        MR::startPaneAnim(this, "HitPointMeter", "Break", 0);
        MR::setTextBoxNumberRecursive(this, "HitPointNumber", mCount);
        MR::startAnim(this, "Damage2", 1);
        mFollowPos.y = 0.0f;
        mFollowPos.x = 0.0f;
        mFollowPosW.y = 0.0f;
        mFollowPosW.x = 0.0f;
        mVisibleRate = 1.0f;
    }

    if (MR::isStep(this, ::sBreakDelayTime)) {
        MR::emitEffect(this, "Break");
        MR::startSystemSE("SE_SY_LIFE_PLATE_DEC");
        MR::startAnim(this, "FontPosition2to1", 1);
        setCountAnimFrame();
        setNerve(GET_NERVE(MeterLayout, MeterLayoutNrvWaitStart));
    }
}

void MeterLayout::exeZeroMeter() {
    if (MR::isFirstStep(this)) {
        MR::startAnim(this, "Damage1", 1);
        mFollowPos.y = 0.0f;
        mFollowPos.x = 0.0f;
        mFollowPosW.y = 0.0f;
        mFollowPosW.x = 0.0f;
        setCountAnimFrame();
        mVisibleRate = 1.0f;
    }

    if (MR::isAnimStopped(this, 1)) {
        setNerve(GET_NERVE(MeterLayout, MeterLayoutNrvZeroMeterBreak));
    }
}

void MeterLayout::exeZeroMeterBreak() {
    if (MR::isFirstStep(this)) {
        mFollowPos.y = 0.0f;
        mFollowPos.x = 0.0f;
        mFollowPosW.y = 0.0f;
        mFollowPosW.x = 0.0f;
    }

    if (MR::isStep(this, ::sBreakDelayTime)) {
        MR::emitEffect(this, "Break123");
        MR::hideLayout(this);
    }
}

void MeterLayout::calcMarioHeadPosition(Vec2f* pHeadPosition) const {
    Vec2f screenPos;
    MR::calcScreenPosition(&screenPos, *MR::getPlayerCenterPos());
    f32 radius = MR::calcPointRadius2D(*MR::getPlayerCenterPos(), cNum_calcMarioHeadPosition_f1);

    screenPos.y = screenPos.y - radius;
    screenPos.x += ::sMarioScreenOffsetX;
    screenPos.y += ::sMarioScreenOffsetY;

    pHeadPosition->x = screenPos.x;
    pHeadPosition->y = screenPos.y;
}

void MeterLayout::calcPowerUpMeterBasePosition(Vec2f* pBasePosition) const {
    pBasePosition->x = MR::getPaneTransX(this, "MoveMeterW");
    pBasePosition->y = MR::getPaneTransY(this, "MoveMeterW");
}

void MeterLayout::setAnimBase() {
    setCountAnimFrame();

    if (::meterPart(mCount) <= 3) {
        MR::startAnim(this, "Danger1", 1);
    } else {
        MR::startAnim(this, "Danger2", 1);
    }

    if (mCount <= 1) {
        MR::setAnimRate(this, cNum_setAnimBase_setAnimRate_1, 1);
    } else if (mCount <= 2) {
        MR::setAnimRate(this, cNum_setAnimBase_setAnimRate_2, 1);
    } else {
        MR::setAnimFrameAndStop(this, 0.0f, 1);
    }
}

void MeterLayout::setCountAnimFrame() {
    MR::setTextBoxNumberRecursive(this, "HitPointNumber", mCount);
    MR::startPaneAnim(this, "HitPointMeter", "Count", 0);
    MR::setPaneAnimFrameAndStop(this, "HitPointMeter", ::meterPart(mCount), 0);
}

void MeterLayout::setRecoveryCountAnimFrame() {
    MR::setTextBoxNumberRecursive(this, "HitPointNumber", mCount);
    MR::startPaneAnim(this, "HitPointMeter", "RecoveryCount", 0);
    MR::setPaneAnimFrameAndStop(this, "HitPointMeter", ::meterPart(mCount), 0);
}
