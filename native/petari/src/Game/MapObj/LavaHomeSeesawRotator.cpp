#include "Game/MapObj/LavaHomeSeesawRotator.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/GravityUtil.hpp"
#include "Game/Util/MapPartsUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/System/LumaCore.hpp"

namespace {
    const f32 sAngularVelocityFric = 0.99f;
    const f32 sAngularAccelHipDrop = 40.0f;
    const f32 sCollisionEfficiency = -0.5f;
    const f32 sAngularSpeedMin = 0.01f;
    const s32 sMoveStartFrame = 2;
    const f32 sGroundOnAngularSpeed = 0.7f;
    const s32 sGroundOnDownTime = 5;
    const s32 sGroundOnReboundTime = 8;
    const f32 sInertiaConstDefault = 1000.0f;
};  // namespace

namespace NrvLavaHomeSeesawRotator {
    NEW_NERVE(HostTypeWait, LavaHomeSeesawRotator, Wait);
    NEW_NERVE(HostTypeMoveStart, LavaHomeSeesawRotator, MoveStart);
    NEW_NERVE(HostTypeMove, LavaHomeSeesawRotator, Move);
    NEW_NERVE(HostTypeStay, LavaHomeSeesawRotator, Stay);
    NEW_NERVE(HostTypeHipDrop, LavaHomeSeesawRotator, HipDrop);
};  // namespace NrvLavaHomeSeesawRotator

LavaHomeSeesawRotator::LavaHomeSeesawRotator(LiveActor* pHost, const char* pMoveSound, f32 moveSoundSpeed)
    : MapPartsRotatorBase(pHost, "シーソー(1軸)"), mAngularSpeedMax(), mInertia(::sInertiaConstDefault), mRotateAngle(), mRestoreForce(),
      mIsHipDropped(), mRotateAxis(0.0f, 0.0f, 1.0f), mAngularVelocity(), mInitialUp(0.0f, 1.0f, 0.0f), mMoveSound(pMoveSound),
      mMoveSoundSpeed(moveSoundSpeed) {
    lumacore::setClass(this, lumacore::kClassId< LavaHomeSeesawRotator >);
    mRotateMtx.identity();
    mRotateQuat.set(0.0f, 0.0f, 0.0f, 1.0f);
}

void LavaHomeSeesawRotator::init(const TableIter& rIter) {
    initNerve(GET_NERVE(LavaHomeSeesawRotator, HostTypeWait));

    MR::getMapPartsArgRotateSpeed(&mAngularSpeedMax, rIter);
    mAngularSpeedMax *= 0.01f;
    MR::getMapPartsArgRotateAngle(&mRotateAngle, rIter);
    s32 inertia = 0;
    MR::getMapPartsArgRotateAccelType(&inertia, rIter);

    if (inertia > 0) {
        mInertia = inertia;
    }

    s32 rotateStopTime = 0;
    MR::getMapPartsArgRotateStopTime(&rotateStopTime, rIter);
    mRestoreForce = rotateStopTime;

    Pos3f baseMtx(mHost->getBaseMtx());
    baseMtx.getYDir(mInitialUp);
    MR::normalize(&mInitialUp);
}

void LavaHomeSeesawRotator::start() {
    mRotateMtx.setRotateDegree(mHost->mRotation);
    updateRotateAxis();

    f32 angle = 0.0f;
    calcRotatedAngle(&angle, mRotateMtx);

    if (isAngleLimited() && mRotateAngle < angle) {
        Rot3f reboundMtx;
        reboundMtx.identity();
        reboundMtx.setRotate(mRotateAxis, 1.05f * MR::toRadian(angle - mRotateAngle));
        mRotateMtx.concat(reboundMtx);
    }

    Vec3f euler;
    mRotateMtx.getEulerXYZ(euler);
    mRotateQuat.setEuler(euler);

    mAngularVelocity = ::sAngularSpeedMin;
    setNerve(GET_NERVE(LavaHomeSeesawRotator, HostTypeStay));
}

