#include "Game/Player/MarioWall.hpp"
#include "Game/Enemy/KarikariDirector.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioSwim.hpp"
#include "Game/Util/MapUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_geometry.hpp>
#include <cstring>

bool Mario::isWalling() const {
    return getCurrentStatus() == MarioStatus_Wall;
}

void Mario::checkWallStick() {
    if (getPlayerMode() == PlayerMode_Bee) {
        checkBeeStick();
        return;
    }

    if (!checkWallJumpCode() && isEnableStickWall()) {
        if (mActor->mGravityJumpTimer) {
            Vec3f horizontal;
            f32 vertical = MR::vecKillElement(mJumpVec, mActor->mGravityVec, &horizontal);
            horizontal.setLength(-0.25f);
            mJumpVec = mActor->mGravityVec * vertical + horizontal;
            return;
        }

        changeStatus(mWall);
        stopWalk();
        mMovementStates.jumping = false;
        mMovementStates.hipDrop = false;
        mMovementStates.wallSlipUp = false;
        mMovementStates.wallJumpQueued = false;
        fixWallingPosition(true);
        resetTornado();
    }
}

u8 Mario::checkStickWallSide() {
    if (isStickOn()) {
        const Vec3f& direction = getWorldPadDir();
        if (direction.dot(getWallNorm()) < -0.5f) {
            return 1;
        }

        if (direction.dot(getWallNorm()) > 0.5f && !mMovementStates.waitStickNeutral) {
            return 2;
        }
    }

    return 0;
}

s32 Mario::checkStickFrontBack() {
    if (isStickOn()) {
        const Vec3f& direction = getWorldPadDir();
        if (direction.dot(-mFrontVec) < -0.5f) {
            return 1;
        }

        if (direction.dot(-mFrontVec) > 0.5f && !mMovementStates.waitStickNeutral) {
            return 2;
        }
    }

    return 0;
}

MarioWall::MarioWall(MarioActor* pActor) : MarioState(pActor, MarioStatus_Wall) {
    mStickTimer = 0;
    mSlideTimer = 0;
    mCancel = 0;
    mOnBinderFloor = 0;
    mIceWall = 0;
    mSlideSpeed = 0.0f;
    mWallVec.zero();
    mTriangleJumpDir.zero();
}

void MarioWall::initTriangleJump() {
    Mario* player = getPlayer();
    MR::vecKillElement(player->mJumpVec, getGravityVec(), &mTriangleJumpDir);
    MR::normalizeOrZero(&mTriangleJumpDir);
    mStickTimer = 0;
}

u32 MarioWall::isCancel() {
    if (getPlayer()->mMovementStates.onGround) {
        mCancel = 0;
    }

    if (mCancel && getPlayer()->mMovementStates.frontWall) {
        if (mWallVec.dot(getPlayer()->getWallNorm()) < mActor->getConst().getTable()->mWallStickCancelAngle) {
            mCancel = 0;
        }
    }

    return mCancel;
}

bool Mario::fixWallingPosition(bool immediate) {
    if (!fixWallingDist()) {
        return false;
    }

    fixWallingDir(immediate);
    return true;
}

void Mario::fixWallingDir(bool immediate) {
    bool back = true;
    if (isStatusActive(MarioStatus_SideStep)) {
        back = false;
    }

    if (mMovementStates.backWall && back) {
        if (immediate) {
            setFrontVecKeepUp(-*mBackWallTriangle->getNormal(0));
            return;
        }

        setFrontVecKeepUp(-*mBackWallTriangle->getNormal(0), 0.1f);
        return;
    }

    if (mMovementStates.frontWall) {
        if (immediate) {
            setFrontVecKeepUp(-*mFrontWallTriangle->getNormal(0));
            return;
        }

        setFrontVecKeepUp(-*mFrontWallTriangle->getNormal(0), 0.1f);
    }
}

