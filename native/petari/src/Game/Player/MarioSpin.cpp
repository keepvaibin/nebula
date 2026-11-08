#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/Mario.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Util/MathUtil.hpp"

void Mario::checkTornado() {
    if (mMovementStates.onGround) {
        mMovementStates.spinBlocked = false;
    }
}

void Mario::resetTornado() {
    mMovementStates.tornado = false;
    mTornadoTimer = 0;
    mYAngleOffset = 0.0f;
    mHandScaleX = 1.0f;
}

void Mario::calcTornadoTilt() {
    bool spinning = mMovementStates.tornado != 0;
    bool flying = false;
    if (getPlayerMode() == PlayerMode_Bee) {
        if (mMovementStates.jumping && mMovementStates.beeFly) {
            flying = true;
        }
    }

    bool tilted = spinning | flying;
    if (!isStickOn() || !tilted) {
        mTornadoTilt *= mActor->getConst().getTable()->mTornadoTiltCancel;
        bool blended;
        if (tilted) {
            blended = MR::vecBlendSphere(mTornadoTiltUp, mHeadVec, &mTornadoTiltUp, mActor->getConst().getTable()->mTornadoTiltSpeed);
        } else {
            blended = MR::vecBlendSphere(mTornadoTiltUp, mHeadVec, &mTornadoTiltUp, mActor->getConst().getTable()->mTornadoTiltOffSpeed);
        }

        if (!blended) {
            mTornadoTiltUp = mHeadVec;
        }
    } else {
        Vec3f tilt(getWorldPadDir() * mActor->getConst().getTable()->mTornadoTiltAngle +
                    mHeadVec * (1.0f - mActor->getConst().getTable()->mTornadoTiltAngle));
        MR::normalize(&tilt);
        bool blended = MR::vecBlendSphere(mTornadoTiltUp, tilt, &mTornadoTiltUp, mActor->getConst().getTable()->mTornadoTiltSpeed);
        MR::normalize(&mTornadoTiltUp);
        f32 alignment = MR::abs(getWorldPadDir().dot(mFrontVec));
        mTornadoTilt = mTornadoTilt * mActor->getConst().getTable()->mTornadoTiltNear + alignment * (1.0f - mActor->getConst().getTable()->mTornadoTiltNear);
        if (!blended) {
            mTornadoTiltUp = tilt;
        }
    }
}

void Mario::reflectWallOnSpinning(const Vec3f& rNormal, u16 time) {
    setFrontVecKeepUp(rNormal);
    mReflectSlipTimer = time;
    mMoveFrontVec = mFrontVec;
    doSpinWallEffect();
}

void Mario::forceStopTornado() {
    resetTornado();
    if (mMovementStates.jumping) {
        cancelTornadoJump();
    }

    mDrawStates.endTornado = true;
}

void Mario::doSpinWallEffect() {
    if ((!mMovementStates.frontWall || !mFrontWallTriangle->mSensor->isType(0x55)) && (!mMovementStates.backWall || !mBackWallTriangle->mSensor->isType(0x55)) &&
        (!mMovementStates.sideWall || !mSideWallTriangle->mSensor->isType(0x55))) {
        playSound("壁反射");
        playSound("声スピンキャンセル");
        playEffect("壁スパーク");
    }
}

void Mario::startRotationTask(u32 flags) {
    pushTask(&Mario::taskOnRotation, flags);
}

bool Mario::taskOnRotation(u32 flags) {
    if (flags & 4) {
        if (!isAnimationRun("ヘリコプタージャンプ")) {
            mYAngleOffset = 0.0f;
            return false;
        }

        if (isRising()) {
            mYAngleOffset += mActor->getConst().getTable()->mTrampleBegomaRotRise;
        } else {
            mYAngleOffset += mActor->getConst().getTable()->mTrampleBegomaRotFall;
        }
    }

    return true;
}
