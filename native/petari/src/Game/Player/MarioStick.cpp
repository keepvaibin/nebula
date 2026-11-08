#include "Game/Player/MarioStick.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include <nebula_math.h>

void Mario::checkBeeStick() {
    return;
}

void Mario::tryBeeStick(const HitSensor* pSensor) {
    bool res = mStick->setStickSensor(pSensor);
    if (res) {
        changeStatus(mStick);
    }
}

MarioStick::MarioStick(MarioActor* pActor) : MarioState(pActor, MarioStatus_Stick) {
    mStickPos.zero();
    mStickDir.zero();
    mFlowerUp.zero();
    mFlowerPos.zero();
    mFlowerRadius = 0.0f;
    mPoseFront.zero();
    mPoseUp.zero();
}

bool MarioStick::startJump() {
    Vec3f v7(mFlowerUp);
    v7.scale(mActor->getConst().getTable()->mBeeStickJumpPower);
    f32 v2 = -mActor->getConst().getTable()->mBeeStickJumpBonus;
    v7 += getGravityVec() * v2;
    getPlayer()->tryStickJump(v7);
    getPlayer()->mBeeGravityTimer = 0;
    return true;
}

bool MarioStick::postureCtrl(MtxPtr pMtx) {
    MR::makeMtxFrontUp(reinterpret_cast< Pos3f* >(pMtx), mPoseFront, mPoseUp);
    return true;
}

bool MarioStick::setStickSensor(const HitSensor* pSensor) {
    if (getPlayer()->mAirTimer < 0x78) {
        return false;
    }

    if (getPlayer()->isStatusActive(MarioStatus_Stick)) {
        return false;
    }

    if (pSensor->isType(ATYPE_BEE_FLOWER)) {
        Vec3f v12;
        MR::getRotatedAxisY(&v12, pSensor->mHost->mRotation);
        Vec3f v11(mActor->mCenterPos - pSensor->mPosition);
        MR::normalizeOrZero(&v11);

        if (v12.dot(v11) < mActor->getConst().getTable()->mBeeStickAngleLimit) {
            return false;
        }

        if (mActor->getLastMove().dot(v11) >= 0.0f) {
            return false;
        }

        v11.setLength(pSensor->mRadius);
        mStickPos = pSensor->mPosition + v11 * 0.9f;
        getPlayer()->setTrans(mStickPos, "Stick");
        clearVelocity();
        MR::normalize(&v11);
        mStickDir = v11;
        mFlowerUp = v12;
        mFlowerPos = pSensor->mPosition;
        mFlowerRadius = pSensor->mRadius;
        return true;
    }

    return false;
}

bool MarioStick::start() {
    changeAnimation("ハチ壁くっつき", "ハチ壁くっつき中");
    startPadVib(0u);
    playEffectTrans("ハチ花くっつき", mFlowerPos);
    playSound("ハチ壁くっつき");
    mActor->mBindOn = 0;
    getPlayer()->setFrontVecKeepSide(-mStickDir);
    mPoseFront = -mStickDir;
    mPoseUp = getPlayer()->mHeadVec;
    getPlayer()->mMovementStates.jumping = 0;
    getPlayer()->mMovementStates.onGround = 0;
    return true;
}

bool MarioStick::update() {
    getPlayer()->setTrans(mStickPos, nullptr);

    if (checkTrgA()) {
        if (!isAnimationRun("ハチ壁くっつき")) {
            if (startJump()) {
                return false;
            }
        }
    }

    Mario* player = getPlayer();
    Mtx secondRotMtx;
    Mtx rotMtx;
    NebulaMtxRotAxisRad(rotMtx, &player->mSideVec, 0.01f * getStickY());

    Vec3f stack_2C(mStickPos - mFlowerPos);
    NebulaMtxMultVec(rotMtx, &stack_2C, &stack_2C);

    if (MR::diffAngleAbs(stack_2C, mFlowerUp) < 1.308997f) {
        Vec3f stack_20(-stack_2C);
        if (!MR::normalizeOrZero(&stack_20)) {
            mPoseFront = stack_20;
        }

        getPlayer()->setFrontVecKeepSide(stack_20);
    }

    const f32 angle = 0.01f * -getStickX();
    const Vec3f* gravityVec = getPlayer()->getGravityVec();
    NebulaMtxRotAxisRad(secondRotMtx, gravityVec, angle);
    NebulaMtxMultVec(secondRotMtx, &stack_2C, &stack_2C);

    if (getStickP() > 0.0f) {
        changeAnimation("ハチ花移動");
    } else {
        stopAnimation("ハチ花移動");
    }

    if (MR::diffAngleAbs(stack_2C, mFlowerUp) < 1.308997f) {
        Vec3f stack_14(-stack_2C);
        if (!MR::normalizeOrZero(&stack_14)) {
            mPoseFront = stack_14;
        }

        getPlayer()->setFrontVecKeepUp(stack_14);
        stack_2C.setLength(mFlowerRadius);
        mStickPos = stack_2C + mFlowerPos;
    }

    mPoseFront = getFrontVec();
    mPoseUp = getPlayer()->mHeadVec;

    getPlayer()->incAirWalkTimer();
    getPlayer()->incAirWalkTimer();
    getPlayer()->incAirWalkTimer();
    getPlayer()->incAirWalkTimer();

    return true;
}

bool MarioStick::close() {
    stopAnimation("ハチ壁くっつき", "落下");

    if (getPlayer()->mMovementStates.onGround) {
        changeAnimation(nullptr, "基本");
    }

    getPlayer()->mMovementStates.jumpBuffered = 0;
    mActor->mBindOn = 1;
    playEffectTrans("ハチ花くっつき", mFlowerPos);
    return true;
}

bool MarioStick::notice() {
    return false;
}
