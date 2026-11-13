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

void MarioActor::setupSensors() {
    initHitSensor(4);

    MR::addHitSensorCallback(this, "body", ATYPE_PLAYER, 32, 100.0f);
    MR::addHitSensorCallback(this, "ex-eye", ATYPE_PUSH, 32, 100.0f);
    MR::addHitSensorCallback(this, "eye", ATYPE_EYE, 64, 2000.0f);

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

    MR::initStarPointerTarget(this, 80.0f, Vec3f(0.0f, 80.0f, 0.0f));
}

void MarioActor::updateHitSensor(HitSensor* pSensor) {
    switch (pSensor->mType) {
    case ATYPE_PLAYER:
        if (mMario->isStatusActive(MarioStatus_Hang)) {
            getRealPos("Spine1", &pSensor->mPosition);
            pSensor->mRadius = mConst->getTable()->mSensorRadius * 60.0f / 100.0f;
            return;
        }

        pSensor->mPosition.set(mCenterPos);
        if (getMovementStates().hipDrop && !getMovementStates().onGround) {
            pSensor->mPosition.add(mMario->mJumpVec);
        }

        pSensor->mRadius = mConst->getTable()->mSensorRadius;
        return;
    case ATYPE_PUSH:
        pSensor->setType(ATYPE_EYE);
        return;
    case ATYPE_EYE:
        if (pSensor == getSensor("ex-eye")) {
            updateScouter();
            return;
        }

        f32 radius = 600.0f;
        pSensor->mPosition.set(mCenterPos);
        if (mMario->mMovementStates.tornado) {
            radius = 1000.0f;
        }

        if (mTakeCount) {
            radius = 2000.0f;
        }

        if (mMario->isSwimming()) {
            radius = 2000.0f;
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
            velocity *= 1.2f;
        } else {
            velocity *= 1.5f;
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

    if (MR::diffAngleAbsHorizontal(pSensor->mPosition - getSensor("ex-eye")->mPosition, mMario->mFrontVec, mGravityVec) >= 1.5707964f) {
        return;
    }

    mScoutSensor = pSensor;
    mScoutPrevRange = mScoutRange;
    mScoutRange = 60.0f;
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

    mScoutRange += 80.0f;
    if (mScoutRange < 100.0f) {
        mScoutRange = 100.0f;
    }

    if (mScoutRange > mScoutPrevRange) {
        mScoutSensor = nullptr;
    }

    f32 maximum = 1000.0f;
    if (mMario->isStatusActive(MarioStatus_Foo)) {
        maximum = 2400.0f;
    }

    if (mTakeCount) {
        maximum = 3000.0f;
    }

    if (mScoutRange > maximum) {
        mScoutRange = 60.0f;
    }

    Vec3f position;
    if (mMario->isSwimming()) {
        position = mCenterPos + mUpVec * mScoutRange;
    } else {
        position = mCenterPos + mMario->mFrontVec * mScoutRange;
    }

    getSensor("ex-eye")->mPosition = position;
    f32 radius = 100.0f;
    if (mScoutRange >= 300.0f) {
        radius = 100.0f + (mScoutRange - 300.0f) * MR::tan(5.0f * MR::pi() / 180.0f);
    }

    if (mScoutRange < 200.0f) {
        radius = 40.0f;
    }

    getSensor("ex-eye")->mRadius = radius;
}

void MarioActor::initScouter() {
    mScoutPrevRange = 0.0f;
    mScoutRange = 60.0f;
    mScoutSensor = 0;

    HitSensor* sensor = getSensor("ex-eye");
    sensor->mRadius = 100.0f;
    getSensor("ex-eye")->validate();
}

void MarioActor::initForJump() {
    mTrampleJumpStep = 0;
    mTrampleCombo = 0;
}
