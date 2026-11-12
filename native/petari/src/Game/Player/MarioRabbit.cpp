#include "Game/Player/MarioRabbit.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Util/JointUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"

void Mario::startRabbitMode() {
    if (!isStatusActive(MarioStatus_Rabbit)) {
        changeStatus(mRabbit);
    }
}

void Mario::endRabbitMode() {
    if (isStatusActive(MarioStatus_Rabbit)) {
        closeStatus(mRabbit);
    }
}

MarioRabbit::MarioRabbit(MarioActor* pActor) : MarioState(pActor, MarioStatus_Rabbit), mVerticalSpeed() {
    mMoveVelocity.zero();
    mWallStart = 0;
    mForcedJump = 0;
    mImpacted = 0;
    mHighJump = 0;
    mJumpSide = 0;
    mJumpBuffer = 0;
    mTurnLockTimer = 0;
    NebulaMtxIdentity(mBodyTiltMtx);
    mBodyFront.set(1.0f, 0.0f, 0.0f);
    mBounceSound = 0;
}

bool MarioRabbit::start() {
    mHighJump = 0;
    mWallStart = 0;
    mJumpSide = 0;
    mJumpBuffer = 0;
    Mario* player = getPlayer();
    MR::vecKillElement(player->mJumpVec, getAirGravityVec(), &mMoveVelocity);
    mBodyFront = getFrontVec();
    if (mForcedJump || getPlayer()->mMovementStates.jumping) {
        mBounceSound = 1;
        return true;
    }

    stopAnimationUpper(nullptr);
    mVerticalSpeed = -mActor->getConst().getTable()->mRabbitFirstJump;
    mForcedJump = 0;
    mTurnLockTimer = 0;
    if (getPlayer()->mDrawStates.wallDamageCheck) {
        mVerticalSpeed *= 0.25f;
        mMoveVelocity = getPlayer()->getWallNorm() * mActor->getConst().getTable()->mRabbitFirstJump * 0.5f;
        getPlayer()->setFrontVecKeepUp(getPlayer()->getWallNorm());
        mWallStart = 1;
        mTurnLockTimer = 60;
    }

    impact();
    return true;
}

void MarioRabbit::hop() {
    if (mVerticalSpeed < 0.0f) {
        mVerticalSpeed = 0.5f * -mActor->getConst().getTable()->mRabbitFirstJump;
    } else {
        mVerticalSpeed = 0.3f * -mActor->getConst().getTable()->mRabbitFirstJump;
    }

    mJumpSide = 0;
}

void MarioRabbit::forceJump() {
    mForcedJump = 1;
    if (getPlayer()->mJumpKind != 12) {
        mJumpSide = 1;
    }
}

void MarioRabbit::impact() {
    if (!getPlayer()->mDrawStates.wallDamageCheck) {
        if (mMoveVelocity.length() > 2.0f * mActor->getConst().getTable()->mRabbitMoveSpeed) {
            mMoveVelocity.setLength(2.0f * mActor->getConst().getTable()->mRabbitMoveSpeed);
        }

        mMoveVelocity.setLength(0.5f * mMoveVelocity.length());
    }

    getPlayer()->stopJump();
    getPlayer()->mMovementStates.onGround = false;
    getPlayer()->mMovementStates.jumping = true;
    getPlayer()->initJumpParam();
    Mario* player = getPlayer();
    player->mInvincibleJump = 0;
    player->mJumpKind = 0;
    getPlayer()->mMovementStates.inputLocked = false;
    getPlayer()->mMovementStates.spinBlocked = false;
    if (mHighJump) {
        switch (mJumpSide) {
        case 0:
            changeAnimationNonStop("ホッパーハイジャンプA");
            break;
        case 1:
            changeAnimationNonStop("ホッパーハイジャンプB");
            break;
        }

        mJumpSide = 1 - mJumpSide;
    } else if (!MR::isNearZero(getStickP())) {
        switch (mJumpSide) {
        case 0:
            changeAnimationNonStop("ホッパー移動A");
            break;
        case 1:
            changeAnimationNonStop("ホッパー移動B");
            break;
        }

        mJumpSide = 1 - mJumpSide;
    } else {
        switch (mJumpSide) {
        case 0:
            changeAnimationNonStop("ホッパージャンプA");
            break;
        case 1:
            changeAnimationNonStop("ホッパージャンプB");
            break;
        }
    }

    mImpacted = 1;
}

