#include "Game/LiveActor/HitSensor.hpp"
#include "Game/MapObj/CollectCounter.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioFoo.hpp"
#include "Game/Player/MarioRabbit.hpp"
#include "Game/Player/MarioSwim.hpp"
#include "Game/Player/MarioWall.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_setupSensors_f1, "setupSensors.f1", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setupSensors_f2, "setupSensors.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setupSensors_f3, "setupSensors.f3", 2000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setupSensors_f4, "setupSensors.f4", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setupSensors_f5, "setupSensors.f5", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f1, "updateHitSensor.f1", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f2, "updateHitSensor.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f3, "updateHitSensor.f3", 600.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f4, "updateHitSensor.f4", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f5, "updateHitSensor.f5", 2000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateHitSensor_f6, "updateHitSensor.f6", 2000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_trampleJump_f1, "trampleJump.f1", 1.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_trampleJump_f2, "trampleJump.f2", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_recordScoutingObject_f1, "recordScoutingObject.f1", 1.5707964f);
NEBULA_TUNABLE_NAMED(f32, cNum_recordScoutingObject_f2, "recordScoutingObject.f2", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateScouter_f1, "updateScouter.f1", 80.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateScouter_f2, "updateScouter.f2", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateScouter_f3, "updateScouter.f3", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateScouter_f4, "updateScouter.f4", 1000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateScouter_f5, "updateScouter.f5", 2400.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateScouter_f6, "updateScouter.f6", 3000.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateScouter_f7, "updateScouter.f7", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateScouter_f8, "updateScouter.f8", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateScouter_f9, "updateScouter.f9", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateScouter_f10, "updateScouter.f10", 100.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateScouter_f11, "updateScouter.f11", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateScouter_f12, "updateScouter.f12", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateScouter_f13, "updateScouter.f13", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateScouter_f14, "updateScouter.f14", 200.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateScouter_f15, "updateScouter.f15", 40.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initScouter_f1, "initScouter.f1", 60.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_initScouter_f2, "initScouter.f2", 100.0f);

void MarioActor::setupSensors() {
    initHitSensor(4);

    MR::addHitSensorCallback(this, "body", ATYPE_PLAYER, 32, cNum_setupSensors_f1);
    MR::addHitSensorCallback(this, "ex-eye", ATYPE_PUSH, 32, cNum_setupSensors_f2);
    MR::addHitSensorCallback(this, "eye", ATYPE_EYE, 64, cNum_setupSensors_f3);

    MR::addHitSensor(this, "dummy", ATYPE_PLAYER, 1, 0.0f, Vec3f(0.0f, 0.0f, 0.0f));

    getSensor("dummy")->invalidate();

    mTakeCount = 0;
    mTakeCandidate = nullptr;

    mClapCoinCount = 0;

    for (s32 idx = 0; idx < mPullSensors.size(); idx++) {
        mPullSensors[idx] = nullptr;
    }

    for (s32 idx = 0; idx < mTakeSensors.size(); idx++) {
        mTakeSensors[idx] = nullptr;
    }

    initScouter();

    mPunchSensorOn = false;
    mPolygonAttackDone = false;

    MR::initStarPointerTarget(this, cNum_setupSensors_f4, Vec3f(0.0f, cNum_setupSensors_f5, 0.0f));
}