bool Mario::fixWallingTop() {
    Vec3f side = getWallNorm().cross(getAirGravityVec());
    MR::normalizeOrZero(&side);
    if (MR::isNearZero(side)) {
        return false;
    }

    mWallTopHead.cross(getWallNorm(), side);
    MR::normalizeOrZero(&mWallTopHead);
    if (MR::isNearZero(mWallTopHead)) {
        return false;
    }

    getWallNorm().dot(getAirGravityVec());
    getPlayer()->forceSetHeadVecKeepSide(mWallTopHead);
    return true;
}

bool Mario::checkWallFloorCode(u16 code) const {
    if ((mMovementStates.frontWall || mMovementStates.frontWallGlancing) && mWallFloorCodes[0] == code) {
        return true;
    }

    if (mMovementStates.backWall && mWallFloorCodes[1] == code) {
        return true;
    }

    if (mMovementStates.sideWall && mWallFloorCodes[2] == code) {
        return true;
    }

    return false;
}

bool Mario::checkWallCode(u16 code, bool frontBackOnly) const {
    if (mMovementStates.backWall && MR::getWallCodeIndex(mBackWallTriangle) == code) {
        return true;
    }

    if ((mMovementStates.frontWall || mMovementStates.frontWallGlancing) && MR::getWallCodeIndex(mFrontWallTriangle) == code) {
        return true;
    }

    if (frontBackOnly) {
        return false;
    }

    return mMovementStates.sideWall && MR::getWallCodeIndex(mSideWallTriangle) == code;
}

bool Mario::checkWallCodeNorm(u16 code, Vec3f* pNormal, bool frontBackOnly) const {
    if (mMovementStates.backWall) {
        u16 wallCode = MR::getWallCodeIndex(mBackWallTriangle);
        if (wallCode == code) {
            if (pNormal != nullptr) {
                *pNormal = *mBackWallTriangle->getNormal(0);
            }

            return true;
        }
    }

    if (mMovementStates.frontWall || mMovementStates.frontWallGlancing) {
        u16 wallCode = MR::getWallCodeIndex(mFrontWallTriangle);
        if (wallCode == code) {
            if (pNormal != nullptr) {
                *pNormal = *mFrontWallTriangle->getNormal(0);
            }

            return true;
        }
    }

    if (frontBackOnly) {
        return false;
    }

    if (mMovementStates.sideWall) {
        u16 wallCode = MR::getWallCodeIndex(mSideWallTriangle);
        if (wallCode == code) {
            if (pNormal != nullptr) {
                *pNormal = *mSideWallTriangle->getNormal(0);
            }

            return true;
        }
    }

    return false;
}

void Mario::setWallCancel() {
    mWall->mCancel = 1;
    mWall->mWallVec = getWallNorm();
}

void Mario::keepDistFrontWall() {
    if (mMovementStates.frontWall) {
        Vec3f horizontal;
        MR::vecKillElement(mFrontWallPos - mGroundPos, *getGravityVec(), &horizontal);
        if (horizontal.length() < 80.0f) {
            push(*mFrontWallTriangle->getNormal(0) * (80.0f - horizontal.length()));
        }
    }
}