void LavaHomeSeesawRotator::end() {
    setNerve(GET_NERVE(LavaHomeSeesawRotator, HostTypeWait));
}

bool LavaHomeSeesawRotator::receiveMsg(u32 msg) {
    if (!MR::isMsgPlayerHipDropFloor(msg) || !MR::isOnPlayer(MR::getBodySensor(mHost))) {
        return false;
    }

    mIsHipDropped = true;
    return tryHipDrop();
}

void LavaHomeSeesawRotator::exeWait() {
}

void LavaHomeSeesawRotator::exeMoveStart() {
    if (isFirstStep()) {
        updateRotateAxis();
        MR::shakeCameraWeak();
        MR::startSound(mHost, "SE_OJ_LAVA_FLOATER_ON_METAL");
    }

    if (isStep(::sGroundOnReboundTime)) {
        setNerve(GET_NERVE(LavaHomeSeesawRotator, HostTypeMove));
    } else {
        updateRotateMtx();
        soundMoveSE();
    }
}

void LavaHomeSeesawRotator::exeMove() {
    updateRotateMtx();
    soundMoveSE();
}

void LavaHomeSeesawRotator::exeStay() {
    if (MR::isOnPlayer(MR::getBodySensor(mHost))) {
        setNerve(GET_NERVE(LavaHomeSeesawRotator, HostTypeMoveStart));
    } else {
        updateRotateMtx();
        soundMoveSE();
    }
}

// receiveMsg sets mIsHipDropped each step the player keeps pounding.
void LavaHomeSeesawRotator::exeHipDrop() {
    if (isFirstStep()) {
        addForceHipDrop();
    }

    updateRotateMtx();

    if (!mIsHipDropped) {
        setNerve(GET_NERVE(LavaHomeSeesawRotator, HostTypeMove));
    }

    mIsHipDropped = false;
}

void LavaHomeSeesawRotator::updateRotateMtx() {
    updateVelocity();

    if (tryStay() || tryRebound()) {
        return;
    }

    // Landing: a dip with the spin, then a rebound against it.
    f32 deltaAngle = mAngularVelocity;
    if (isNerve(GET_NERVE(LavaHomeSeesawRotator, HostTypeMoveStart))) {
        if (getStep() <= ::sGroundOnDownTime) {
            deltaAngle = ::sGroundOnAngularSpeed * MR::sign(mAngularVelocity);
        } else if (getStep() <= ::sGroundOnReboundTime) {
            deltaAngle = ::sGroundOnAngularSpeed * -MR::sign(mAngularVelocity);
        }
    }

    Quat4f rotate;
    rotate.setRotate(mRotateAxis, MR::toRadian(deltaAngle));

    mRotateQuat.mult(rotate);
    mRotateQuat.normalize();
    mRotateMtx.setQuat(mRotateQuat);
}

void LavaHomeSeesawRotator::updateVelocity() {
    if (MR::isOnPlayer(MR::getBodySensor(mHost))) {
        f32 direction = calcPlayerTorqueSign();
        mAngularVelocity += direction * (getDistanceFromRotAxis() / (0.1f * mInertia));
    }

    if (!isNerve(GET_NERVE(LavaHomeSeesawRotator, HostTypeMoveStart)) && getStep() > ::sMoveStartFrame) {
        updateRestoreForce();

        if (!isNerve(GET_NERVE(LavaHomeSeesawRotator, HostTypeStay))) {
            mAngularVelocity *= ::sAngularVelocityFric;
        }
    }

    clampAngularSpeed();
}

void LavaHomeSeesawRotator::updateRestoreForce() {
    f32 direction = MR::sign(calcTiltFromRest());
    f32 restoreForce = 0.01f * mRestoreForce;
    mAngularVelocity += (direction * restoreForce) / (0.1f * mInertia);
}

