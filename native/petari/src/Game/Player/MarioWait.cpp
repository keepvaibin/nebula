#include "Game/Player/MarioWait.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioAnimator.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_controlWaitAnimation_f1, "controlWaitAnimation.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_controlWaitAnimation_f2, "controlWaitAnimation.f2", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_controlWaitAnimation_f3, "controlWaitAnimation.f3", 1.5707964f);
NEBULA_TUNABLE_NAMED(f32, cNum_controlWaitAnimation_f4, "controlWaitAnimation.f4", 0.7853982f);
NEBULA_TUNABLE_NAMED(f32, cNum_controlWaitAnimation_f5, "controlWaitAnimation.f5", 0.7853982f);
NEBULA_TUNABLE_NAMED(f32, cNum_controlWaitAnimation_f6, "controlWaitAnimation.f6", 0.7853982f);
NEBULA_TUNABLE_NAMED(f32, cNum_controlWaitAnimation_f7, "controlWaitAnimation.f7", 1.5707964f);
NEBULA_TUNABLE_NAMED(f32, cNum_controlWaitAnimation_f8, "controlWaitAnimation.f8", 0.7853982f);
NEBULA_TUNABLE_NAMED(f32, cNum_controlWaitAnimation_f9, "controlWaitAnimation.f9", 0.7853982f);
NEBULA_TUNABLE_NAMED(f32, cNum_controlWaitAnimation_f10, "controlWaitAnimation.f10", 0.7853982f);
NEBULA_TUNABLE_NAMED(f32, cNum_stopWaitAnimation_f1, "stopWaitAnimation.f1", 1.5f);
NEBULA_TUNABLE_NAMED(f32, cNum_isBlendWaitGround_f1, "isBlendWaitGround.f1", 8.0f);

void MarioAnimator::controlWaitAnimation() {
    if (mWaitHoldTimer) {
        mWaitHoldTimer--;
        return;
    }

    Vec3f side;
    Mario* player = getPlayer();
    side.cross(getFrontVec(), player->mGroundNorm);
    MR::normalizeOrZero(&side);
    if (MR::isNearZero(side)) {
        return;
    }

    Vec3f front;
    front.cross(getPlayer()->mGroundNorm, side);
    MR::normalizeOrZero(&front);
    f32 sideSlope = side.dot(getPlayer()->getAirGravityVec());
    f32 frontSlope = front.dot(getPlayer()->getAirGravityVec());
    f32 weights[4];
    weights[0] = weights[1] = weights[2] = weights[3] = 0.0f;
    f32 sideBias = 0.0f;
    f32 frontBias = 0.0f;
    if (mWaitPhase == 1) {
        sideBias = cNum_controlWaitAnimation_f1;
    } else if (mWaitPhase == 2) {
        frontBias = cNum_controlWaitAnimation_f2;
    }

    f32 frontMagnitude = MR::abs(frontSlope);
    f32 sideMagnitude = MR::abs(sideSlope);
    if (sideBias + sideMagnitude > frontBias + frontMagnitude) {
        f32 angle = cNum_controlWaitAnimation_f3 - marioAcos(sideMagnitude);
        if (angle >= cNum_controlWaitAnimation_f4) {
            angle = cNum_controlWaitAnimation_f5;
        }

        weights[2] = angle / cNum_controlWaitAnimation_f6;
        weights[3] = 1.0f - weights[2];
        if (sideSlope < 0.0f) {
            mXanimePlayer->changeTrackAnimation(2, "坂右ウエイト");
        } else {
            mXanimePlayer->changeTrackAnimation(2, "坂左ウエイト");
        }

        if (getPlayer()->mActionStates.squatEnded) {
            forceSetBlendWeight(weights);
            getPlayer()->mActionStates.squatEnded = false;
        } else {
            setBlendWeight(weights, mActor->getConst().getTable()->mSlopeAnimBlendRatio);
        }

        mWaitPhase = 1;
    } else {
        f32 angle = cNum_controlWaitAnimation_f7 - marioAcos(frontMagnitude);
        if (angle >= cNum_controlWaitAnimation_f8) {
            angle = cNum_controlWaitAnimation_f9;
        }

        weights[2] = angle / cNum_controlWaitAnimation_f10;
        weights[3] = 1.0f - weights[2];
        if (frontSlope < 0.0f) {
            mXanimePlayer->changeTrackAnimation(2, "坂前ウエイト");
        } else {
            mXanimePlayer->changeTrackAnimation(2, "坂後ウエイト");
        }

        if (getPlayer()->mActionStates.squatEnded) {
            forceSetBlendWeight(weights);
            getPlayer()->mActionStates.squatEnded = false;
        } else {
            setBlendWeight(weights, mActor->getConst().getTable()->mSlopeAnimBlendRatio);
        }

        mWaitPhase = 2;
    }

    mWaitHoldTimer = 4;
}

void MarioAnimator::stopWaitAnimation() {
    if (getPlayerMode() == PlayerMode_Invincible && getPlayer()->mWalkSpeed >= cNum_stopWaitAnimation_f1) {
        return;
    }

    if (mWaitHoldTimer) {
        mWaitHoldTimer--;
        return;
    }

    if (isAnimationRun("基本")) {
        mXanimePlayer->changeTrackAnimation(2, "ラン");
    }
}