bool Mario::isEnableStickWall() {
    switch (getPlayerMode()) {
    case PlayerMode_Teresa:
        return false;
    case PlayerMode_Foo:
        if (isStatusActive(MarioStatus_Foo)) {
            return false;
        }

        break;
    }

    if (mWall->isCancel()) {
        return false;
    }

    if (!mMovementStates.jumping || mMovementStates.onGround) {
        return false;
    }

    if (mMovementStates.hipDrop) {
        return false;
    }

    if (mMovementStates.tornado) {
        return false;
    }

    if (mDrawStates.pressed) {
        return false;
    }

    if (!mMovementStates.frontWall && !mMovementStates.backWall) {
        return false;
    }

    if (mMovementStates.shadowFound && mVerticalSpeed < mActor->getConst().getTable()->mWallStickGrHeight) {
        return false;
    }

    if (mMovementStates.frontLedge && mFrontFloorHeight < mActor->getConst().getTable()->mWallStickFrHeight && !getWallPolygon()->mSensor->isType(0x57)) {
        return false;
    }

    if (isAnimationRun("空中ひねり")) {
        if (mActor->mPunchFrame < 25) {
            return false;
        }
    } else if (mActor->isPunching() && mActor->mPunchFrame < 15) {
        return false;
    }

    if (mMovementStates.frontLedge && mMovementStates.shadowFound && mMovementStates.frontFloorFound) {
        if ((mFrontFloorPos - mShadowPos).dot(-*getGravityVec()) < mActor->getConst().getTable()->mWallStickStepHeight) {
            return false;
        }
    }

    Vec3f horizontal;
    if (mMovementStates.backWall) {
        MR::vecKillElement(mJumpVec, mActor->mGravityVec, &horizontal);
        if (mBackWallTriangle->getNormal(0)->dot(horizontal) > 0.0f) {
            return false;
        }
    }

    if (isInhibitWall()) {
        return false;
    }

    if (mMovementStates.frontWall && calcPolygonAngleD(mFrontWallTriangle) < 80.0f) {
        return false;
    }

    if (mMovementStates.backWall && calcPolygonAngleD(mBackWallTriangle) < 80.0f) {
        return false;
    }

    Vec3f side = getWallNorm().cross(getAirGravityVec());
    if (MR::normalizeOrZero(&side)) {
        return false;
    }

    if (checkWallCode(CollisionWallCode_NoAction, true)) {
        return false;
    }

    if ((getPlayerMode() != PlayerMode_Ice || !getWallPolygon()->mSensor->isType(0x57)) && isRising()) {
        return false;
    }

    if (mMovementStates.backWall) {
        if (!isAnimationRun("壁ジャンプ") && getPlayer()->checkStickWallSide() != 1) {
            return false;
        }
    } else if (!isAnimationRun("壁ジャンプ") && !mMovementStates.wallJump && getPlayer()->checkStickWallSide() != 1) {
        return false;
    }

    if (mMovementStates.hangBlocked) {
        return false;
    }

    if (mActor->mTakeCount) {
        if (!checkTrgA()) {
            return false;
        }

        mWall->initTriangleJump();
        mWall->startJump();
        return false;
    }

    Vec3f position(mActor->mHeadPos);
    f32 distance = 120.0f;
    if (mMovementStates.backWall) {
        distance = -120.0f;
    }

    if (!MR::isExistMapCollision(position, mFrontVec * distance)) {
        return false;
    }

    return MR::getKarikariClingNum() == 0;
}

bool MarioWall::start() {
    mSlideTimer = 0;
    mSlideSpeed = 0.0f;
    changeAnimation("壁くっつき");
    startPadVib(0u);
    mOnBinderFloor = 0;
    initTriangleJump();
    mIceWall = 0;

    if (getPlayerMode() == PlayerMode_Ice && getPlayer()->getWallPolygon()->mSensor->isType(0x57)) {
        MarioActor* actor = mActor;
        actor->createIceWall(getPlayer()->getWallPos() - getPlayer()->getWallNorm() * 30.0f, getPlayer()->getWallNorm());
        playEffectRT("氷壁ジャンプ", getPlayer()->getWallNorm(), getPlayer()->getWallPos() - getPlayer()->getWallNorm() * 15.0f);
        playSound("スケート着地");
        mIceWall = 1;
    }

    return true;
}

