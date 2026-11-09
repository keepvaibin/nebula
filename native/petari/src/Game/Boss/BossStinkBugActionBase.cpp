#include "Game/Boss/BossStinkBugActionBase.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/BossStinkBug.hpp"
#include "Game/Boss/BossStinkBugFunction.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ModelUtil.hpp"
#include "Game/Util/NerveUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/RailUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/SpringValue.hpp"
#include "Game/Util/StringUtil.hpp"
#include <nebula_geometry.hpp>
#include <nebula_math.h>

BossStinkBugActionBase::BossStinkBugActionBase(const char* pName, BossStinkBug* pStinkBug)
    : ActorStateBase< BossStinkBug >(pName, pStinkBug), mSpringValue(), mRailDiff(0.0f, 0.0f, 0.0f), mTurnFront(0.0f, 0.0f, 1.0f), mFlySpeed(15.0f), mHeightOffset(), mLocalHeightOffset(),
      mThrowBombTimer(), mIsRailToEnd(true), mCameraDistance(), mPrevCameraDistance(), mApproachStep() {
    mSpringValue = new SpringValue(mHeightOffset, mHeightOffset, 0.02f, 0.95f, 0.0f);
}

bool BossStinkBugActionBase::updateGroundWalk(f32 speed) {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(getHost(), "GroundWalk");
    }

    BossStinkBugFunction::moveRail(getHost(), speed, 0.0f, 0.0f);
    mSpeed = speed;

    MR::calcDistanceToPlayer(getHost());
    return false;
}

bool BossStinkBugActionBase::updateFlyRoll() {
    if (MR::isFirstStep(this)) {
        MR::startBck(getHost(), "FlyRollRight");
    }

    startSoundFly(0);
    mSpeed = mFlySpeed;

    BossStinkBugFunction::moveRail(getHost(), mSpeed, mHeightOffset, mLocalHeightOffset);

    if (MR::isBckStopped(getHost())) {
        return true;
    } else {
        return false;
    }
}

bool BossStinkBugActionBase::updateFlyRollWait(s32 waitSteps) {
    if (MR::isFirstStep(this)) {
        MR::startBck(getHost(), "FlyRollWaitRight");
    }

    startSoundFly(0);
    mSpeed = mFlySpeed;

    BossStinkBugFunction::moveRail(getHost(), mSpeed, mHeightOffset, mLocalHeightOffset);

    if (MR::isGreaterStep(this, waitSteps)) {
        return true;
    } else {
        return false;
    }
}

bool BossStinkBugActionBase::updateFlyRollEnd() {
    if (MR::isFirstStep(this)) {
        MR::startBck(getHost(), "FlyRollEndRight");
    }

    startSoundFly(0);
    mSpeed = mFlySpeed;

    BossStinkBugFunction::moveRail(getHost(), mSpeed, mHeightOffset, mLocalHeightOffset);

    if (MR::isBckStopped(getHost())) {
        return true;
    } else {
        return false;
    }
}

bool BossStinkBugActionBase::updateFlyDash(f32 startSpeed) {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(getHost(), "FlyWait");
        BossStinkBugFunction::calcDiffCurrentRailPosition(&mRailDiff, getHost(), mHeightOffset, mLocalHeightOffset);
    }

    startSoundFly(0);

    if (MR::isLessStep(this, NebulaFrameClock::stepsOfMilliseconds(500))) {
        mSpeed = MR::calcNerveEaseInOutValue(this, NebulaFrameClock::stepsOfMilliseconds(500), startSpeed, 40.0f);
    } else {
        mSpeed = MR::calcNerveEaseInOutValue(this, NebulaFrameClock::stepsOfMilliseconds(500), 100, 40.0f, mFlySpeed);
    }

    BossStinkBugFunction::regainToRail(getHost(), mRailDiff, MR::calcNerveRate(this, 100), mSpeed, mHeightOffset, mLocalHeightOffset);

    if (MR::isGreaterStep(this, 100)) {
        return true;
    } else {
        return false;
    }
}

void BossStinkBugActionBase::throwBomb(s32 newWaitTime) {
    if (mThrowBombTimer == 0) {
        BossStinkBugFunction::throwBomb(getHost());
        mThrowBombTimer = newWaitTime;
    } else {
        mThrowBombTimer--;
    }
}

