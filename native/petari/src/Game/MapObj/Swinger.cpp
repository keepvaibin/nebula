#include "Game/MapObj/Swinger.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include "Game/Util/MathUtil.hpp"

// A pendulum `length` long hanging from `pPivot` in the frame of `pBaseMtx`, pulled by `gravityAccel` along
// `pGravity` (or world -Y when null), its velocity kept by `damping` each update.
Swinger::Swinger(const Vec3f* pPivot, MtxPtr pBaseMtx, f32 length, f32 gravityAccel, f32 damping, const Vec3f* pGravity)
    : mPivot(pPivot), mTipPos(0.0f), mAcceleration(0.0f), mSwingDir(0.0f, -1.0f, 0.0f), mLength(length), mGravityAccel(gravityAccel), mDamping(damping),
      mGravity(pGravity), mSide(pBaseMtx[0][0], pBaseMtx[1][0], pBaseMtx[2][0]), mUp(pBaseMtx[0][1], pBaseMtx[1][1], pBaseMtx[2][1]),
      mFront(pBaseMtx[0][2], pBaseMtx[1][2], pBaseMtx[2][2]) {
    mTipPos.set(mPivot->x, (mPivot->y - mLength), mPivot->z);
    mSwingMtx.identity();
    mSwingMtx.setInline(pBaseMtx);
    mSwingMtx.setTrans(*mPivot);
}

void Swinger::accel(const Vec3f& rAccel) {
    mAcceleration.add(rAccel);
}

// The tip moves by the velocity (mAcceleration) and is pulled back to the length; the velocity keeps only its part
// across the rope.
void Swinger::update() {
    Vec3f pivot(*mPivot);
    mAcceleration.scale(mDamping);

    if (mGravity != nullptr) {
        Vec3f gravity = *mGravity;
        gravity.scale(mGravityAccel);
        mAcceleration.add(gravity);
    } else {
        mAcceleration.y -= mGravityAccel;
    }

    mTipPos.add(mAcceleration);
    mSwingDir.set(mTipPos - pivot);

    if (mSwingDir.isZero()) {
        mSwingDir.set(0.0f, -1.0f, 0.0f);
    } else {
        MR::normalize(&mSwingDir);
    }

    mTipPos.set(mSwingDir);
    mTipPos.scale(mLength);
    mTipPos.add(pivot);

    f32 alongRope = (mSwingDir.z * mAcceleration.z) + ((mSwingDir.x * mAcceleration.x) + (mSwingDir.y * mAcceleration.y));
    f32 acrossX = mAcceleration.x - (mSwingDir.x * alongRope);
    f32 acrossY = mAcceleration.y - (mSwingDir.y * alongRope);
    f32 acrossZ = mAcceleration.z - (mSwingDir.z * alongRope);
    mAcceleration.x = acrossX;
    mAcceleration.y = acrossY;
    mAcceleration.z = acrossZ;

    updateSwingMtx(pivot);
}

// Up along the rope toward the pivot; the side keeps its last value when up meets the front.
void Swinger::updateSwingMtx(const Vec3f& rPivot) {
    mUp.set(mSwingDir);
    mUp.scale(-1.0f);

    Vec3f lastSide(mSide);
    mSide.cross(mUp, mFront);
    if (MR::isNearZero(mSide)) {
        mSide.set(lastSide);
    }
    MR::normalize(&mSide);

    mFront.cross(mSide, mUp);
    MR::normalize(&mFront);

    mSwingMtx.setXDir(mSide);
    mSwingMtx.setYDir(mUp);
    mSwingMtx.setZDir(mFront);
    mSwingMtx.setTrans(rPivot);
}