bool MarioWall::update() {
    bool release = false;
    if (mActor->mGravityJumpTimer) {
        return false;
    }

    if (MR::getKarikariClingNum()) {
        return false;
    }

    if ((isStatusActiveID(MarioStatus_Rabbit) || mActor->mFrameCount - getPlayer()->mJumpFrame < 6) && startJump()) {
        return false;
    }

    if (getPlayer()->mDrawStates.binderFloorHit) {
        if (mOnBinderFloor) {
            Vec3f horizontal;
            MarioActor* actor = mActor;
            if (MR::vecKillElement(actor->getLastMove(), getGravityVec(), &horizontal) < 1.0f) {
                release = true;
            }
        }

        getPlayer()->mMovementStates.onGround = true;
        mOnBinderFloor = 1;
    } else {
        mOnBinderFloor = 0;
    }

    mStickTimer++;
    if (getPlayer()->mMovementStates.onGround && !release) {
        if (getPlayer()->mVerticalSpeed < 80.0f) {
            if (!isOnSlipGround()) {
                getPlayer()->setFrontVecKeepUp(getPlayer()->getWallNorm());
                changeAnimation("着地");
                changeAnimationInterpoleFrame(1);
                mActor->setBlendMtxTimer(4);
            }

            release = true;
        } else {
            getPlayer()->mMovementStates.onGround = false;
        }
    }

    if (mStickTimer >= getActor()->getConst().getTable()->mWallReleaseTime + getActor()->getConst().getTable()->mWallStickTime) {
        release = true;
    }

    if (mStickTimer >= 3 && !getPlayer()->mMovementStates.frontWall && !getPlayer()->mMovementStates.frontWallGlancing) {
        release = true;
    }

    if (release) {
        if (!getPlayer()->mMovementStates.onGround) {
            mCancel = 1;
            Vec3f normal(getPlayer()->getWallNorm());
            mWallVec = normal;
            getPlayer()->setFrontVecKeepUp(-normal);
        }

        if (isOnSlipGround()) {
            addVelocityAfter(getPlayer()->getWallNorm() * 10.0f);
        } else {
            addVelocityAfter(getPlayer()->getWallNorm() * 30.0f);
        }

        return false;
    }

    if (checkTrgA() && startJump()) {
        if (mIceWall) {
            playSound("スケートジャンプ");
        }

        return false;
    }

    if (mActor->isRequestRush()) {
        getPlayer()->mMovementStates.spinBlocked = false;
        changeAnimation("空中ひねり");
        getPlayer()->tryWallPunch();
        getPlayer()->setWallCancel();
        return false;
    }

    if (!getPlayer()->fixWallingPosition(false)) {
        getPlayer()->setWallCancel();
        return false;
    }

    f32 blend = 0.9f;
    f32 speed = getActor()->getConst().getTable()->mWallDropSpeedNormal;
    u8 side = getPlayer()->checkStickWallSide();
    if (side == 1) {
        side = 0;
    }

    switch (side) {
    case 1:
        blend = 0.7f;
        if (mStickTimer > getActor()->getConst().getTable()->mWallStickTime) {
            mStickTimer = getActor()->getConst().getTable()->mWallStickTime;
        }

        mSlideTimer = 0;
        speed = getActor()->getConst().getTable()->mWallDropSpeedStop;
        changeAnimation("壁くっつき");
        stopEffect("共通壁手擦り");
        break;
    case 2:
        if (mIceWall) {
            if (mStickTimer < getActor()->getConst().getTable()->mWallStickTimeIce - 15) {
                mStickTimer = getActor()->getConst().getTable()->mWallStickTimeIce - 15;
            }
        } else if (mStickTimer < 165) {
            mStickTimer = 165;
        }

        if (!mSlideTimer) {
            mSlideTimer = 1;
        }

        stopEffect("共通壁手擦り");
        break;
    case 0:
        if (mStickTimer < getActor()->getConst().getTable()->mWallStickTime) {
            stopEffect("共通壁手擦り");
        } else {
            if (!isAnimationRun("壁くっつき")) {
                changeAnimation("壁すべり");
            }

            playSound("スリップ");
            playEffect("共通壁手擦り");
        }

        break;
    }

    if (mSlideTimer) {
        mSlideTimer++;
    }

    if (mStickTimer < getActor()->getConst().getTable()->mWallStickTime) {
        blend = 1.0f;
    }

    if (mIceWall) {
        if (mStickTimer > getActor()->getConst().getTable()->mWallStickTimeIce) {
            mCancel = 1;
            mWallVec = getPlayer()->getWallNorm();
            return false;
        }

        return true;
    }

    mSlideSpeed = mSlideSpeed * blend + speed * (1.0f - blend);
    addVelocity(getPlayer()->mWallTopHead, -mSlideSpeed);
    if (side <= 1) {
        f32 ratio;
        if (mStickTimer < getActor()->getConst().getTable()->mWallStickTime) {
            ratio = 0.0f;
        } else {
            ratio = 1.0f - (mStickTimer - getActor()->getConst().getTable()->mWallStickTime) * getActor()->getConst().getTable()->mWallSideMoveRatio;
        }

        getPlayer()->moveWallSlide(MR::clamp(ratio, 0.0f, 1.0f));
    }

    return true;
}