bool MarioRabbit::update() {
    if (getPlayer()->mMorphResetTimer) {
        return false;
    }

    getPlayer()->mMovementStates.hangBlocked = false;
    getPlayer()->checkWallStick();
    if (mJumpBuffer) {
        mJumpBuffer--;
    }

    if (!getPlayer()->mMovementStates.jumping && !getPlayer()->mMovementStates.onGround) {
        getPlayer()->mMovementStates.onGround = true;
    }

    if (getPlayer()->mMovementStates.onGround) {
        if (mBounceSound) {
            playSound("ホッパー跳ね返り");
            mBounceSound = 0;
        }

        if (mImpacted || isAnimationRun("ホッパー壁ジャンプ") || isAnimationRun("ホッパーヒップドロップ")) {
            stopAnimation(nullptr);
            switch (mJumpSide) {
            case 0:
                changeAnimation("ホッパージャンプA");
                break;
            case 1:
                changeAnimation("ホッパージャンプB");
                break;
            }

            mHighJump = 0;
            mImpacted = 0;
            if (!getPlayer()->mMovementStates.hipDrop) {
                getPlayer()->mMovementStates.jumping = false;
            }

            startPadVib(1u);
            playEffect("共通着地普通");
            if (!mHighJump) {
                playSound("ホッパー跳ね返り");
            }
        }

        if (getPlayer()->mLandTimer < mActor->getConst().getTable()->mHopperLandingTime) {
            if (mActor->isRequestJump() || mJumpBuffer) {
                mHighJump = 1;
                playEffect("共通ハイジャンプ");
                startPadVib("マリオ[ホッパーため]");
                switch (mJumpSide) {
                case 0:
                    changeAnimationNonStop("ホッパーハイジャンプA");
                    break;
                case 1:
                    changeAnimationNonStop("ホッパーハイジャンプB");
                    break;
                }
            }

            return true;
        }

        if (mHighJump) {
            if (getPlayer()->mLandTimer < mActor->getConst().getTable()->mRabbitChargeTime2) {
                playSound("ホッパージャンプ溜め");
                return true;
            }

            mVerticalSpeed = -mActor->getConst().getTable()->mRabbitFirstJump2;
            playSound("声物ジャンプ");
            playSound("ホッパージャンプ");
        } else {
            mVerticalSpeed = -mActor->getConst().getTable()->mRabbitFirstJump;
        }

        impact();
        mForcedJump = 0;
    } else {
        if (mForcedJump) {
            getPlayer()->procJump(false);
            return true;
        }

        if (mActor->isRequestHipDrop()) {
            if (getPlayer()->jumpToHipDrop()) {
                mJumpSide = 0;
            }
        }

        if (mActor->isRequestJump()) {
            mJumpBuffer = 3;
        }
    }

    getPlayer()->mJumpVec = getAirGravityVec() * mVerticalSpeed;
    addVelocity(getAirGravityVec(), mVerticalSpeed);
    if (mHighJump) {
        if (mVerticalSpeed < 0.0f) {
            mVerticalSpeed += mActor->getConst().getTable()->mRabbitGravityRise2;
        } else {
            mVerticalSpeed += mActor->getConst().getTable()->mRabbitGravityDrop2;
        }
    } else {
        if (mVerticalSpeed < 0.0f) {
            mVerticalSpeed += mActor->getConst().getTable()->mRabbitGravityRise;
        } else {
            mVerticalSpeed += mActor->getConst().getTable()->mRabbitGravityDrop;
        }
    }

    if (mVerticalSpeed > 50.0f) {
        mVerticalSpeed = 50.0f;
    }

    if (getStickP() != 0.0f) {
        if (mTurnLockTimer) {
            mTurnLockTimer--;
        } else {
            const Vec3f& direction = getWorldPadDir();
            if (mHighJump) {
                getPlayer()->setFrontVecKeepUp(direction, mActor->getConst().getTable()->mRabbitTurnRatio2);
                if (mVerticalSpeed < 0.0f) {
                    mMoveVelocity += getFrontVec() * mActor->getConst().getTable()->mRabbitMoveAcc2;
                } else {
                    mMoveVelocity += getFrontVec() * mActor->getConst().getTable()->mRabbitMoveAcc3;
                }
            } else {
                getPlayer()->setFrontVecKeepUp(direction, mActor->getConst().getTable()->mRabbitTurnRatio);
                mMoveVelocity += getFrontVec() * mActor->getConst().getTable()->mRabbitMoveAcc;
            }
        }
    }

    if (!mWallStart) {
        if (mMoveVelocity.length() > mActor->getConst().getTable()->mRabbitMoveSpeed) {
            mMoveVelocity.setLength(mActor->getConst().getTable()->mRabbitMoveSpeed);
        }
    }

    addVelocity(mMoveVelocity);
    f32 angle = MR::diffAngleAbsHorizontal(getFrontVec(), mBodyFront, getAirGravityVec());
    Vec3f cross = getFrontVec().cross(mBodyFront);
    if (cross.dot(getAirGravityVec()) < 0.0f) {
        angle = -angle;
    }

    NebulaMtxCopy(MR::tmpMtxRotXRad(angle), mBodyTiltMtx);
    if (MR::abs(angle) >= 1.0471976f) {
        MR::vecBlendSphere(mBodyFront, getFrontVec(), &mBodyFront, 0.2f);
    } else {
        MR::vecBlendSphere(mBodyFront, getFrontVec(), &mBodyFront, 0.05f);
    }

    MR::normalizeOrZero(&mBodyFront);
    switch (mJumpSide) {
    case 0:
        setJointGlobalMtx(static_cast< u32 >(MR::getJointIndex(mActor, "Hip")), mBodyTiltMtx);
    case 1:
        setJointGlobalMtx(static_cast< u32 >(MR::getJointIndex(mActor, "Spine1")), mBodyTiltMtx);
        break;
    }

    return true;
}

bool MarioRabbit::close() {
    stopAnimation(nullptr);
    if (getPlayer()->mMovementStates.jumping) {
        stopAnimation(nullptr, "落下");
    } else {
        stopAnimation(nullptr, "基本");
    }

    setJointGlobalMtx(static_cast< u32 >(MR::getJointIndex(mActor, "Hip")), nullptr);
    setJointGlobalMtx(static_cast< u32 >(MR::getJointIndex(mActor, "Spine1")), nullptr);
    return true;
}
