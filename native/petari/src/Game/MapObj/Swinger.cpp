#include "Game/MapObj/Swinger.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/MathUtil.hpp"

Swinger::Swinger(const Vec3f* a1, MtxPtr a2, f32 a3, f32 a4, f32 a5, const Vec3f* a6)
    : mPivot(a1), mTipPos(0.0f), mAcceleration(0.0f), mSwingDir(0.0f, -1.0f, 0.0f), mLength(a3), mGravityAccel(a4), mDamping(a5), mGravity(a6), mSide(a2[0][0], a2[1][0], a2[2][0]),
      mUp(a2[0][1], a2[1][1], a2[2][1]), mFront(a2[0][2], a2[1][2], a2[2][2]) {
    mTipPos.set(mPivot->x, (mPivot->y - mLength), mPivot->z);
    mSwingMtx.identity();
    mSwingMtx.setInline(a2);
    mSwingMtx.setTrans(*mPivot);
}

void Swinger::accel(const Vec3f& a1) {
    mAcceleration.add(a1);
}

void Swinger::update() {
    Vec3f v20(*mPivot);
    mAcceleration.scale(mDamping);

    if (mGravity) {
        Vec3f v19 = *mGravity;
        v19.scale(mGravityAccel);
        mAcceleration.add(v19);
    } else {
        mAcceleration.y -= mGravityAccel;
    }
    mTipPos.add(mAcceleration);
    mSwingDir.set(mTipPos - v20);

    if (mSwingDir.isZero()) {
        mSwingDir.set(0.0f, -1.0f, 0.0f);
    } else {
        MR::normalize(&mSwingDir);
    }
    mTipPos.set(mSwingDir);
    mTipPos.scale(mLength);
    mTipPos.add(v20);

    f32 xval = mAcceleration.x - (mSwingDir.x * ((mSwingDir.z * mAcceleration.z) + ((mSwingDir.x * mAcceleration.x) + (mSwingDir.y * mAcceleration.y))));
    f32 yval = mAcceleration.y - (mSwingDir.y * ((mSwingDir.z * mAcceleration.z) + ((mSwingDir.x * mAcceleration.x) + (mSwingDir.y * mAcceleration.y))));
    f32 zval = mAcceleration.z - (mSwingDir.z * ((mSwingDir.z * mAcceleration.z) + ((mSwingDir.x * mAcceleration.x) + (mSwingDir.y * mAcceleration.y))));
    mAcceleration.x = xval;
    mAcceleration.y = yval;
    mAcceleration.z = zval;

    updateSwingMtx(v20);
}

void Swinger::updateSwingMtx(const Vec3f& a1) {
    mUp.set(mSwingDir);
    mUp.x *= -1.0f;
    mUp.y *= -1.0f;
    mUp.z *= -1.0f;
    Vec3f v14(mSide);
    mSide.cross(mUp, mFront);
    if (MR::isNearZero(mSide)) {
        mSide.set(v14);
    }
    MR::normalize(&mSide);
    mFront.cross(mSide, mUp);
    MR::normalize(&mFront);

    mSwingMtx.mMtx[0][0] = mSide.x;
    mSwingMtx.mMtx[1][0] = mSide.y;
    mSwingMtx.mMtx[2][0] = mSide.z;
    mSwingMtx.mMtx[0][1] = mUp.x;
    mSwingMtx.mMtx[1][1] = mUp.y;
    mSwingMtx.mMtx[2][1] = mUp.z;
    mSwingMtx.mMtx[0][2] = mFront.x;
    mSwingMtx.mMtx[1][2] = mFront.y;
    mSwingMtx.mMtx[2][2] = mFront.z;

    mSwingMtx.setTrans(a1);
}