bool MarioWall::close() {
    stopAnimation("壁くっつき");
    stopAnimation("壁すべり");
    if (getPlayer()->mMovementStates.onGround) {
        changeAnimation(nullptr, "基本");
    }

    stopEffect("共通壁手擦り");
    getPlayer()->resetTornado();
    getPlayer()->mMovementStates.jumpBuffered = false;
    return true;
}

bool MarioWall::startJump() {
    if (getPlayer()->isInhibitWall()) {
        return false;
    }

    Vec3f velocity(getPlayer()->getWallNorm());
    mWallVec = velocity;
    if (!getPlayer()->mDrawStates.wallJumpForbidden && mStickTimer < 15 && !MR::isNearZero(mTriangleJumpDir)) {
        f32 dot = velocity.dot(-mTriangleJumpDir);
        if (dot < mActor->getConst().getTable()->mWallTriJumpMargin && dot > 0.0f) {
            Mtx rotation;
            Vec3f axis;
            axis.cross(-mTriangleJumpDir, velocity);
            NebulaMtxRotAxisRad(rotation, &axis, MR::acos(dot));
            NebulaMtxMultVec(rotation, &velocity, &velocity);
        }
    }

    velocity.scale(mActor->getConst().getTable()->mWallJumpPowerXZ);
    velocity += getGravityVec() * -mActor->getConst().getTable()->mWallJumpPowerY;
    getPlayer()->tryWallJump(velocity, true);
    playEffect("共通壁ジャンプ");
    mCancel = 1;
    getPlayer()->mMovementStates.spinBlocked = false;
    return true;
}

bool MarioWall::startBackJump(u32 type) {
    if (getPlayer()->isInhibitWall()) {
        return false;
    }

    Vec3f velocity(getPlayer()->getWallNorm());
    if (getPlayer()->mMovementStates.jumping && velocity.dot(getPlayer()->mJumpVec) > 0.0f) {
        return false;
    }

    mWallVec = velocity;
    velocity.scale(mActor->getConst().getTable()->mWallBackJumpPowerXZ);
    velocity += getGravityVec() * -mActor->getConst().getTable()->mWallBackJumpPowerY;
    getPlayer()->tryWallJump(velocity, false);
    switch (type) {
    case 0: {
        playEffectRTZ("結界ヒット", mWallVec, getPlayer()->getWallPos());
        playSound("結界ヒット");
        Mario* player = getPlayer();
        player->mAirWalkTimer = 0;
        player = getPlayer();
        player->mAirControlInhibitTimer = 60;
        break;
    }

    case 1:
        playEffectRTZ("水壁ヒット", mWallVec, getPlayer()->getWallPos());
        playSound("水弾かれ");
        break;
    case 2:
        playSound("トランポリンジャンプ大");
        break;
    }

    startPadVib(2);
    mCancel = 1;
    getPlayer()->mMovementStates.spinBlocked = true;
    return true;
}

