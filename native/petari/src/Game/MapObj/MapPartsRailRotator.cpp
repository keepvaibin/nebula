#include "Game/MapObj/MapPartsRailRotator.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/MapPartsUtil.hpp"
#include "Game/Util/MathUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_updateHostRotateMtx_f1, "updateHostRotateMtx.f1", 180.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_updateInfo_f1, "updateInfo.f1", 0.01f);

namespace NrvMapPartsRailRotator {
    NEW_NERVE(HostTypeWait, MapPartsRailRotator, Wait);
    NEW_NERVE(HostTypeRotateAtPoint, MapPartsRailRotator, Rotate);
    NEW_NERVE(HostTypeRotateBetweenPoints, MapPartsRailRotator, Rotate);
    NEW_NERVE(HostTypeDone, MapPartsRailRotator, Done);
};  // namespace NrvMapPartsRailRotator

MapPartsRailRotator::MapPartsRailRotator(LiveActor* pHost)
    : MapPartsFunction(pHost, "レイル回転"), mRotateAxis(), mRotateType(), mRotateSpeed(), mTargetAngle(), mAngle(), mHostRotateMtx() {
    lumacore::setClass(this, lumacore::kClassId< MapPartsRailRotator >);
    mBaseRotateMtx.identity();
    mRotateMtx.identity();
}

void MapPartsRailRotator::init(const TableIter&) {
    initNerve(GET_NERVE(MapPartsRailRotator, HostTypeWait));
}

void MapPartsRailRotator::initWithRotateMtx(const TableIter& rIter, MtxPtr pMtx) {
    mHostRotateMtx = pMtx;
    init(rIter);
}

void MapPartsRailRotator::start() {
}

void MapPartsRailRotator::end() {
    mBaseRotateMtx.identity();
    mRotateMtx.identity();
    mAngle = 0.0f;
    setNerve(GET_NERVE(MapPartsRailRotator, HostTypeWait));
}

bool MapPartsRailRotator::isWorking() const {
    return 0.0f != mRotateSpeed && 0.0f < mTargetAngle;
}

bool MapPartsRailRotator::hasRotation(s32 point) const {
    f32 speed = -1.0f;
    MR::getMapPartsArgRailRotateSpeed(&speed, mHost, point);
    f32 angle = 0.0f;
    MR::getMapPartsArgRailRotateAngle(&angle, mHost, point);
    return 0.0f != speed && 0.0f < angle;
}

void MapPartsRailRotator::rotateAtPoint(s32 point) {
    updateInfo(point);

    if (!isWorking()) {
        setNerve(GET_NERVE(MapPartsRailRotator, HostTypeDone));
    } else {
        setNerve(GET_NERVE(MapPartsRailRotator, HostTypeRotateAtPoint));
    }
}

bool MapPartsRailRotator::hasRotationBetweenPoints(s32 point) const {
    if (!hasRotation(point)) {
        return false;
    }

    s32 type = 0;
    MR::getMapPartsArgRailRotateType(&type, mHost, point);
    return type == 1;
}

void MapPartsRailRotator::rotateBetweenPoints(s32 point, f32 time) {
    updateInfo(point);

    if (!isWorking()) {
        setNerve(GET_NERVE(MapPartsRailRotator, HostTypeDone));
    } else {
        mRotateSpeed = (mTargetAngle / time) * MR::sign(mRotateSpeed);
        setNerve(GET_NERVE(MapPartsRailRotator, HostTypeRotateBetweenPoints));
    }
}

void MapPartsRailRotator::updateHostRotateMtx() {
    if (mHostRotateMtx != nullptr) {
        NebulaMtxCopy(mHostRotateMtx, mBaseRotateMtx.toMtxPtr());
    } else if (mHost->getBaseMtx() != nullptr) {
        mBaseRotateMtx.set(mHost->getBaseMtx());
        mBaseRotateMtx.zeroTrans();
    } else {
        mBaseRotateMtx.setEuler(mHost->mRotation * (PI / cNum_updateHostRotateMtx_f1));
    }
}

void MapPartsRailRotator::updateInfo(s32 point) {
    s32 speedCalcType = -1;
    MR::getMapPartsArgSpeedCalcType(&speedCalcType, mHost, point);

    if (MR::isMapPartsRailSpeedCalcTypeTime(speedCalcType)) {
        s32 time = 0;
        MR::getMapPartsArgRailRotateTime(&time, mHost, point);
        f32 angle = 0.0f;
        MR::getMapPartsArgRailRotateAngle(&angle, mHost, point);
        // A time under one step turns the whole angle in one step.
        mRotateSpeed = time > 0 ? angle / time : angle;
    } else {
        f32 speed = -1.0f;
        MR::getMapPartsArgRailRotateSpeed(&speed, mHost, point);
        mRotateSpeed = cNum_updateInfo_f1 * speed;
    }

    f32 angle = -1.0f;
    MR::getMapPartsArgRailRotateAngle(&angle, mHost, point);
    angle *= getPlacementArgAngleFactor();
    mTargetAngle = angle;
    MR::getMapPartsArgRailRotateAxis(&mRotateAxis, mHost, point);
    MR::getMapPartsArgRailRotateType(&mRotateType, mHost, point);

    if (angle < 0.0f) {
        mRotateSpeed = 0.0f;
    }

    mAngle = 0.0f;
    updateHostRotateMtx();
}

bool MapPartsRailRotator::isReachedTargetAngle() const {
    return mTargetAngle <= MR::abs(mAngle);
}

void MapPartsRailRotator::calcRotateAxisDir(AxisType axis, Vec3f* pDir) const {
    switch (axis) {
    case AxisType_X:
        mBaseRotateMtx.getXDir(*pDir);
        break;
    case AxisType_Y:
        mBaseRotateMtx.getYDir(*pDir);
        break;
    case AxisType_Z:
        mBaseRotateMtx.getZDir(*pDir);
        break;
    }
}

void MapPartsRailRotator::updateRotateMtx(AxisType axis, f32 angle) {
    Vec3f direction;
    calcRotateAxisDir(axis, &direction);
    mRotateMtx.identity();
    mRotateMtx.makeRotate(direction, PI_180 * angle);
    mRotateMtx.concat(mRotateMtx, mBaseRotateMtx);
}

void MapPartsRailRotator::exeRotate() {
    if (isFirstStep()) {
        updateHostRotateMtx();
    }

    mAngle += mRotateSpeed;

    if (isReachedTargetAngle()) {
        updateRotateMtx(static_cast< AxisType >(mRotateAxis), mTargetAngle * MR::sign(mRotateSpeed));
        sendMsgToHost(ACTMES_MAPPARTS_END_ROTATE_AT_POINT);
        setNerve(GET_NERVE(MapPartsRailRotator, HostTypeDone));
    } else {
        updateRotateMtx(static_cast< AxisType >(mRotateAxis), mAngle);
    }
}
