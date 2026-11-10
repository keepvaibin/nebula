#include "Game/Player/MarioFlip.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioDamage.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_start_f1, "start.f1", 0.75f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f1, "update.f1", 30.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f2, "update.f2", 0.98f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f3, "update.f3", 0.97f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f4, "update.f4", 0.97f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f5, "update.f5", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f6, "update.f6", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f7, "update.f7", 1.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f8, "update.f8", 2.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_update_f9, "update.f9", 1.2f);
NEBULA_TUNABLE_NAMED(f32, cNum_setVec_f1, "setVec.f1", 5.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_setVec_f2, "setVec.f2", 0.75f);

bool Mario::doFlipJump(const Vec3f& rVec) {
    if (isAnimationRun("壁はじき")) {
        return false;
    }

    stopWalk();
    Vec3f vec(rVec);
    vec.setLength(mActor->mConst->getTable()->mWallSpinFlipGround);
    vec -= getGravityVec()->scaleInline(mActor->mConst->getTable()->mWallSpinHopGround);
    tryForcePowerJump(vec, true);

    changeAnimation("壁はじき");

    stopPunch();
    mMovementStates.spinBlocked = true;
    mJumpKind = 3;
    startPadVib(2);

    return true;
}

bool Mario::doFlipBackRoll(const Vec3f& rVec) {
    Vec3f flat;
    MR::vecKillElement(rVec, getAirGravityVec(), &flat);

    if (doFlipLarge(flat)) {
        checkDamage();
        changeAnimationNonStop("後転ふっとび");

        mDamage->setStrings("後転ふっとび空中", "後転ふっとび着地");

        Vec3f flatAfter;
        MR::vecKillElement(rVec, getAirGravityVec(), &flatAfter);
        setFrontVecKeepUp(-flatAfter, static_cast< u32 >(12));

        return true;
    }

    return false;
}

bool MarioFlip::start() {
    mTimer = 0;
    mStep = 0;
    mYAngle = 0.0f;
    mYSpeed = -cNum_start_f1;

    changeAnimation("はねとばされ");

    playSound("声小ダメージ");
    playSound("壁衝突");
    playEffect("ダメージ");
    startPadVib(2);

    addVelocity(mVelocity);

    return true;
}

