#include "Game/LiveActor/LiveActor.hpp"
#include "Game/MapObj/MapPartsSeesaw1AxisRotator.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/MapPartsUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/VectorUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_init_f1, "init.f1", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_start_f1, "start.f1", 1.05f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateVelocity_f1, "updateVelocity.f1", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRestoreForce_f1, "updateRestoreForce.f1", 0.01f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateRestoreForce_f2, "updateRestoreForce.f2", 0.1f);
NEBULA_TUNABLE_NAMED(f32, cNum_getDistanceFromRotAxis_f1, "getDistanceFromRotAxis.f1", 0.001f);
NEBULA_TUNABLE_NAMED(f32, cNum_addForceHipDrop_f1, "addForceHipDrop.f1", 0.1f);

namespace {
    NEBULA_TUNABLE(f32, sAngularVelocityFric, 0.99f);
    NEBULA_TUNABLE(f32, sAngularAccelHipDrop, 40.0f);
    NEBULA_TUNABLE(f32, sCollisionEfficiency, -0.5f);
    NEBULA_TUNABLE(f32, sAngularSpeedMin, 0.1f);
    NEBULA_TUNABLE(s32, sMoveStartFrame, 2);
    NEBULA_TUNABLE(f32, sInertiaConstDefault, 1000.0f);
};  // namespace

namespace NrvMapPartsSeesaw1AxisRotator {
    NEW_NERVE(HostTypeWait, MapPartsSeesaw1AxisRotator, Wait);
    NEW_NERVE(HostTypeMoveStart, MapPartsSeesaw1AxisRotator, Move);
    NEW_NERVE(HostTypeMove, MapPartsSeesaw1AxisRotator, Move);
    NEW_NERVE(HostTypeStay, MapPartsSeesaw1AxisRotator, Stay);
    NEW_NERVE(HostTypeHipDrop, MapPartsSeesaw1AxisRotator, HipDrop);
};  // namespace NrvMapPartsSeesaw1AxisRotator

MapPartsSeesaw1AxisRotator::MapPartsSeesaw1AxisRotator(LiveActor* pHost, const char* pMoveSound, f32 moveSoundSpeed)
    : MapPartsRotatorBase(pHost, "シーソー(1軸)"), mAngularSpeedMax(), mInertiaConst(::sInertiaConstDefault), mRotateAngle(), mRestoreForce(),
      mHipDrop(), mRotateAxis(0.0f, 0.0f, 1.0f), mAngularVelocity(), mAngularAccel(), mInitialUp(0.0f, 1.0f, 0.0f), mMoveSound(pMoveSound),
      mMoveSoundSpeed(moveSoundSpeed) {
    lumacore::setClass(this, lumacore::kClassId< MapPartsSeesaw1AxisRotator >);
    mRotateMtx.identity();
}

void MapPartsSeesaw1AxisRotator::init(const TableIter& rIter) {
    initNerve(GET_NERVE(MapPartsSeesaw1AxisRotator, HostTypeWait));
    MR::getMapPartsArgRotateSpeed(&mAngularSpeedMax, rIter);
    mAngularSpeedMax *= cNum_init_f1;
    MR::getMapPartsArgRotateAngle(&mRotateAngle, rIter);
    s32 inertia = 0;
    MR::getMapPartsArgRotateAccelType(&inertia, rIter);

    if (inertia > 0) {
        mInertiaConst = inertia;
    }

    s32 restoreForce = 0;
    MR::getMapPartsArgRotateStopTime(&restoreForce, rIter);
    mRestoreForce = restoreForce;

    Pos3f baseMtx;
    baseMtx.set(mHost->getBaseMtx());
    baseMtx.getYDir(mInitialUp);
    MR::normalize(&mInitialUp);
}