void MarioActor::updateHitSensor(HitSensor* pSensor) {
    switch (pSensor->mType) {
    case ATYPE_PLAYER:
        if (mMario->isStatusActive(MarioStatus_Hang)) {
            getRealPos("Spine1", &pSensor->mPosition);
            pSensor->mRadius = cNum_updateHitSensor_f1;
            return;
        }

        pSensor->mPosition.set(mCenterPos);
        if (getMovementStates().hipDrop && !getMovementStates().onGround) {
            pSensor->mPosition.add(mMario->mJumpVec);
        }

        pSensor->mRadius = cNum_updateHitSensor_f2;
        return;
    case ATYPE_PUSH:
        pSensor->setType(ATYPE_EYE);
        return;
    case ATYPE_EYE:
        if (pSensor == getSensor("ex-eye")) {
            updateScouter();
            return;
        }

        f32 radius = cNum_updateHitSensor_f3;
        pSensor->mPosition.set(mCenterPos);
        if (mMario->mMovementStates.tornado) {
            radius = cNum_updateHitSensor_f4;
        }

        if (mTakeCount) {
            radius = cNum_updateHitSensor_f5;
        }

        if (mMario->isSwimming()) {
            radius = cNum_updateHitSensor_f6;
        }

        pSensor->mRadius = radius;
        mPunchSensorOn = false;
        mPunchTimerSensor = false;
        if (MR::isEqualString(mMarioAnim->mXanimePlayer->getCurrentBckName(), "spin2nd")) {
            mPunchSensorOn = true;
            mPunchTimerSensor = true;
            if (mPunchHitTimer <= 2) {
                if (!mPunchHitTimer) {
                    mPunchFrame = 0;
                    mAirPunchHitCount = 0;
                }

                mPunchHitTimer = 2;
            }
        }

        if (!mMario->isAnimationTerminate(nullptr)) {
            if (mMario->mJumpKind == 8 && isJumping()) {
                mPunchSensorOn = true;
            }

            if (isAnimationRun("地上ひねり")) {
                mPunchSensorOn = true;
            }

            if (isAnimationRun("サマーソルト")) {
                mPunchSensorOn = true;
            }

            if (isAnimationRun("水泳スピン")) {
                mPunchSensorOn = true;
            }

            if (isAnimationRun("水上スピン")) {
                mPunchSensorOn = true;
            }

            if (isAnimationRun("しゃがみスピン")) {
                mPunchSensorOn = true;
            }

            if (isAnimationRun("フーファイタースピン")) {
                mPunchSensorOn = true;
            }

            if (isAnimationRun("ハチスピン")) {
                mPunchSensorOn = true;
            }

            if (isAnimationRun("ハチスピン空中")) {
                mPunchSensorOn = true;
            }

            if (isAnimationRun("アイスひねり空中")) {
                mPunchSensorOn = true;
            }

            if (isAnimationRun("ファイアスピン空中")) {
                mPunchSensorOn = true;
            }

            if (isAnimationRun("ファイアスピン")) {
                mPunchSensorOn = true;
            }

            if (isAnimationRun("アイスひねり")) {
                mPunchSensorOn = true;
            }

            if (isAnimationRun("アイスひねり移動")) {
                mPunchSensorOn = true;
            }

            if (isAnimationRun("アイスひねり静止")) {
                mPunchSensorOn = true;
            }

            if (isAnimationRun("ハンマー投げリリース")) {
                mPunchSensorOn = true;
            }

            if (mMario->isSwimming() && mMario->mSwim->isSpinning()) {
                mPunchSensorOn = true;
            }

            if (mMario->isStatusActive(MarioStatus_Foo) && mMario->mFoo->mSpinTimer) {
                mPunchSensorOn = true;
            }

            if (mPunchSensorOn) {
                mPunchTimerSensor = true;
            }
        }

        if (pSensor->isValid()) {
            attackOrPushPolygons();
        }

        if (mTakenSensor != nullptr) {
            tryTornadoPull(mTakenSensor);
        }

        return;
    }
}

