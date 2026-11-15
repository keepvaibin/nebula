#include "Game/MapObj/SpinDriverOperateRing.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/StarPointerUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sOperateAccel = 0.5f;
    const f32 sToPlaneRate = 0.05f;
    const f32 sDirectionRate = 0.1f;
    // Offset at radius rate 1.
    const f32 sMaxOffset = 500.0f;
    const f32 sOperatePowerScale = 2.0f;
    // Pointer distance below which the pull weakens linearly.
    const f32 sOperateFullPowerDist = 50.0f;
    // Past this offset rate, pull away from the centre fades out.
    const f32 sOperateLimitStartRate = 0.5f;
    const f32 sToCenterStartRate = 0.1f;
    const f32 sToCenterMinRate = 0.0001f;
    const f32 sAttenuate = 0.94f;

    // SpinDriverOperateRing::mOperateState.
    enum OperateState {
        OperateState_None,
        // The pointer is on the plane.
        OperateState_Point,
        // ... and A is held: the ring follows it.
        OperateState_Hold,
    };
}  // namespace

SpinDriverOperateRing::SpinDriverOperateRing(const char* pName)
    : LiveActor(pName), mCenter(0, 0, 0), mOffset(0, 0, 0), mAccelerate(0, 0, 0), mDirection(0, 0, 0), mPointerPos(0.0f, 0.0f, 0.0f), mRadiusRate(),
      mOffsetLength(), mOffsetRate(), mOperateState(OperateState_None) {
    lumacore::setClass(this, lumacore::kClassId< SpinDriverOperateRing >);
}

void SpinDriverOperateRing::init(const TableIter& /*rIter*/) {
    makeActorAppeared();
    MR::invalidateClipping(this);
}

void SpinDriverOperateRing::setRadiusRate(f32 rate) {
    mRadiusRate = MR::normalize(rate, 0.0f, 1.0f);
}

void SpinDriverOperateRing::reset() {
    resetVelocityAndTrans();
}

void SpinDriverOperateRing::update(const Vec3f& rCenter, const Vec3f& rShootDir) {
    mCenter.set(rCenter);

    if (mRadiusRate <= 0.0f) {
        resetVelocityAndTrans();
        return;
    }

    updateControlPoint(rShootDir);
    addAccelToOperatePlane(rShootDir);

    Vec3f operateAccel;
    addAccelOperate(&operateAccel);

    addAccelToCenter();
    attenuateVelocity();

    updateDirection(operateAccel);
    updatePosition();
}

// The offset stays inside the ring; at the edge the outward velocity is dropped.
void SpinDriverOperateRing::updatePosition() {
    mOffset += mAccelerate;
    mOffsetLength = mOffset.length();
    mOffsetRate = mOffsetLength / (mRadiusRate * ::sMaxOffset);

    Vec3f offsetDir(0.0f, 0.0f, 0.0f);
    if (!MR::isNearZero(mOffsetLength)) {
        offsetDir = mOffset * (1.0f / mOffsetLength);
    }

    if (mOffsetRate > 1.0f) {
        f32 outward = mAccelerate.dot(offsetDir);
        if (outward > 0.0f) {
            mAccelerate -= offsetDir * outward;
        }

        mOffset = mOffset.scaleInline(1.0f / mOffsetRate);
        mOffsetRate = 1.0f;
    }
}

void SpinDriverOperateRing::updateControlPoint(const Vec3f& rShootDir) {
    if (!MR::isStarPointerInScreen(PLAYER_1) || !MR::calcStarPointerPosOnPlane(&mPointerPos, mCenter, rShootDir, PLAYER_1, false)) {
        mOperateState = OperateState_None;
        return;
    }

    mOperateState = MR::isHeldFire(PLAYER_1) ? OperateState_Hold : OperateState_Point;
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

void SpinDriverOperateRing::addAccelToOperatePlane(const Vec3f& rShootDir) {
    f32 dot = -rShootDir.dot(mOffset);
    mAccelerate += rShootDir.scaleInline(::sToPlaneRate * dot);
}

void SpinDriverOperateRing::addAccelOperate(Vec3f* pAccel) {
    Vec3f accel;
    calcOperatePowerByPointer(&accel);
    accel.scale(::sOperatePowerScale);

    if (mOffsetRate > ::sOperateLimitStartRate) {
        Vec3f toCenter(-mOffset / mOffsetLength);
        f32 dot = toCenter.dot(accel);
        f32 limitRate = MR::normalize(mOffsetRate, ::sOperateLimitStartRate, 1.0f);

        if (dot < 0.0f) {
            accel -= toCenter * dot * limitRate;
        }
    }

    pAccel->set(accel);
    mAccelerate += accel;
}

void SpinDriverOperateRing::addAccelToCenter() {
    f32 rate = MR::normalize(mOffsetRate, ::sToCenterStartRate, 1.0f);

    if (rate > ::sToCenterMinRate) {
        f32 scalar = ((::sOperateAccel * rate) / mOffsetLength);
        mAccelerate -= mOffset * scalar;
    }
}

void SpinDriverOperateRing::attenuateVelocity() {
    mAccelerate.x *= ::sAttenuate;
    mAccelerate.y *= ::sAttenuate;
    mAccelerate.z *= ::sAttenuate;
}

void SpinDriverOperateRing::calcOperatePowerByPointer(Vec3f* pPower) const {
    if (mOperateState != OperateState_Hold) {
        pPower->zero();
        return;
    }

    Vec3f dir(mPointerPos - mCenter - mOffset);
    f32 dist;
    MR::separateScalarAndDirection(&dist, &dir, dir);

    if (dist < ::sOperateFullPowerDist) {
        pPower->set(dir * (dist / ::sOperateFullPowerDist));
    } else {
        pPower->set(dir);
    }
}
