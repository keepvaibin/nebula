#include "Game/Camera/CameraTargetMtx.hpp"
#include "Game/AreaObj/CubeCamera.hpp"
#include "Game/Util/AreaObjUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/System/LumaCore.hpp"

CameraTargetMtx::CameraTargetMtx(const char* pName)
    : CameraTargetObj(pName), mPosition(0.0f, 0.0f, 0.0f), mLastMove(0.0f, 0.0f, 0.0f), mGravityVector(0.0f, -1.0f, 0.0f), mUp(0.0f, 1.0f, 0.0f),
      mFront(0.0f, 0.0f, 1.0f), mSide(0.0f, 0.0f, 1.0f), mInvalidLastMove(), mCameraArea() {
    lumacore::setClass(this, lumacore::kClassId< CameraTargetMtx >);
    mMatrix.identity();
}

const Vec3f& CameraTargetMtx::getPosition() const {
    return mPosition;
}

const Vec3f& CameraTargetMtx::getUpVec() const {
    return mUp;
}

const Vec3f& CameraTargetMtx::getFrontVec() const {
    return mFront;
}

const Vec3f& CameraTargetMtx::getSideVec() const {
    return mSide;
}

const Vec3f& CameraTargetMtx::getLastMove() const {
    return mLastMove;
}

const Vec3f& CameraTargetMtx::getGroundPos() const {
    return mPosition;
}

const Vec3f& CameraTargetMtx::getGravityVector() const {
    return mGravityVector;
}

CubeCameraArea* CameraTargetMtx::getCubeCameraArea() const {
    return mCameraArea;
}

void CameraTargetMtx::invalidateLastMove() {
    mInvalidLastMove = true;
}

void CameraTargetMtx::movement() {
    mMatrix.getYDir(mUp);
    mMatrix.getZDir(mFront);
    mMatrix.getXDir(mSide);

    Vec3f position;
    mMatrix.getTrans(position);

    if (mInvalidLastMove) {
        mInvalidLastMove = false;
        mLastMove.zero();
    } else {
        mLastMove.sub(position, mPosition);
    }

    mPosition.set(position);

    mCameraArea = static_cast< CubeCameraArea* >(MR::getAreaObj("CubeCamera", mPosition));

    MR::calcGravityVectorOrZero(this, mPosition, &mGravityVector, nullptr, 0);
}
