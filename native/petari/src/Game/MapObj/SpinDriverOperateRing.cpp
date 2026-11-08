#include "Game/MapObj/SpinDriverOperateRing.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"

namespace {
    static const f32 sOperateAccel = 0.5f;
    static const f32 sToPlaneRate = 0.05f;
    static const f32 sDirectionRate = 0.1f;
};  // namespace

namespace NrvSpinDriverOperateRing {
    NEW_NERVE(SpinDriverOperateRingNrvWait, SpinDriverOperateRing, Wait);
};  // namespace NrvSpinDriverOperateRing

SpinDriverOperateRing::SpinDriverOperateRing(const char* pName)
    : LiveActor(pName), mCenter(0, 0, 0), mOffset(0, 0, 0), mAccelerate(0, 0, 0), mDirection(0, 0, 0), mOperateState(), mPointerPos(0.0f, 0.0f, 0.0f),
      mRadiusRate(), mOffsetLength(), mOffsetRate() {
}

void SpinDriverOperateRing::init(const TableIter& rIter) {
    makeActorAppeared();
    MR::invalidateClipping(this);
}

void SpinDriverOperateRing::control() {
}

void SpinDriverOperateRing::setRadiusRate(f32 rate) {
    mRadiusRate = MR::normalize(rate, 0.0f, 1.0f);
}

void SpinDriverOperateRing::reset() {
    resetVelocityAndTrans();
}

void SpinDriverOperateRing::update(const Vec3f& a1, const Vec3f& a2) {
    mCenter.set(a1);

    if (mRadiusRate <= 0.0f) {
        resetVelocityAndTrans();
        return;
    }

    updateControlPoint(a2);

    addAccelToOperatePlane(a2);

    Vec3f direction;
    addAccelOperate(&direction, a2);

    addAccelToCenter();
    attenuateVelocity();

    updateDirection(direction);
    updatePosition();
}

void SpinDriverOperateRing::updatePosition() {
    mOffset += mAccelerate;
    mOffsetLength = mOffset.length();
    mOffsetRate = mOffsetLength / (mRadiusRate * 500.0f);

    Vec3f vec24(0.0f, 0.0f, 0.0f);
    if (!MR::isNearZero(mOffsetLength)) {
        vec24 = mOffset * (1.0f / mOffsetLength);
    }

    if (mOffsetRate > 1.0f) {
        f32 dot = mAccelerate.dot(vec24);
        if (dot > 0.0f) {
            mAccelerate -= vec24 * dot;
        }

        mOffset = mOffset.scaleInline(1.0f / mOffsetRate);
        mOffsetRate = 1.0f;
    }
}

void SpinDriverOperateRing::updateControlPoint(const Vec3f& rVec) {
    if (MR::isStarPointerInScreen(PLAYER_1) && MR::calcStarPointerPosOnPlane(&mPointerPos, mCenter, rVec, PLAYER_1, false)) {
        mOperateState = 1;

        if (MR::isHeldFire(PLAYER_1)) {
            mOperateState = 2;
        }
    } else {
        mOperateState = 0;
    }
}

void SpinDriverOperateRing::updateDirection(const Vec3f& rNewDir) {
    mDirection = rNewDir * ::sDirectionRate + mDirection * (1.0f - ::sDirectionRate);
}

void SpinDriverOperateRing::resetVelocityAndTrans() {
    mDirection.zero();
    mAccelerate.zero();
    mOffset.zero();
    mOffsetLength = 0.0f;
    mOffsetRate = 0.0f;
}

void SpinDriverOperateRing::addAccelToOperatePlane(const Vec3f& rVec) {
    f32 dot = -rVec.dot(mOffset);
    mAccelerate += rVec.scaleInline(::sToPlaneRate * dot);
}

void SpinDriverOperateRing::addAccelOperate(Vec3f* pVec, const Vec3f& rVec) {
    Vec3f dpdVec;
    calcOperatePowerByPointer(&dpdVec);
    dpdVec.scale(2.0f);

    if (mOffsetRate > 0.5f) {
        Vec3f vec44(-mOffset / mOffsetLength);
        f32 dot = vec44.dot(dpdVec);
        f32 norm = MR::normalize(mOffsetRate, 0.5f, 1.0f);

        if (dot < 0.0f) {
            dpdVec -= vec44 * dot * norm;
        }
    }

    pVec->set(dpdVec);
    mAccelerate += dpdVec;
}

void SpinDriverOperateRing::addAccelToCenter() {
    f32 norm = MR::normalize(mOffsetRate, 0.1f, 1.0f);

    if (norm > 0.0001f) {
        f32 scalar = ((::sOperateAccel * norm) / mOffsetLength);
        mAccelerate -= mOffset * scalar;
    }
}

void SpinDriverOperateRing::attenuateVelocity() {
    mAccelerate.x *= 0.94f;
    mAccelerate.y *= 0.94f;
    mAccelerate.z *= 0.94f;
}

void SpinDriverOperateRing::calcOperatePowerByPointer(Vec3f* pVec) const {
    if (mOperateState == 2) {
        pVec->set(mPointerPos - mCenter - mOffset);

        Vec3f vec(mPointerPos - mCenter - mOffset);
        f32 scalar;
        MR::separateScalarAndDirection(&scalar, &vec, vec);

        if (scalar < 50.0f) {
            f32 scale = scalar / 50.0f;
            pVec->set(vec * scale);
        } else {
            pVec->set(vec);
        }
    } else {
        pVec->zero();
    }
}

void SpinDriverOperateRing::exeWait() {
}