void BossStinkBugActionBase::resetThrowBombTiming() {
    mThrowBombTimer = 0;
}

f32 BossStinkBugActionBase::calcShakeOffSignSpeed(s32 numSteps) const {
    if (MR::isLessStep(this, 10)) {
        return MR::calcNerveEaseInOutValue(this, numSteps, mFlySpeed, 3.0f);
    } else {
        return 3.0f;
    }
}

f32 BossStinkBugActionBase::calcShakeOffSpeed() const {
    return MR::calcNerveEaseInOutValue(this, MR::getBckFrameMax(getHost(), "FlyRollRight"), 3.0f, mFlySpeed);
}

f32 BossStinkBugActionBase::calcShakeOffEndSpeed() const {
    return MR::calcNerveEaseInOutValue(this, MR::getBckFrameMax(getHost(), "FlyRollEndRight"), 35.0f, mFlySpeed);
}

// Half of what two thirds of the rail leaves after the roll animations and the given wait.
s32 BossStinkBugActionBase::calcKeepRollTime(s32 waitTime) const {
    f32 railTime = (MR::getRailTotalLength(getHost()) * (2.0f / 3.0f)) / mFlySpeed;

    f32 rollTime = 0.0f;
    rollTime += MR::getBckFrameMax(getHost(), "FlyRollRight");
    rollTime += MR::getBckFrameMax(getHost(), "FlyRollEndRight");
    rollTime += waitTime;

    s32 keepRollTime = (railTime - rollTime) / 2;

    if (keepRollTime < 0) {
        keepRollTime = 0;
    }

    return keepRollTime;
}

s32 BossStinkBugActionBase::calcFlySoundVol() const {
    s32 vol = 550;
    if (mApproachStep > 0) {
        if (mCameraDistance < 1500.0f) {
            vol = 1000;
            if (mApproachStep < 12) {
                vol = MR::getLinerValue(mApproachStep, 0.0f, 1.0f, 12.0f);
                vol = 450 * vol + 550;
            }
        } else if (mCameraDistance < 2500) {
            vol = MR::getLinerValueFromMinMax(mCameraDistance, 1500.0f, 2500.0f, 1000.0f, 550.0f);
            if (mApproachStep < 12) {
                vol = MR::getLinerValue(mApproachStep, 0.0f, 1.0f, 12.0f);
                vol = 450 * vol + 550;
            }
        } else {
            vol = 550;
        }
    }

    return vol;
}

s32 BossStinkBugActionBase::calcIIRCutOff() const {
    if (mCameraDistance < 2000.0f) {
        return 126;
    } else if (mCameraDistance < 4500.0f) {
        return MR::getLinerValueFromMinMax(mCameraDistance, 2000.0f, 4500.0f, 126.0f, 20.0f);
    }

    return 20;
}

void BossStinkBugActionBase::updateSound() {
    if (getHost()->isLegInWater()) {
        if (isAngry()) {
            MR::setSeVersion(getHost(), BossStinkBug::SeVersion_ANGRY_WATER);
        } else {
            MR::setSeVersion(getHost(), BossStinkBug::SeVersion_WATER);
        }
    } else {
        if (isAngry()) {
            MR::setSeVersion(getHost(), BossStinkBug::SeVersion_ANGRY);
        } else {
            MR::setSeVersion(getHost(), BossStinkBug::SeVersion_NORMAL);
        }
    }
}

void BossStinkBugActionBase::updateSoundFly() {
    if (mApproachStep > 0) {
        mApproachStep--;
    }

    mPrevCameraDistance = mCameraDistance;
    mCameraDistance = MR::getCamPos().distance(getHost()->mPosition);

    if (mPrevCameraDistance - mCameraDistance > 0.0f) {
        mApproachStep = 12;
    }
}

bool BossStinkBugActionBase::updateGroundRegainRail(f32 speed) {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(getHost(), "GroundWalk");
        BossStinkBugFunction::calcDiffCurrentRailPosition(&mRailDiff, getHost(), mHeightOffset, mLocalHeightOffset);
    }

    f32 nerveRate = MR::calcNerveRate(this, NebulaFrameClock::stepsOfMilliseconds(1500));
    BossStinkBugFunction::regainToRail(getHost(), mRailDiff, nerveRate, speed, 0.0f, 0.0f);
    mSpeed = speed;

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
        return true;
    } else {
        return false;
    }
}