void MarioActor::doTrampleJump(HitSensor* pSensor) {
    if (mMario->mHitStates.trampleBlocked) {
        return;
    }

    if (getMario()->isStatusActive(MarioStatus_Wall)) {
        mMario->closeStatus(nullptr);
    }

    mMario->mAirWalkTimer = getConst().getTable()->mAirWalkTimeTornado;
    mMario->mMovementStates.spinBlocked = false;
    switch (pSensor->mType) {
    case 0x1F:
        mTrampleJumpStep = 0;
        trampleJump(getConst().getTable()->mTrampleBegoma, getConst().getTable()->mTrampleLong);
        changeAnimationNonStop("ヘリコプタージャンプ");
        mMario->startPadVib(2u);
        playSound("ヘリコプタージャンプ", -1);
        mMario->startRotationTask(4);
        mMario->mJumpKind = 11;
        break;
    default:
        if (MR::isEqualString(pSensor->mHost->mName, "砲弾")) {
            mMario->playSoundTrampleCombo(mTrampleCombo);
            mTrampleCombo++;
            if (mTrampleCombo > 1 && mTrampleCombo < 5) {
                mTrampleCounter->setCount(mTrampleCombo);
            }

            if (mTrampleCombo == 5) {
                mTrampleCounter->kill();
                mTrampleCombo = 0;
                MR::requestOneUp();
                MR::incPlayerLeft();
            }
        }

        if (MR::isEqualString(pSensor->mHost->mName, "全滅用クリボー")) {
            mMario->playSoundTrampleCombo(mTrampleCombo);
            mTrampleCombo++;
            if (mTrampleCombo > 1 && mTrampleCombo < 8) {
                mTrampleCounter->setCount(mTrampleCombo);
            }

            if (mTrampleCombo >= 8) {
                mTrampleCounter->kill();
                MR::requestOneUp();
                MR::incPlayerLeft();
            }
        }

        trampleJump(getConst().getTable()->mTrampleNormal, getConst().getTable()->mTrampleLong);
        break;
    }
}

void MarioActor::trampleJump(f32 normal, f32 extra) {
    if (mMario->isStatusActive(MarioStatus_Foo)) {
        return;
    }

    Vec3f velocity(mMario->mJumpVec);
    MR::vecKillElement(velocity, getGravityVec(), &velocity);
    velocity.add(-getGravityVec() * normal);
    switch (mTrampleJumpStep) {
    case 2:
        if (mMario->checkLvlA()) {
            velocity *= cNum_trampleJump_f1;
        } else {
            velocity *= cNum_trampleJump_f2;
        }

        break;
    }

    if (mMario->checkLvlA()) {
        velocity.add(-getGravityVec() * extra);
    }

    mMario->tryForceFreeJump(velocity);
    mMario->popTask(&Mario::taskOnHipDropSlide);
    if (mPlayerMode != PlayerMode_Hopper) {
        if (!mMario->mMovementStates.squat) {
            mTrampleJumpStep++;
            if (mTrampleJumpStep == 1) {
                changeAnimation("ジャンプふみ1");
            } else if (mTrampleJumpStep == 2) {
                changeAnimation("ジャンプふみ2");
            } else if (mTrampleJumpStep >= 3) {
                changeAnimation("ジャンプふみ3");
                mTrampleJumpStep = 0;
            }
        }
    } else {
        stopAnimation(nullptr);
        switch (mMario->mRabbit->mJumpSide) {
        case 0:
            changeAnimation("ホッパーふみジャンプA");
            break;
        case 1:
            changeAnimation("ホッパーふみジャンプB");
            break;
        }
    }

    playSound("声踏み", -1);
    playEffect("ふみつぶし");
    mMario->startPadVib(0u);
    mMario->mMovementStates.forcedLaunch = false;
    mMario->mMovementStates.inputLocked = false;
    mMario->mMovementStates.launchKind = 0;
}

void MarioActor::attackSensor(HitSensor* pOwn, HitSensor* pOther) {
    if (!isEnableNerveChange()) {
        return;
    }

    if (pOwn->mType == ATYPE_PLAYER) {
        if (mInRush && !getSensor("eye")->isValid()) {
            addRushSensor(pOther, false);
        }
    } else {
        if (pOwn == getSensor("eye")) {
            if (MR::isDead(pOther->mHost)) {
                return;
            }

            if (mInRush) {
                attackOrPushSensorInRush(pOther, (pOther->mPosition - pOwn->mPosition).length());
            } else if (isDamaging()) {
                attackOrPushSensorInDamage(pOther, (pOther->mPosition - pOwn->mPosition).length());
            } else {
                attackOrPushSensor(pOther, (pOther->mPosition - pOwn->mPosition).length());
            }
        }

        if (pOwn == getSensor("ex-eye")) {
            recordScoutingObject(pOther);
        }
    }
}