void MapPartsSeesaw1AxisRotator::start() {
    Pos3f rotateMtx;
    rotateMtx.identity();
    rotateMtx.setRotateDegree(mHost->mRotation);
    mRotateMtx.set(rotateMtx);

    Pos3f baseMtx(mHost->getBaseMtx());
    baseMtx.getZDir(mRotateAxis);
    MR::normalize(&mRotateAxis);

    f32 angle = 0.0f;
    calcRotatedAngle(&angle, mRotateMtx);

    if (isAngleLimited() && mRotateAngle < angle) {
        Pos3f correction;
        correction.identity();
        correction.setRotate(mRotateAxis, cNum_start_f1 * (PI_180 * (angle - mRotateAngle)));
        mRotateMtx.concat(correction);
    }

    mAngularVelocity = 0.0f;
    mAngularAccel = 0.0f;
    setNerve(GET_NERVE(MapPartsSeesaw1AxisRotator, HostTypeStay));
}

void MapPartsSeesaw1AxisRotator::end() {
    mAngularVelocity = 0.0f;
    mAngularAccel = 0.0f;
    setNerve(GET_NERVE(MapPartsSeesaw1AxisRotator, HostTypeWait));
}

bool MapPartsSeesaw1AxisRotator::receiveMsg(u32 msg) {
    if (msg != ACTMES_PLAYER_HIP_DROP_FLOOR || !MR::isOnPlayer(MR::getBodySensor(mHost))) {
        return false;
    }

    mHipDrop = true;
    return tryHipDrop();
}

void MapPartsSeesaw1AxisRotator::exeMove() {
    if (isNerve(GET_NERVE(MapPartsSeesaw1AxisRotator, HostTypeMoveStart)) && ::sAngularSpeedMin < MR::abs(mAngularVelocity)) {
        setNerve(GET_NERVE(MapPartsSeesaw1AxisRotator, HostTypeMove));
        return;
    }

    rotate();

    if (mMoveSound != nullptr && mMoveSoundSpeed < MR::abs(mAngularVelocity)) {
        MR::startLevelSound(mHost, mMoveSound);
    }
}

void MapPartsSeesaw1AxisRotator::exeStay() {
    if (isFirstStep()) {
        mAngularAccel = 0.0f;
    }

    if (MR::isOnPlayer(MR::getBodySensor(mHost)) || mAngularAccel != 0.0f) {
        setNerve(GET_NERVE(MapPartsSeesaw1AxisRotator, HostTypeMoveStart));
    }
}

void MapPartsSeesaw1AxisRotator::exeHipDrop() {
    if (isFirstStep()) {
        addForceHipDrop();
    }

    rotate();

    if (!mHipDrop) {
        setNerve(GET_NERVE(MapPartsSeesaw1AxisRotator, HostTypeMove));
    }

    mHipDrop = false;
}

void MapPartsSeesaw1AxisRotator::rotate() {
    Pos3f baseMtx(mHost->getBaseMtx());
    baseMtx.getZDir(mRotateAxis);
    mAngularVelocity += mAngularAccel;
    updateVelocity();

    if (mAngularVelocity == 0.0f && mAngularAccel == 0.0f) {
        setNerve(GET_NERVE(MapPartsSeesaw1AxisRotator, HostTypeStay));
        return;
    }

    mAngularAccel = 0.0f;

    if (isGoingToReachTargetAngle()) {
        mAngularVelocity *= ::sCollisionEfficiency;
        return;
    }

    Pos3f rotation;
    rotation.identity();
    rotation.setRotate(mRotateAxis, PI_180 * mAngularVelocity);
    rotation.concat(rotation, mRotateMtx);
    mRotateMtx.set(rotation);
    Vec3f angles;
    mRotateMtx.getEuler(angles);
    mHost->mRotation.set(angles * _180_PI);
}

void MapPartsSeesaw1AxisRotator::updateVelocity() {
    if (MR::isOnPlayer(MR::getBodySensor(mHost))) {
        Vec3f gravity;
        MR::calcGravityVector(mHost, &gravity, nullptr, 0);
        Vec3f offset(*MR::getPlayerPos() - mHost->mPosition);
        Vec3f torque;
        torque.cross(offset, gravity);
        f32 direction = MR::sign(torque.dot(mRotateAxis));
        mAngularVelocity += direction * (getDistanceFromRotAxis() / (cNum_updateVelocity_f1 * mInertiaConst));
    }

    if (!isNerve(GET_NERVE(MapPartsSeesaw1AxisRotator, HostTypeMoveStart)) && getStep() > ::sMoveStartFrame) {
        updateRestoreForce();
        mAngularVelocity *= ::sAngularVelocityFric;
    }

    clampAngularSpeed();
}