bool MarioWait::checkStart() {
    if (mActor->mInDemo) {
        mTimer = 0;
        return false;
    }

    if (getPlayer()->isStatusActive(MarioStatus_Wait)) {
        return false;
    }

    bool damaged = mActor->isHidden();
    if (damaged) {
        mTimer = 0;
        return false;
    }

    bool cancel = false;
    switch (getPlayer()->mFloorCode) {
    case 27:
    case 28:
        cancel = true;
        break;
    }

    if (getStickP() != 0.0f || checkLvlA() || checkLvlZ() || mActor->isRequestRush()) {
        cancel = true;
    } else if (getPlayer()->mMovementStates.jumping && getPlayerMode() != PlayerMode_Teresa) {
        cancel = true;
    } else if (getPlayer()->isStatusActive(MarioStatus_Swim)) {
        cancel = true;
    } else if (getPlayer()->isStatusActive(MarioStatus_Hang)) {
        cancel = true;
    } else if (getPlayer()->isStatusActive(MarioStatus_Sukekiyo)) {
        cancel = true;
    } else if (getPlayer()->isStatusActive(MarioStatus_Bury)) {
        cancel = true;
    } else if (getPlayerMode() == PlayerMode_Hopper || isStatusActiveID(MarioStatus_Foo)) {
        cancel = true;
    } else if (getPlayer()->isStatusActive(MarioStatus_FpView)) {
        cancel = true;
    } else if (getPlayer()->isStatusActive(MarioStatus_Magic)) {
        cancel = true;
    } else if (getPlayer()->isStatusActive(MarioStatus_Talk)) {
        cancel = true;
    }

    if (mActor->mBeeWallWalk) {
        cancel = true;
    }

    if (getPlayer()->mHitStates.karikariClinging) {
        cancel = true;
    }

    if (getPlayer()->mMovementStates.slopeSlide) {
        cancel = true;
    }

    if (getPlayer()->mMovementStates.squat) {
        cancel = true;
    }

    if (getPlayer()->mDrawStates.inWater) {
        cancel = true;
    }

    if (!MR::isNearZero(getPlayer()->mRelativeMove)) {
        cancel = true;
    }

    if (MR::isNearZero(getPlayer()->mWalkSpeed)) {
        mTimer++;
    } else {
        cancel = true;
    }

    if (cancel) {
        mTimer = 0;
        return false;
    }

    if (mTimer == 1800) {
        if (MR::getAreaObj("NonSleepCube", getTrans()) != nullptr) {
            mKind = 1;
            return true;
        }

        mKind = 0;
        return true;
    }

    return false;
}

bool MarioWait::start() {
    switch (mKind) {
    case 0:
        changeAnimation("特殊ウエイト1A");
        break;
    case 1:
        changeAnimation("戦闘ウエイト");
        break;
    }

    mTimer = 0;
    mStep = 0;
    return true;
}

bool MarioWait::update() {
    if (getStickP() != 0.0f || checkLvlA() || checkLvlZ() || mActor->isRequestJump2P()) {
        stopAnimation(nullptr);
        getPlayer()->mainMove();
        return false;
    }

    bool cancel = false;
    if (mActor->isRequestRush()) {
        cancel = true;
    }

    if (getPlayer()->isStatusActive(MarioStatus_FpView)) {
        cancel = true;
    }

    if (getPlayer()->mHitStates.karikariClinging) {
        cancel = true;
    }

    if (getPlayerMode() != PlayerMode_Teresa && !getPlayer()->mMovementStates.onGround) {
        cancel = true;
    }

    if (!MR::isNearZero(getPlayer()->mRelativeMove)) {
        cancel = true;
    }

    if (cancel) {
        stopAnimation(nullptr);
        return false;
    }

    mTimer++;
    switch (mKind) {
    case 0:
        switch (mStep) {
        case 0:
            if (!mActor->mTakeCount) {
                u32 duration = 1800;
                if (isPlayerModeTeresa()) {
                    duration = 210;
                }

                if (mTimer == duration) {
                    mTimer = 0;
                    mStep++;
                    changeAnimation("特殊ウエイト1B");
                }
            }

            break;
        case 1:
            break;
        }

        break;
    case 1:
    case 2:
        break;
    }

    return true;
}

bool MarioWait::close() {
    stopSound("声あくび", 0);
    stopSound("声いびき１", 0);
    stopSound("声いびき２", 0);
    mTimer = 0;
    return true;
}

bool Mario::isBlendWaitGround() const {
    return calcAngleD(mGroundNorm) > cNum_isBlendWaitGround_f1;
}

void Mario::checkSpecialWaitAnimation() {
    if (mWait->checkStart()) {
        changeStatus(mWait);
    }
}

void Mario::resetSleepTimer() {
    mWait->mTimer = 0;
    if (isStatusActive(MarioStatus_Wait)) {
        closeStatus(mWait);
    }
}

MarioWait::MarioWait(MarioActor* pActor) : MarioState(pActor, MarioStatus_Wait) {
    mTimer = 0;
    mKind = 0;
    mStep = 0;
}

void MarioState::hitPoly(u8, const Vec3f&, HitSensor*) {
}

bool MarioState::keep() {
    return true;
}

bool MarioState::notice() {
    return false;
}

void MarioState::init() {
}
