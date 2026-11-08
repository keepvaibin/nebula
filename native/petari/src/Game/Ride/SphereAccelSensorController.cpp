#include "Game/Ride/SphereAccelSensorController.hpp"
#include "Game/Util/PlayerInputUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include <nebula_geometry.hpp>

#include "Game/System/NebulaInput.hpp"

namespace {
    // Tilt mapping per hand: the rest angle in the YZ plane, a dead zone, and the tilt that reaches full input
    // (the Y range applies to tilting back).
    const f32 cCoreBaseDegreeYZ = 10.0f;
    const f32 cCoreAccelDegreeMargin = 5.0f;
    const f32 cCoreAccelDegreeRange = 25.0f;
    const f32 cCoreAccelDegreeRangeY = 25.0f;
    const f32 cSubBaseDegreeYZ = 45.0f;
    const f32 cSubAccelDegreeMargin = 10.0f;
    const f32 cSubAccelDegreeRange = 40.0f;
    const f32 cSubAccelDegreeRangeY = 22.5f;

    // Signed angle from rV2 to rV1; both unit length.
    f32 calcSignedAngle(const Vec2f& rV1, const Vec2f& rV2) {
        const f32 orientation = rV1.y * rV2.x - rV1.x * rV2.y;
        f32 angle = MR::acos(rV1.dot(rV2));
        if (orientation < 0.0f) {
            angle = -angle;
        }

        return angle;
    }

    // Removes the dead zone and scales the rest of the tilt to [-1, 1] over the range.
    f32 mapTilt(f32 angle, f32 margin, f32 rangePositive, f32 rangeNegative) {
        if (MR::abs(angle) < margin) {
            return 0.0f;
        }

        if (angle > 0.0f) {
            return (angle - margin) / (rangePositive - margin);
        }

        return (angle + margin) / (rangeNegative - margin);
    }
}  // namespace

SphereAccelSensorController::SphereAccelSensorController() : mPad(Pad_Core) {
}

void SphereAccelSensorController::getPadAcceleration(Vec3f* pAccel) const {
    if (mPad == Pad_Core) {
        MR::getMainTilt(pAccel, PLAYER_1);
    } else {
        MR::getSubTilt(pAccel, PLAYER_1);
    }
}

bool SphereAccelSensorController::testBrake() const {
    if (mPad == Pad_Core) {
        return MR::isHeldJump(PLAYER_1);
    }

    return MR::isHeldCrouch(PLAYER_1) || MR::isHeldCamera(PLAYER_1);
}

f32 SphereAccelSensorController::calcJumpPower() const {
    return MR::isPressedJump(PLAYER_1);
}

bool SphereAccelSensorController::doBrake() const {
    return testBrake();
}

void SphereAccelSensorController::update(const Vec3f&) {
}

void SphereAccelSensorController::calcXY(f32* pX, f32* pY) {
    const bool isMain = isMainHand();
    const f32 accelMargin = MR::toRadian(isMain ? ::cCoreAccelDegreeMargin : ::cSubAccelDegreeMargin);
    const f32 accelRange = MR::toRadian(isMain ? ::cCoreAccelDegreeRange : ::cSubAccelDegreeRange);
    const f32 accelRangeY = MR::toRadian(isMain ? ::cCoreAccelDegreeRangeY : ::cSubAccelDegreeRangeY);
    const f32 baseAngleYZ = MR::toRadian(isMain ? ::cCoreBaseDegreeYZ : ::cSubBaseDegreeYZ);

    Vec3f padAccel;
    getPadAcceleration(&padAccel);

    f32 angleXY = 0.0f;
    Vec2f accelXY(padAccel.x, MR::abs(padAccel.y));
    if (!accelXY.isZero()) {
        MR::normalizeOrZero(&accelXY);
        angleXY = MR::asin(accelXY.x);
    }

    f32 angleYZ = 0.0f;
    Vec2f accelYZ(-padAccel.y, padAccel.z);
    if (!accelYZ.isZero()) {
        MR::normalizeOrZero(&accelYZ);
        angleYZ = ::calcSignedAngle(accelYZ, Vec2f(MR::cos(baseAngleYZ), MR::sin(baseAngleYZ)));
    }

    *pX = ::mapTilt(angleXY, accelMargin, accelRange, accelRange);
    *pY = ::mapTilt(angleYZ, accelMargin, accelRange, accelRangeY);
}
