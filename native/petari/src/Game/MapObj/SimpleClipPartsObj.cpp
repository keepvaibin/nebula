#include "Game/MapObj/SimpleClipPartsObj.hpp"
#include "Game/LiveActor/HitSensor.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util/ActorSensorUtil.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

SimpleClipPartsObj::SimpleClipPartsObj(const char* pName) : MapObjActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< SimpleClipPartsObj >);
}

void SimpleClipPartsObj::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    info.setupRailMover();
    info.setupRotator();
    MapObjActorUtil::setupInitInfoSimpleMapObj(&info);
    MapObjActorUtil::setupInitInfoTypical(&info, mObjectName);
    MapObjActor::initialize(rIter, info);
    getSensor("body")->setType(ATYPE_CLIP_FIELD_MAP_PARTS);
}

void SimpleClipPartsObj::control() {
    MapObjActor::control();
    if (!MR::isValidSwitchA(this)) {
        return;
    }

    if (MR::isOnSwitchA(this)) {
        MR::validateCollisionParts(this);
    } else {
        MR::invalidateCollisionParts(this);
    }
}

void SimpleClipPartsObj::connectToScene(const MapObjActorInitInfo& rInfo) {
    if (rInfo.mConnectToScene) {
        MR::connectToClippedMapParts(this);
    }
}

// Keeps the base from entering the wait nerve.
void SimpleClipPartsObj::initCaseUseSwitchA(const MapObjActorInitInfo& /*rInfo*/) {
}

void SimpleClipPartsObj::initCaseUseSwitchB(const MapObjActorInitInfo& /*rInfo*/) {
    MR::listenStageSwitchOnOffB(this, MR::Functor< MapObjActor >(this, &MapObjActor::startMapPartsFunctions),
                                MR::Functor< MapObjActor >(this, &MapObjActor::endMapPartsFunctions));
    setStateWait();
}

void SimpleClipPartsObj::initCaseNoUseSwitchB(const MapObjActorInitInfo& /*rInfo*/) {
    MapObjActor::startMapPartsFunctions();
}