bool BossStinkBugActionBase::updateGroundFind() {
    if (MR::isFirstStep(this)) {
        MR::startSound(getHost(), "SE_BV_BOSS_BUG_ATTACK_G");
        MR::startBck(getHost(), "Find");
        mSpeed = 0.0f;
    }

    if (MR::isBckStopped(getHost())) {
        return true;
    } else {
        return false;
    }
}

bool BossStinkBugActionBase::updateGroundShakeOff() {
    if (MR::isFirstStep(this)) {
        MR::startBck(getHost(), "GroundRide");
    }

    mSpeed *= 0.95f;
    BossStinkBugFunction::moveRail(getHost(), mSpeed, 0.0f, 0.0f);

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfMilliseconds(1500))) {
        return true;
    } else {
        return false;
    }
}

bool BossStinkBugActionBase::updateGroundRun(f32 maxSpeed) {
    if (MR::isFirstStep(this)) {
        MR::startBck(getHost(), "GroundRun");
        BossStinkBugFunction::validateAttack(getHost());
    }

    if (getHost()->isLegInWater()) {
        MR::startLevelSound(getHost(), "SE_BM_LV_BOSS_BUG_RUN_ADD_WATER");
    }

    mSpeed = maxSpeed * MR::calcNerveRate(this, NebulaFrameClock::stepsOfSeconds(1));
    BossStinkBugFunction::moveRail(getHost(), mSpeed, 0.0f, 0.0f);

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(3))) {
        return true;
    } else {
        return false;
    }
}

bool BossStinkBugActionBase::updateGroundAttackSuccess() {
    if (MR::isFirstStep(this)) {
        MR::startBck(getHost(), "AttackSuccess");
    }

    if (MR::isStep(this, 12)) {
        MR::startSound(getHost(), "SE_BV_BOSS_BUG_GLAD");
    }

    mSpeed *= 0.75f;
    BossStinkBugFunction::moveRail(getHost(), mSpeed, 0.0f, 0.0f);

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(3))) {
        return true;
    } else {
        return false;
    }
}

bool BossStinkBugActionBase::updateGroundCoolDown() {
    if (MR::isFirstStep(this)) {
        MR::startBck(getHost(), "CoolDown");
    }

    MR::startLevelSound(getHost(), "SE_BM_LV_BOSS_BUG_BRAKE");

    if (getHost()->isLegInWater()) {
        MR::startLevelSound(getHost(), "SE_BM_LV_BOSS_BUG_RUN_ADD_WATER");
    }

    mSpeed *= 0.95f;
    BossStinkBugFunction::moveRail(getHost(), mSpeed, 0.0f, 0.0f);

    if (MR::isBckStopped(getHost())) {
        return true;
    } else {
        return false;
    }
}

bool BossStinkBugActionBase::updateGroundTurn() {
    if (MR::isFirstStep(this)) {
        mTurnFront = getHost()->mFront;

        mIsRailToEnd = MR::isRailGoingToEnd(getHost());

        if (mIsRailToEnd) {
            MR::startBck(getHost(), "GroundTurnLeft");
        } else {
            MR::startBck(getHost(), "GroundTurnRight");
        }

        mSpeed = 0.0f;
    }

    BossStinkBugFunction::turnRailGround(getHost(), mTurnFront, MR::calcNerveRate(this, NebulaFrameClock::stepsOfSeconds(2)), mIsRailToEnd);

    if (MR::isGreaterStep(this, 160)) {
        return true;
    } else {
        return false;
    }
}

bool BossStinkBugActionBase::updateGroundDamage() {
    if (MR::isFirstStep(this)) {
        MR::zeroVelocity(getHost());
        MR::startSound(getHost(), "SE_BM_BOSS_BUG_PRESSED");
        MR::startBck(getHost(), "GroundDamage");

        MR::tryRumblePadStrong(getHost(), PLAYER_1);
        MR::shakeCameraStrong();

        getHost()->startEventCamera("Fall");
        getHost()->disposeBomb();

        mSpeed = 0.0f;
        MR::offPlayerControl();
    }

    if (MR::isStep(this, 9)) {
        MR::startSound(getHost(), "SE_BV_BOSS_BUG_DAMAGE");
    }

    if (MR::isBckStopped(getHost())) {
        MR::onPlayerControl(true);
        getHost()->endEventCamera("Fall");
        return true;
    } else {
        return false;
    }
}

