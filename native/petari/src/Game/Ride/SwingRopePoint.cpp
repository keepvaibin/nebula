#include "Game/Ride/SwingRopePoint.hpp"
#include "Game/Util/MathUtil.hpp"

SwingRopePoint::SwingRopePoint(const Vec3f& rPos)
    : mPosition(rPos), mVelocity(0.0f, 0.0f, 0.0f), mSide(1.0f, 0.0f, 0.0f), mUp(0.0f, 1.0f, 0.0f), mFront(0.0f, 0.0f, 1.0f) {
}

void SwingRopePoint::addAccel(const Vec3f& rAccel) {
    mVelocity.add(rAccel);
}

namespace {
    // The unit direction from the anchor to the point's next position; *pUp turns to face the anchor.
    // At the anchor itself the direction is -*pUp.
    Vec3f dirFromAnchor(const Vec3f& rNextPosDiff, Vec3f* pUp) {
        Vec3f dir = rNextPosDiff;
        if (MR::isNearZero(dir)) {
            dir.set(*pUp);
            dir.scale(-1.0f);
        } else {
            MR::normalize(&dir);
            pUp->set(dir);
            pUp->mult(-1.0f);
        }

        return dir;
    }
};  // namespace

bool SwingRopePoint::restrict(const Vec3f& rAnchor, f32 length, const Vec3f* pVel) {
    Vec3f nextPosDiff(mPosition);
    nextPosDiff.add(mVelocity);
    nextPosDiff.sub(rAnchor);

    if (pVel != nullptr) {
        nextPosDiff.sub(*pVel);
    }

    const Vec3f dir = dirFromAnchor(nextPosDiff, &mUp);

    if (nextPosDiff.squared() >= length * length) {
        Vec3f restriction = dir * length;
        mVelocity.x -= nextPosDiff.x - restriction.x;
        mVelocity.y -= nextPosDiff.y - restriction.y;
        mVelocity.z -= nextPosDiff.z - restriction.z;
        return true;
    }

    return false;
}

void SwingRopePoint::strain(const Vec3f& rAnchor, f32 length) {
    Vec3f nextPosDiff = mPosition;
    nextPosDiff.add(mVelocity);
    nextPosDiff.sub(rAnchor);

    const Vec3f dir = dirFromAnchor(nextPosDiff, &mUp);

    Vec3f restriction = dir * length;
    mVelocity.x -= nextPosDiff.x - restriction.x;
    mVelocity.y -= nextPosDiff.y - restriction.y;
    mVelocity.z -= nextPosDiff.z - restriction.z;
}

void SwingRopePoint::updatePos(f32 friction) {
    mPosition.add(mVelocity);
    mVelocity.scale(friction);
}

void SwingRopePoint::updateAxis(const Vec3f& rAxis) {
    Vec3f side = mSide;
    mSide.cross(mUp, rAxis);
    if (MR::isNearZero(mSide)) {
        mSide.set(side);
    } else {
        MR::normalize(&mSide);
    }

    Vec3f front = mFront;
    mFront.cross(mSide, mUp);
    if (MR::isNearZero(mFront)) {
        mFront.set(front);
    } else {
        MR::normalize(&mFront);
    }
}

void SwingRopePoint::updatePosAndAxis(const Vec3f& rAxis, f32 friction) {
    updatePos(friction);
    updateAxis(rAxis);
}

void SwingRopePoint::stop() {
    mVelocity.zero();
}

void SwingRopePoint::setInfo(const Vec3f& rTarget, const Vec3f& rVelocity, const Vec3f& rAnchor, f32 length) {
    mVelocity.set(rVelocity);

    Vec3f direction(rTarget);
    direction.sub(rAnchor);

    if (MR::isNearZero(direction)) {
        direction.set< f32 >(0.0f, -1.0f, 0.0f);
    } else {
        MR::normalize(&direction);
    }

    mPosition.set(direction);
    mPosition.scale(length);
    mPosition.add(rAnchor);

    mUp.set(direction);
    mUp.mult(-1.0f);

    // A front that is zero or parallel to up is replaced by Z, or by X when up is along Z.
    mSide.cross(mUp, mFront);
    if (MR::isNearZero(mSide)) {
        mFront.set< f32 >(0.0f, 0.0f, 1.0f);
        mSide.cross(mUp, mFront);
        if (MR::isNearZero(mSide)) {
            mFront.set< f32 >(1.0f, 0.0f, 0.0f);
            mSide.cross(mUp, mFront);
        }
    }

    mFront.cross(mSide, mUp);
    MR::normalize(&mFront);
    mSide.cross(mUp, mFront);
    MR::normalize(&mSide);
}

void SwingRopePoint::setPosAndAxis(const Vec3f& rPos, const Vec3f& rSide, const Vec3f& rUp, const Vec3f& rFront) {
    mPosition.set(rPos);
    mSide.set(rSide);
    mUp.set(rUp);
    mFront.set(rFront);
}

void SwingRopePoint::setAndUpdatePosAndAxis(const Vec3f& rPos, const Vec3f& rUp, const Vec3f& rAxis) {
    mPosition.set(rPos);
    mVelocity.zero();
    mUp.set(rUp);
    updatePos(1.0f);
    updateAxis(rAxis);
}