bool MarioActor::sendMsgToSensor(HitSensor* pSensor, u32 msg) {
    return pSensor->receiveMessage(msg, getSensor("body"));
}

void MarioActor::resetSensorCount() {
    mRushSensorCount = 0;
    mClapCoinCount = 0;
    mTakeCandidate = 0;
}

void MarioActor::recordScoutingObject(HitSensor* pSensor) {
    if (pSensor == mTakenSensor) {
        return;
    }

    if (!MR::isSensorEnemy(pSensor) && !MR::isSensorMapObj(pSensor) && !MR::isSensorRide(pSensor)) {
        return;
    }

    if (MR::diffAngleAbsHorizontal(pSensor->mPosition - getSensor("ex-eye")->mPosition, mMario->mFrontVec, mGravityVec) >= cNum_recordScoutingObject_f1) {
        return;
    }

    mScoutSensor = pSensor;
    mScoutPrevRange = mScoutRange;
    mScoutRange = cNum_recordScoutingObject_f2;
}

void MarioActor::updateScouter() {
    const u32 count = mTakeCount;
    HitSensor* previous = mThrowAimSensor;
    mThrowAimSensor = nullptr;
    if (count) {
        if (mScoutSensor != nullptr && MR::isExistInAttributeGroupSearchTurtle(mScoutSensor->mHost)) {
            if (MR::isSensorEnemy(mScoutSensor)) {
                mThrowAimHold = 16;
            } else {
                mThrowAimHold = 2;
            }

            mThrowAimSensor = mScoutSensor;
        } else if (mThrowAimHold) {
            mThrowAimSensor = previous;
            mThrowAimHold--;
        }
    }

    mScoutRange += cNum_updateScouter_f1;
    if (mScoutRange < cNum_updateScouter_f2) {
        mScoutRange = cNum_updateScouter_f3;
    }

    if (mScoutRange > mScoutPrevRange) {
        mScoutSensor = nullptr;
    }

    f32 maximum = cNum_updateScouter_f4;
    if (mMario->isStatusActive(MarioStatus_Foo)) {
        maximum = cNum_updateScouter_f5;
    }

    if (mTakeCount) {
        maximum = cNum_updateScouter_f6;
    }

    if (mScoutRange > maximum) {
        mScoutRange = cNum_updateScouter_f7;
    }

    Vec3f position;
    if (mMario->isSwimming()) {
        position = mCenterPos + mUpVec * mScoutRange;
    } else {
        position = mCenterPos + mMario->mFrontVec * mScoutRange;
    }

    getSensor("ex-eye")->mPosition = position;
    f32 radius = cNum_updateScouter_f8;
    if (mScoutRange >= cNum_updateScouter_f9) {
        radius = cNum_updateScouter_f10 + (mScoutRange - cNum_updateScouter_f11) * MR::tan(cNum_updateScouter_f12 * MR::pi() / cNum_updateScouter_f13);
    }

    if (mScoutRange < cNum_updateScouter_f14) {
        radius = cNum_updateScouter_f15;
    }

    getSensor("ex-eye")->mRadius = radius;
}

void MarioActor::initScouter() {
    mScoutPrevRange = 0.0f;
    mScoutRange = cNum_initScouter_f1;
    mScoutSensor = 0;

    HitSensor* sensor = getSensor("ex-eye");
    sensor->mRadius = cNum_initScouter_f2;
    getSensor("ex-eye")->validate();
}

void MarioActor::initForJump() {
    mTrampleJumpStep = 0;
    mTrampleCombo = 0;
}