bool BossStinkBugActionBase::updateFly() {
    if (MR::isFirstStep(this)) {
        MR::startBck(getHost(), "FlyWait");
    }

    startSoundFly(0);

    mSpeed = mFlySpeed;
    BossStinkBugFunction::moveRail(getHost(), mSpeed, mHeightOffset, mLocalHeightOffset);
    return false;
}

bool BossStinkBugActionBase::updateFlyShakeOffSign(s32 numSteps) {
    if (MR::isFirstStep(this)) {
        MR::startBck(getHost(), "FlyRide");
        mSpringValue->mSpringValue = mHeightOffset;
        mSpringValue->mRestValue = mHeightOffset;
        mSpringValue->mVelocity = -15.0f;
    }

    startSoundFly(0);

    mSpeed = calcShakeOffSignSpeed(numSteps);

    BossStinkBugFunction::moveRail(getHost(), mSpeed, mSpringValue->update(), mLocalHeightOffset);

    if (MR::isGreaterStep(this, numSteps)) {
        return true;
    } else {
        return false;
    }
}

bool BossStinkBugActionBase::updateFlyShakeOff() {
    if (MR::isFirstStep(this)) {
        MR::startBck(getHost(), "FlyRollRight");
    }

    startSoundFly(3);

    mSpeed = calcShakeOffSpeed();

    BossStinkBugFunction::moveRail(getHost(), mSpeed, mSpringValue->update(), mLocalHeightOffset);

    if (MR::isBckStopped(getHost())) {
        return true;
    } else {
        return false;
    }
}

bool BossStinkBugActionBase::updateFlyShakeOffWait(s32 numSteps) {
    if (MR::isFirstStep(this)) {
        MR::startBck(getHost(), "FlyRollWaitRight");
    }

    startSoundFly(0);

    mSpeed = 35.0f;
    BossStinkBugFunction::moveRail(getHost(), mSpeed, mHeightOffset, mLocalHeightOffset);

    if (MR::isGreaterStep(this, numSteps)) {
        return true;
    } else {
        return false;
    }
}

bool BossStinkBugActionBase::updateFlyShakeOffEnd() {
    if (MR::isFirstStep(this)) {
        MR::startBck(getHost(), "FlyRollEndRight");
    }

    startSoundFly(0);

    mSpeed = calcShakeOffEndSpeed();
    BossStinkBugFunction::moveRail(getHost(), mSpeed, mHeightOffset, mLocalHeightOffset);

    if (MR::isBckStopped(getHost())) {
        return true;
    } else {
        return false;
    }
}

bool BossStinkBugActionBase::updateFlyRollSign(s32 numSteps) {
    if (MR::isFirstStep(this)) {
        MR::startBck(getHost(), "FlyRollSignRight");
    }

    if (MR::isStep(this, 10)) {
        MR::startSound(getHost(), "SE_BV_BOSS_BUG_ROLL");
    }

    startSoundFly(0);

    mSpeed = mFlySpeed;
    BossStinkBugFunction::moveRail(getHost(), mSpeed, mHeightOffset, mLocalHeightOffset);

    if (MR::isGreaterStep(this, numSteps)) {
        return true;
    } else {
        return false;
    }
}

bool BossStinkBugActionBase::updateFlyTurnSign() {
    if (MR::isFirstStep(this)) {
        MR::startSound(getHost(), "SE_BV_BOSS_BUG_TURN");
        MR::startBck(getHost(), "FlyTurnSign");
    }

    startSoundFly(0);

    mSpeed = mFlySpeed;
    BossStinkBugFunction::moveRail(getHost(), mSpeed, mHeightOffset, mLocalHeightOffset);

    if (MR::isBckStopped(getHost())) {
        return true;
    } else {
        return false;
    }
}