void MapPartsSeesaw1AxisRotator::updateRestoreForce() {
    Pos3f baseMtx(mHost->getBaseMtx());
    Vec3f up;
    baseMtx.getYDir(up);
    MR::normalize(&up);
    Vec3f torque;
    torque.cross(up, mInitialUp);
    f32 force = torque.dot(mRotateAxis);

    if (MR::isNearZero(force) && MR::abs(mAngularVelocity) <= ::sAngularSpeedMin) {
        mAngularVelocity = 0.0f;
        return;
    }

    f32 direction = MR::sign(force);
    f32 restoreForce = cNum_updateRestoreForce_f1 * mRestoreForce;
    mAngularVelocity += (direction * restoreForce) / (cNum_updateRestoreForce_f2 * mInertiaConst);
}

void MapPartsSeesaw1AxisRotator::clampAngularSpeed() {
    mAngularVelocity = MR::clamp(mAngularVelocity, -mAngularSpeedMax, mAngularSpeedMax);
}

f32 MapPartsSeesaw1AxisRotator::getDistanceFromRotAxis() const {
    Vec3f offset(*MR::getPlayerPos() - mHost->mPosition);
    Pos3f baseMtx(mHost->getBaseMtx());
    Vec3f axis;
    baseMtx.getXDir(axis);
    Vec3f projection;
    projection.scale(axis.dot(offset), axis);
    return cNum_getDistanceFromRotAxis_f1 * projection.length();
}

void MapPartsSeesaw1AxisRotator::addForceHipDrop() {
    Vec3f gravity;
    MR::calcGravityVector(mHost, &gravity, nullptr, 0);
    Vec3f offset(*MR::getPlayerPos() - mHost->mPosition);
    Vec3f torque;
    torque.cross(offset, gravity);
    f32 direction = MR::sign(torque.dot(mRotateAxis));
    f32 inertia = cNum_addForceHipDrop_f1 * mInertiaConst;
    mAngularVelocity += direction * ((::sAngularAccelHipDrop * getDistanceFromRotAxis()) / inertia);
}

bool MapPartsSeesaw1AxisRotator::isGoingToReachTargetAngle() const {
    if (!isAngleLimited()) {
        return false;
    }

    Pos3f rotation;
    rotation.identity();
    rotation.setRotate(mRotateAxis, PI_180 * mAngularVelocity);
    rotation.concat(rotation, mRotateMtx);
    f32 angle = 0.0f;
    calcRotatedAngle(&angle, rotation);
    return mRotateAngle <= angle;
}

void MapPartsSeesaw1AxisRotator::calcRotatedAngle(f32* pAngle, const Pos3f& rMtx) const {
    Vec3f up;
    rMtx.getYDir(up);
    MR::normalize(&up);
    Vec3f gravity;
    MR::calcGravityVector(mHost, &gravity, nullptr, 0);

    Vec3f gravityUp;
    if (!gravity.isZero()) {
        gravityUp.set(-gravity);
    } else {
        gravityUp.set< f32 >(0.0f, 1.0f, 0.0f);
    }

    MR::normalize(&gravityUp);
    f32 cosine = up.dot(gravityUp);

    if (MR::isNearZero(1.0f - cosine)) {
        cosine = 1.0f;
    }

    if (MR::isNearZero(cosine)) {
        cosine = 0.0f;
    }

    *pAngle = _180_PI * Angle::acosRadian(cosine);
}

bool MapPartsSeesaw1AxisRotator::tryHipDrop() {
    if (isNerve(GET_NERVE(MapPartsSeesaw1AxisRotator, HostTypeMove)) || isNerve(GET_NERVE(MapPartsSeesaw1AxisRotator, HostTypeMoveStart)) ||
        isNerve(GET_NERVE(MapPartsSeesaw1AxisRotator, HostTypeStay))) {
        setNerve(GET_NERVE(MapPartsSeesaw1AxisRotator, HostTypeHipDrop));
        return true;
    }

    return false;
}

bool MapPartsSeesaw1AxisRotator::isWorking() const {
    return true;
}
