#include "Game/LiveActor/HitSensor.hpp"
#include "Game/Map/HitInfo.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Player/MarioConst.hpp"
#include "Game/Player/MarioState.hpp"
#include "Game/Player/MarioSwim.hpp"
#include "Game/Util/MathUtil.hpp"

bool MarioActor::isFastRise() const {
    if (mInRush) {
        if (mRushMove.dot(getGravityVec()) < -60.0f) {
            return true;
        }
    }

    return false;
}

bool MarioActor::isLongDrop() const {
    if (mMario->getMovementStates().shadowFound == 0) {
        return true;
    }

    return mMario->mVerticalSpeed > 1000.0f;
}

bool MarioActor::isFastDrop() const {
    return mMario->getMovementStates().hipDrop;
}

u32 MarioActor::getSpecialMode() const {
    u32 mode = 0;
    if (mMario->getMovementStates().jumping && mMario->getMovementStates().tornado) {
        mode = 1;
    }

    if (mMario->getMovementStates().jumping && mMario->getMovementStates().beeFly) {
        mode = 1;
    }

    if (mode == 0) {
        return mode;
    }

    if (mMario->mVerticalSpeed < 300.0f) {
        mode = 2;
    }

    if (mMario->mVerticalSpeed > 800.0f) {
        return 3;
    }

    return mode;
}

bool MarioActor::isCameraStateOn(SPECIAL_STATUS_FOR_CAMERA status) const {
    switch (status) {
    case CameraStatus_FireDance:
        if (mMario->isStatusActive(MarioStatus_FireDance)) {
            return true;
        }

        break;

    case CameraStatus_HeliJump:
        if (isJumping() && mMario->mJumpKind == 11) {
            return true;
        }

        break;
    }

    return false;
}

void MarioActor::getFrontVec(Vec3f* pVec) const {
    *pVec = mMario->mFrontVec;
}

void MarioActor::getSideVec(Vec3f* pVec) const {
    *pVec = mMario->mSideVec;
}

void MarioActor::getUpVec(Vec3f* pVec) const {
    *pVec = mUpVec;
}

void MarioActor::getLastMove(Vec3f* pVec) const {
    *pVec = mLastMove;
}

const Vec3f& MarioActor::getLastMove() const {
    return mLastMove;
}

const Vec3f& MarioActor::getTransForCamera() const {
    return mCameraTrans;
}

void MarioActor::updateTransForCamera() {
    if (mBaseMtxForced) {
        return;
    }

    if (isNerve(GET_NERVE(MarioActor, MarioActorNrvGameOverBlackHole))) {
        mCameraTrans = mBlackHolePosition * 0.1f + mCameraTrans * 0.9f;
        return;
    }

    if (mMario->isStatusActive(MarioStatus_Step) || mMario->isStatusActive(MarioStatus_Bump)) {
        mCameraTrans = mCameraTrans * 0.8f + mPosition * 0.2f;
        return;
    }

    mCameraTrans = mPosition + Vec3f(mMario->mLookOfs);  // matches now. result of getter?

    if (mMario->isSwimming() && mMario->mSwim->isOnWaterSurface()) {
        if (mCameraLiftTimer < 100) {
            mCameraLiftTimer++;
        }
    } else if (mCameraLiftTimer != 0) {
        mCameraLiftTimer--;
    }

    mCameraTrans += mMario->mHeadVec * mCameraLiftTimer;
}

void MarioActor::updateForCamera() {
    if (mBaseMtxForced) {
        return;
    }

    if (!(mCameraUpTarget == mMario->mHeadVec)) {
        mCameraUpTarget = mMario->mHeadVec;
        mCameraUpTimer = mConst->getTable()->mCameraHeadRotationTimer;
    }

    if (mCameraUpTimer != 0) {
        mCameraUpTimer--;

        if (!MR::vecBlendSphere(mUpVec, mCameraUpTarget, &mUpVec, 1.0f - static_cast< f32 >(mCameraUpTimer) / mConst->getTable()->mCameraHeadRotationTimer)) {
            mUpVec = mCameraUpTarget;
        }
    }

    mCameraFrontVec = mMario->mFrontVec;
    mCameraSideVec = mMario->mSideVec;
}