bool BossStinkBugActionBase::updateFlyTurn() {
    if (MR::isFirstStep(this)) {
        MR::startBck(getHost(), "FlyTurn");
        mTurnFront = getHost()->mFront;
        mIsRailToEnd = MR::isRailGoingToEnd(getHost());
    }

    startSoundFly(1);

    mSpeed = 0.0f;
    BossStinkBugFunction::turnRail(getHost(), mTurnFront, 0.0f, MR::calcNerveRate(this, NebulaFrameClock::stepsOfSeconds(2)), mFlySpeed, mHeightOffset, mLocalHeightOffset, mIsRailToEnd);

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        return true;
    } else {
        return false;
    }
}

bool BossStinkBugActionBase::updateFall() {
    if (MR::isFirstStep(this)) {
        getHost()->startEventCamera("Fall");
        MR::startBck(getHost(), "Fall");
        MR::onBind(getHost());

        mRailDiff = getHost()->mPosition;
        mSpeed = 0.0f;
        getHost()->disposeBomb();
    }

    MR::startLevelSound(getHost(), "SE_BM_LV_BOSS_BUG_FLY_ADD", 600, 126);

    BossStinkBugFunction::setFallVelocity(getHost(), mRailDiff, MR::calcNerveRate(this, 80), 0.3f, 1100.0f);

    if (MR::isBinded(getHost())) {
        MR::offBind(getHost());
        MR::zeroVelocity(getHost());
        return true;
    } else {
        return false;
    }
}

bool BossStinkBugActionBase::updateFallDamage() {
    if (MR::isFirstStep(this)) {
        MR::startSound(getHost(), "SE_BM_BOSS_BUG_PRESSED");
        MR::startBck(getHost(), "FallDamage");
        MR::startAction(getHost()->getWingModel(), "FallDamage");
        MR::tryRumblePadStrong(getHost(), PLAYER_1);
        MR::shakeCameraStrong();
        MR::offBind(getHost());

        mSpeed = 0.0f;
        MR::offPlayerControl();
        getHost()->disposeBomb();
    }

    if (MR::isStep(this, 9)) {
        MR::startSound(getHost(), "SE_BV_BOSS_BUG_DAMAGE");
    }

    if (MR::isBckStopped(getHost())) {
        MR::onPlayerControl(true);
        getHost()->endEventCamera("Fall");
        return true;
    } else {
        return false;
    }
}

bool BossStinkBugActionBase::updateToGround() {
    if (MR::isFirstStep(this)) {
        MR::startSound(getHost(), "SE_BV_BOSS_BUG_ANGRY_LAND");
        MR::startAction(getHost()->getWingModel(), "Ground");
        MR::tryStartBck(getHost(), "TurnDown");
        getHost()->validateCollisionGround();
        getHost()->onBindLeg();
        getHost()->mLegIKRate = 0.0f;
        mTurnFront = getHost()->mFront;

        mLandingHeight = mHeightOffset + mLocalHeightOffset;
        mLandingHeightSpeed = 0.0f;
    }

    startSoundFly(0);
    mLandingHeight += mLandingHeightSpeed;

    if (MR::isLessEqualStep(this, 40)) {
        mLandingHeightSpeed += 1.2f;
    }

    mLandingHeightSpeed -= 1.0f;
    mLandingHeightSpeed *= 0.99f;

    if (mLandingHeight < 0.0f) {
        mLandingHeight = 0.0f;
    }

    f32 nerveRate = MR::calcNerveRate(this, NebulaFrameClock::stepsOfSeconds(1));
    getHost()->mLegIKRate = nerveRate;
    MR::rotateVecDegree(&getHost()->mFront, MR::getRailDirection(getHost()), getHost()->mGravity, MR::calcNerveValue(this, NebulaFrameClock::stepsOfSeconds(1), 0.0f, 180.0f));

    mSpeed *= 0.9f;
    BossStinkBugFunction::moveRail(getHost(), mSpeed, mLandingHeight, 0.0f);

    if (mLandingHeight <= 0.0f) {
        MR::reverseRailDirection(getHost());
        return true;
    } else {
        return false;
    }
}

bool BossStinkBugActionBase::updateLand() {
    if (MR::isFirstStep(this)) {
        MR::tryStartBck(getHost(), "Land");
        getHost()->mLegIKRate = 1.0f;
    }

    mSpeed *= 0.99f;
    BossStinkBugFunction::moveRail(getHost(), mSpeed, 0.0f, 0.0f);

    if (MR::isGreaterStep(this, 20)) {
        return true;
    } else {
        return false;
    }
}