void LavaHomeSeesawRotator::updateRotateAxis() {
    Pos3f baseMtx(mHost->getBaseMtx());
    baseMtx.getZDir(mRotateAxis);
    MR::normalize(&mRotateAxis);
}

void LavaHomeSeesawRotator::clampAngularSpeed() {
    mAngularVelocity = MR::clamp(mAngularVelocity, -mAngularSpeedMax, mAngularSpeedMax);
}

f32 LavaHomeSeesawRotator::getDistanceFromRotAxis() const {
    Vec3f offset(*MR::getPlayerPos() - mHost->mPosition);

    Pos3f baseMtx(mHost->getBaseMtx());
    Vec3f axis;
    baseMtx.getXDir(axis);

    Vec3f projection;
    projection.scale(axis.dot(offset), axis);
    return 0.001f * projection.length();
}

// The side of the axis the player stands on, as the sign of the torque gravity gives.
f32 LavaHomeSeesawRotator::calcPlayerTorqueSign() const {
    Vec3f gravity;
    MR::calcGravityVector(mHost, &gravity, nullptr, 0);

    Vec3f offset(*MR::getPlayerPos() - mHost->mPosition);
    Vec3f torque;
    torque.cross(offset, gravity);
    return MR::sign(torque.dot(mRotateAxis));
}

// Signed about the rotate axis; zero when the host's up matches the rest up.
f32 LavaHomeSeesawRotator::calcTiltFromRest() const {
    Pos3f baseMtx(mHost->getBaseMtx());
    Vec3f up;
    baseMtx.getYDir(up);
    MR::normalize(&up);

    Vec3f torque;
    torque.cross(up, mInitialUp);
    return torque.dot(mRotateAxis);
}

void LavaHomeSeesawRotator::addForceHipDrop() {
    f32 direction = calcPlayerTorqueSign();
    f32 inertia = 0.1f * mInertia;
    mAngularVelocity += direction * (::sAngularAccelHipDrop * getDistanceFromRotAxis() / inertia);
}

bool LavaHomeSeesawRotator::isAngleLimited() const {
    return 0.0f < mRotateAngle;
}

bool LavaHomeSeesawRotator::isGoingToReachTargetAngle() const {
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

void LavaHomeSeesawRotator::calcRotatedAngle(f32* pAngle, const Pos3f& rMtx) const {
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

    *pAngle = _180_PI * MR::acos(cosine);
}

bool LavaHomeSeesawRotator::tryHipDrop() {
    if (isNerve(GET_NERVE(LavaHomeSeesawRotator, HostTypeMove)) || isNerve(GET_NERVE(LavaHomeSeesawRotator, HostTypeMoveStart)) ||
        isNerve(GET_NERVE(LavaHomeSeesawRotator, HostTypeStay))) {
        setNerve(GET_NERVE(LavaHomeSeesawRotator, HostTypeHipDrop));
        return true;
    }

    return false;
}

bool LavaHomeSeesawRotator::tryStay() {
    if (isNerve(GET_NERVE(LavaHomeSeesawRotator, HostTypeStay)) || isNerve(GET_NERVE(LavaHomeSeesawRotator, HostTypeMoveStart))) {
        return false;
    }

    if (MR::abs(mAngularVelocity) < ::sAngularSpeedMin && MR::isNearZero(calcTiltFromRest())) {
        setNerve(GET_NERVE(LavaHomeSeesawRotator, HostTypeStay));
        return true;
    }

    return false;
}

bool LavaHomeSeesawRotator::tryRebound() {
    if (isGoingToReachTargetAngle()) {
        mAngularVelocity *= ::sCollisionEfficiency;
        return true;
    }

    return false;
}

void LavaHomeSeesawRotator::soundMoveSE() {
    if (mMoveSound != nullptr && mMoveSoundSpeed < MR::abs(mAngularVelocity)) {
        MR::startLevelSound(mHost, mMoveSound);
    }
}
