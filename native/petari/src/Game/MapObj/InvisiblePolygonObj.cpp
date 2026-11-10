#include "Game/MapObj/InvisiblePolygonObj.hpp"
#include "Game/Util/ActorMovementUtil.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

InvisiblePolygonObj::InvisiblePolygonObj(const char* pName) : LiveActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< InvisiblePolygonObj >);
    mBaseMtx.identity();
}

void InvisiblePolygonObj::init(const TableIter& rIter) {
    MR::initDefaultPos(this, rIter);

    initCollision(rIter);

    MR::connectToSceneMapObjMovement(this);
    MR::setClippingTypeSphere(this, MR::getCollisionBoundingSphereRange(this));

    if (MR::useStageSwitchReadAppear(this, rIter)) {
        MR::syncStageSwitchAppear(this);
        makeActorDead();
    } else {
        makeActorAppeared();
    }
}

void InvisiblePolygonObj::initBaseMtx() {
    if (0.0f == mRotation.x && 0.0f == mRotation.z) {
        MR::makeMtxTransRotateY(mBaseMtx.toMtxPtr(), this);
    } else {
        MR::makeMtxTR(mBaseMtx.toMtxPtr(), this);
    }
}

void InvisiblePolygonObj::initCollision(const TableIter& rIter) {
    initBaseMtx();

    const char* pName = nullptr;
    MR::getObjectName(&pName, rIter);

    ResourceHolder* resource = MR::createAndAddResourceHolder(pName);

    initHitSensor(1);

    HitSensor* sensor = MR::addBodyMessageSensorMapObj(this);

    MR::initCollisionPartsFromResourceHolder(this, pName, sensor, resource, nullptr);
}