bool BossStinkBugActionBase::updateToFly() {
    if (MR::isFirstStep(this)) {
        MR::startSound(getHost(), "SE_BV_BOSS_BUG_ANGRY_TAKEOFF");
        MR::startAction(getHost()->getWingModel(), "Fly");
        MR::tryStartBck(getHost(), "TakeOff");
        mToFlyStartSpeed = mSpeed;
        getHost()->validateCollisionHitOnly();
        getHost()->offBindLeg();
    }

    if (MR::isLessStep(this, 0)) {
        startSoundFly(0);
    } else {
        startSoundFly(2);
    }

    if (MR::isLessStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        mSpeed = MR::calcNerveEaseInOutValue(this, NebulaFrameClock::stepsOfSeconds(1), mToFlyStartSpeed, 40.0f);
    } else {
        mSpeed = MR::calcNerveEaseInOutValue(this, NebulaFrameClock::stepsOfSeconds(1), NebulaFrameClock::stepsOfSeconds(2), 40.0f, mFlySpeed);
    }

    f32 nerveEaseInOutValue = MR::calcNerveEaseInOutValue(this, NebulaFrameClock::stepsOfSeconds(2), 0.0f, mHeightOffset + mLocalHeightOffset);
    BossStinkBugFunction::moveRail(getHost(), mSpeed, nerveEaseInOutValue, 0.0f);

    if (MR::isGreaterStep(this, NebulaFrameClock::stepsOfSeconds(2))) {
        getHost()->validateCollisionFly();
        return true;
    } else {
        return false;
    }
}

void BossStinkBugActionBase::startSoundFly(s32 sound) {
    s32 flySoundVol = calcFlySoundVol();
    s32 iIRCCutoff = calcIIRCutOff();

    switch (sound) {
    case 0:
        if (isAngry()) {
            MR::startLevelSound(getHost(), "SE_BM_LV_BOSS_BUG_FLY_BASE2", flySoundVol, iIRCCutoff);
        } else {
            MR::startLevelSound(getHost(), "SE_BM_LV_BOSS_BUG_FLY_BASE", flySoundVol, iIRCCutoff);
        }

        MR::startLevelSound(getHost(), "SE_BM_LV_BOSS_BUG_FLY_ADD", flySoundVol, iIRCCutoff);
        MR::startLevelSound(getHost(), "SE_BM_LV_BOSS_BUG_FLY_ALARM", flySoundVol, iIRCCutoff);
        break;
    case 1:
        MR::startLevelSound(getHost(), "SE_BM_LV_BOSS_BUG_FLY_ROTATE", flySoundVol, iIRCCutoff);
        MR::startLevelSound(getHost(), "SE_BM_LV_BOSS_BUG_FLY_ROTATE2");
        MR::startLevelSound(getHost(), "SE_BM_LV_BOSS_BUG_FLY_ADD", flySoundVol, iIRCCutoff);
        MR::startLevelSound(getHost(), "SE_BM_LV_BOSS_BUG_FLY_ALARM", flySoundVol, iIRCCutoff);
        break;
    case 2:
        MR::startLevelSound(getHost(), "SE_BM_LV_BOSS_BUG_FLY_ROLL", flySoundVol, iIRCCutoff);
        MR::startLevelSound(getHost(), "SE_BM_LV_BOSS_BUG_FLY_ADD", flySoundVol, iIRCCutoff);
        MR::startLevelSound(getHost(), "SE_BM_LV_BOSS_BUG_FLY_ALARM", flySoundVol, iIRCCutoff);
        break;
    case 3:
        if (isAngry()) {
            MR::startLevelSound(getHost(), "SE_BM_LV_BOSS_BUG_FLY_SHAKE2", flySoundVol, iIRCCutoff);
        } else {
            MR::startLevelSound(getHost(), "SE_BM_LV_BOSS_BUG_FLY_SHAKE", flySoundVol, iIRCCutoff);
        }

        MR::startLevelSound(getHost(), "SE_BM_LV_BOSS_BUG_FLY_ADD", flySoundVol, iIRCCutoff);
        MR::startLevelSound(getHost(), "SE_BM_LV_BOSS_BUG_FLY_ALARM", flySoundVol, iIRCCutoff);
    }
}