bool Mario::fixWallingDist() {
    if (mMovementStates.backWall & mMovementStates.frontWall) {
        return true;
    }

    if (mMovementStates.backWall) {
        Vec3f position(mBackWallPos + *mBackWallTriangle->getNormal(0) * 79.0f);
        f32 distance = MR::vecKillElement(position - mPosition, *mBackWallTriangle->getNormal(0), &position);
        mPosition += *mBackWallTriangle->getNormal(0) * distance;
        if (!fixWallingTop()) {
            return false;
        }

        mPosition = mBackWallPos + *mBackWallTriangle->getNormal(0) * 60.0f - mWallTopHead * 80.0f;
    } else if (mMovementStates.frontWall) {
        if (!fixWallingTop()) {
            return false;
        }

        if (MR::isExistMapCollision(mPosition, mFrontVec * 100.0f) && mFrontVec.dot(*mFrontWallTriangle->getNormal(0)) < -0.999f) {
            Vec3f position(mFrontWallPos + *mFrontWallTriangle->getNormal(0) * 60.0f - mWallTopHead * 80.0f);
            Vec3f correction;
            MR::vecKillElement(position - mPosition, *getGravityVec(), &correction);
            if (!MR::isNearZero(correction)) {
                Vec3f relative(correction + mActor->mLastPushMove);
                if (MR::abs(relative.x) < MR::abs(correction.x)) {
                    correction.x = relative.x;
                }

                if (MR::abs(relative.y) < MR::abs(correction.y)) {
                    correction.y = relative.y;
                }

                if (MR::abs(relative.z) < MR::abs(correction.z)) {
                    correction.z = relative.z;
                }

                addTrans(correction);
            }
        }
    }

    return true;
}

bool Mario::isInhibitWall() const {
    if (mDrawStates.wallInhibited) {
        return true;
    }

    if (checkWallCode(CollisionWallCode_NotWallJump, true)) {
        return true;
    }

    return checkWallCode(CollisionWallCode_NoAction, true);
}

void Mario::tryWallPunch() {
    if (!mMovementStates.spinBlocked && !checkWallCodeNorm(8, nullptr, false) && (!isStatusActive(MarioStatus_Skate) || !checkWallFloorCode(0x21))) {
        if (isSwimming()) {
            if (getPlayer()->mMovementStates.frontWall) {
                mSwim->hitWall(*mFrontWallTriangle->getNormal(0), mFrontWallTriangle->mSensor);
            }

            if (getPlayer()->mMovementStates.backWall) {
                mSwim->hitWall(*mBackWallTriangle->getNormal(0), mBackWallTriangle->mSensor);
            }

            if (getPlayer()->mMovementStates.sideWall) {
                mSwim->hitWall(*mSideWallTriangle->getNormal(0), mSideWallTriangle->mSensor);
            }
        } else {
            stopWalk();
            Vec3f velocity(getWallNorm() * mActor->getConst().getTable()->mWallSpinFlipGround);
            if (mMovementStates.jumping) {
                velocity.scale(mActor->getConst().getTable()->mWallSpinFlipAirRatio);
            }

            velocity -= *getGravityVec() * mActor->getConst().getTable()->mWallSpinHopGround;
            tryForcePowerJump(velocity, true);
            mMovementStates.spinBlocked = true;
            startPadVib(2);
            playSound("壁反射");
            playSound("声スピンキャンセル");
            playEffectTrans("壁ヒット", getWallPos());
            if (getPlayer()->mMovementStates.frontWall) {
                sendPunch(mFrontWallTriangle->mSensor, true);
            }

            if (getPlayer()->mMovementStates.backWall) {
                sendPunch(mBackWallTriangle->mSensor, true);
            }

            if (getPlayer()->mMovementStates.sideWall) {
                sendPunch(mSideWallTriangle->mSensor, true);
            }

            mActor->mSwingInhibitTimer = 30;
        }
    }
}

bool MarioWall::notice() {
    return false;
}
