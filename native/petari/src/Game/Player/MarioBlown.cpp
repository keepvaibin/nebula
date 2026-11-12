#include "Game/Player/MarioBlown.hpp"
#include "Game/Player/FireMarioBall.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Util/MathUtil.hpp"

bool Mario::blown(const Vec3f& rVec) {
    if (getCurrentStatus() == MarioStatus_Blown) {
        return false;
    }

    if (getCurrentStatus() == MarioStatus_Damage) {
        return false;
    }

    if (getCurrentStatus() == MarioStatus_RecoveryOrWarp) {
        return false;
    }

    stopWalk();

    mBlown->setVelocityOffGravity(rVec);

    mMovementStates.blown = true;
    mMovementStates.squat = false;
    mJumpKind = 3;

    return true;
}

MarioBlown::MarioBlown(MarioActor* pActor) : MarioState(pActor, MarioStatus_Blown), mTimer(), mStep() {
    mVelocity.zero();
    mJumpedOut = false;
    mAirLanding = false;
}

bool MarioBlown::start() {
    mTimer = 0;
    mStep = 0;
    mJumpedOut = false;
    mAirLanding = false;

    changeAnimation("壁ヒット", "基本");
    playSound("声壁体当たり");
    playSound("壁衝突");

    playEffectTrans("壁ヒット", getPlayer()->getWallPos());

    getPlayer()->mMovementStates.onGround = false;
    getPlayer()->mMovementStates.jumping = true;
    getPlayer()->mMovementStates.hipDrop = false;

    mVelocity += -mActor->mGravityVec * mActor->getConst().getTable()->mJumpHeightBlown;
    getPlayer()->setJumpVec(mVelocity);
    addVelocity(mVelocity);
    getPlayer()->lockGroundCheck(this, true);
    return true;
}

bool MarioBlown::update() {
    mTimer++;

    if (getPlayerMode() == PlayerMode_Teresa) {
        return false;
    }

    switch (mStep) {
    case 0:
    case 2:
        addVelocity(mVelocity);
        mVelocity += mActor->mGravityVec * mActor->getConst().getTable()->mGravityBlown;
        if (mTimer > 120) {
            changeAnimation("中ダメージ空中");
        }

        if (mTimer > 60) {
            Vec3f killed;
            if (MR::isNearZero(MR::vecKillElement(mActor->getLastMove(), mActor->mGravityVec, &killed))) {
                return false;
            }
        }

        if (getPlayer()->getMovementStates().onGround || MR::isNearZero(getPlayer()->mVerticalSpeed)) {
            getPlayer()->mMovementStates.jumping = false;
            getPlayer()->mMovementStates.forceGroundOnce = true;

            if (getPlayer()->damagePolygonCheck(getPlayer()->getGroundPolygon())) {
                getPlayer()->mHitStates.blown = true;
                mJumpedOut = true;
                return false;
            }

            if (mStep != 2 || mTimer >= 3) {
                playSound("吹っ飛び倒れ");
                changeAnimation("壁ヒット着地");
                playEffect("共通壁ヒット着地");
                MR::vecKillElement(mVelocity, mActor->mGravityVec, &mVelocity);
            }

            mStep = 1;
            mTimer = 0;
        }

        break;
    case 1:
        if (!getPlayer()->getMovementStates().onGround && !MR::isNearZero(getPlayer()->mVerticalSpeed)) {
            mAirLanding = true;
            return false;
        }

        addVelocity(mVelocity);
        mVelocity.mult(0.95f);

        if (!isAnimationRun("壁ヒット着地")) {
            return false;
        }

        if (mTimer > 15 && checkTrgA()) {
            getPlayer()->stopJump();
            getPlayer()->tryJump();
            mJumpedOut = true;
            return false;
        }

        break;
    }

    f32 dot = setVelocityOffGravity(mVelocity);

    if (mVelocity.length() > 10.0f) {
        mVelocity.mult(0.5f);
    }

    mVelocity += mActor->mGravityVec * MR::clamp(dot, 0.0f, 40.0f);
    getPlayer()->setJumpVec(mVelocity);
    return true;
}

bool MarioBlown::close() {
    if (!mJumpedOut) {
        getPlayer()->stopJump();
    }

    stopAnimation("壁ヒット");

    if (mAirLanding) {
        stopAnimation("壁ヒット着地", "基本");
    }

    mActor->setBlendMtxTimer(6);
    getPlayer()->unlockGroundCheck(this);

    if (!mJumpedOut) {
        getPlayer()->mMovementStates.simpleGroundCheck = false;
    }

    return true;
}