bool MarioFlip::update() {
    if (mTimer == 0) {
        changeAnimationNonStop("はねとばされ");
    }

    mTimer++;
    Vec3f velocity(mVelocity);
    Vec3f direction(mVelocity);
    MR::normalizeOrZero(&direction);
    f32 control;
    if (mTimer < 30) {
        control = static_cast< f32 >(mTimer) / cNum_update_f1;
    } else {
        control = 1.0f;
    }

    Vec3f lateral;
    f32 along = MR::vecKillElement(getWorldPadDir(), direction, &lateral);
    if (along <= 0.0f) {
        mVelocity += direction * mActor->mConst->getTable()->mFlipFriction3 * along * control;
    }

    mVelocity += lateral * mActor->mConst->getTable()->mFlipFriction3;
    switch (mStep) {
    case 0:
        mYAngle += mYSpeed;
        mYSpeed *= cNum_update_f2;
        playSound("はねとばされ");
        addVelocity(velocity);
        mVelocity.scale(mActor->mConst->getTable()->mFlipFriction1);
        if (mTimer == mActor->mConst->getTable()->mFlipTimer1) {
            mStep++;
        }

        break;
    case 1:
        mYAngle += mYSpeed;
        mYSpeed *= cNum_update_f3;
        playSound("はねとばされ");
        if (!getPlayer()->mMovementStates.onGround) {
            return false;
        }

        addVelocity(velocity);
        mVelocity.scale(mActor->mConst->getTable()->mFlipFriction2);
        if (mTimer == mActor->getConst().getTable()->mFlipTimer1 + mActor->mConst->getTable()->mFlipTimer2) {
            mStep++;
        }

        break;
    case 2:
        addVelocity(velocity);
        mVelocity.scale(mActor->mConst->getTable()->mFlipFriction2);
        mYSpeed *= cNum_update_f4;
        playSound("はねとばされ");
        mYAngle += mYSpeed;
        if (MR::isAngleBetween(mYAngle, -cNum_update_f5, cNum_update_f6)) {
            changeAnimation("はねとばされ終了");
            mStep++;
        }

        break;
    case 3:
        mYAngle = 0.0f;
        if (!isAnimationRun("はねとばされ終了")) {
            return false;
        }

        break;
    }

    mYAngle = MR::normalizeAngleAbs(mYAngle);
    setYangleOffset(mYAngle);
    if (getPlayer()->mMovementStates.frontWall || getPlayer()->mMovementStates.sideWall || getPlayer()->mMovementStates.backWall) {
        Vec3f normal(getPlayer()->getWallNorm());
        Vec3f tangent;
        f32 incoming = MR::vecKillElement(mVelocity, normal, &tangent);
        if (incoming < 0.0f) {
            stopAnimation(nullptr);
            changeAnimationNonStop("はねとばされ");
            playEffectTrans("壁ヒット", getPlayer()->getWallPos());
            mVelocity = tangent + normal * -incoming * cNum_update_f7;
            addVelocity(mVelocity, cNum_update_f8);
            mYSpeed *= cNum_update_f9;
            getPlayer()->setFrontVecKeepUp(-mVelocity);
        }
    }

    if (mStep >= 1 && checkTrgA()) {
        getPlayer()->tryJump();
        return false;
    }

    if (mStep == 3 && getStickP() != 0.0f) {
        return false;
    }

    return true;
}

bool Mario::doFlipRot(const Vec3f& rVec) {
    getCurrentStatus();
    if (getCurrentStatus() == MarioStatus_FireDamage) {
        return false;
    }

    if (mMovementStates.damaged) {
        return false;
    }

    mFlip->setVec(rVec);

    stopJump();
    stopWalk();
    forceStopTornado();

    changeStatus(mFlip);

    return true;
}

bool Mario::doFlipLarge(const Vec3f& rVec) {
    if (getCurrentStatus() == MarioStatus_Damage) {
        return false;
    }

    if (getCurrentStatus() == MarioStatus_Flip) {
        return false;
    }

    if (getCurrentStatus() == MarioStatus_FireDamage) {
        return false;
    }

    if (mMovementStates.damaged) {
        return false;
    }

    return flipLarge(rVec);
}

MarioFlip::MarioFlip(MarioActor* pActor) : MarioState(pActor, MarioStatus_Flip), mTimer(), mStep() {
    mVelocity.zero();
    mYAngle = 0.0f;
    mYSpeed = 0.0f;
}

void MarioFlip::setVec(const Vec3f& rVec) {
    MR::vecKillElement(rVec, mActor->mGravityVec, &mVelocity);

    if (mActor->mPunchSensorOn) {
        mVelocity.setLength(mActor->mConst->getTable()->mSlideDistFlipSpin);
    } else {
        mVelocity.setLength(mActor->mConst->getTable()->mSlideDistFlip);
    }

    addVelocity(mVelocity, cNum_setVec_f1);

    mTimer = 0;
    mStep = 0;
    mYSpeed = -cNum_setVec_f2;

    if (getPlayer()->isStatusActive(MarioStatus_Hang)) {
        mVelocity.zero();
        return;
    }

    getPlayer()->setFrontVecKeepUp(-mVelocity);
}

bool MarioFlip::close() {
    setYangleOffset(0.0f);

    stopAnimation("はねとばされ");
    stopAnimation("はねとばされ終了");

    if (mActor->mHealth == 0) {
        if (!getPlayer()->getMovementStates().onGround) {
            mActor->forceGameOverNonStop();
        } else {
            mActor->forceGameOver();
        }
    }

    return true;
}
