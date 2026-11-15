#include "Game/MapObj/MapParts.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util.hpp"
#include <nebula_geometry.hpp>
#include "math_types.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    const char* cFollowJointName = "Move";
};  // namespace

MapParts::MapParts(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< MapParts >);
    mClipCenter.setZeroVec();
}

void MapParts::init(const TableIter& rIter) {
    if (MR::isConnectedWithRail(rIter)) {
        initRailRider(rIter);
    }

    MR::initDefaultPosNoRepeat(this, rIter);
    initModelAndCollision(rIter);
    connectToScene();
    MR::initMapPartsClipping(this, rIter, &mClipCenter, false);
    MR::joinToGroupArray(this, rIter, "MapParts", 0x40);
    initSound(4, false);
}

void MapParts::appear() {
    LiveActor::appear();
    MR::tryStartAllAnim(this, MR::getModelResName(this));
}

s32 MapParts::getSensorNumMax() const {
    return 0;
}

s32 MapParts::getMoveStartSignalTime() {
    return 50;
}

void MapParts::connectToScene() {
    MR::connectToSceneMapParts(this);
}

void MapParts::initModelAndCollision(const TableIter& rIter) {
    MR::FormattedName name;
    MR::getMapPartsObjectName(name, rIter);
    initModelManagerWithAnm(name.c_str(), nullptr, false);
    initHitSensor(2);
    Vec3f dist;
    Vec3f lerpVec;
    Vec3f sensor_offs(0.0f, 0.0f, 0.0f);
    u32 sensorNum = getSensorNumMax();
    HitSensor* sensor = MR::addHitSensorMapObj(this, "body", sensorNum, 100.0f, sensor_offs);
    if (MR::isExistJoint(this, ::cFollowJointName)) {
        MtxPtr jointMtx = MR::getJointMtx(this, ::cFollowJointName);
        MR::initCollisionParts(this, name.c_str(), sensor, jointMtx);
        MR::tryCreateCollisionAllOtherCategory(this, jointMtx, sensor, nullptr, nullptr, nullptr);
    } else {
        MR::initCollisionParts(this, name.c_str(), sensor, nullptr);
        MR::tryCreateCollisionAllOtherCategory(this, sensor, nullptr, nullptr, nullptr);
    }

    f32 sensorRange;
    if (MR::getModel(this)) {
        Box3f box;
        MR::calcModelBoundingBox(&box, this);
        dist.sub(box.f, box.i);
        sensorRange = 0.5f * dist.length();
        lerpVec.lerp(box.f, box.i, 0.5f);
        MR::setSensorOffset(this, "body", lerpVec - mPosition);
    } else {
        sensorRange = MR::getCollisionBoundingSphereRange(this);
    }

    sensor->mRadius = sensorRange;
}
