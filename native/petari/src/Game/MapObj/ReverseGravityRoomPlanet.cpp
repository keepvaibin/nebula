#include "Game/MapObj/ReverseGravityRoomPlanet.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/MapObjActorInitInfo.hpp"
#include "Game/Util.hpp"
#include "Game/System/LumaCore.hpp"

namespace NrvReverseGravityRoomPlanet {
    NEW_NERVE(ReverseGravityRoomPlanetNrvDownStart, ReverseGravityRoomPlanet, DownStart);
    NEW_NERVE(ReverseGravityRoomPlanetNrvDownWait, ReverseGravityRoomPlanet, DownWait);
    NEW_NERVE(ReverseGravityRoomPlanetNrvUpStart, ReverseGravityRoomPlanet, UpStart);
    NEW_NERVE(ReverseGravityRoomPlanetNrvUpWait, ReverseGravityRoomPlanet, UpWait);
};  // namespace NrvReverseGravityRoomPlanet

ReverseGravityRoomPlanet::ReverseGravityRoomPlanet(const char* pName) : MapObjActor(pName) {
    lumacore::setClass(this, lumacore::kClassId< ReverseGravityRoomPlanet >);
}

void ReverseGravityRoomPlanet::init(const TableIter& rIter) {
    MapObjActor::init(rIter);
    MapObjActorInitInfo info;
    info.setupDefaultPos();
    info.setupConnectToScene();
    info.setupEffect(nullptr);
    info.setupFarClipping(-1.0f);
    info.setupNerve(GET_NERVE(ReverseGravityRoomPlanet, ReverseGravityRoomPlanetNrvDownWait));
    initialize(rIter, info);
}

void ReverseGravityRoomPlanet::exeDownStart() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "ChangeRed");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(ReverseGravityRoomPlanet, ReverseGravityRoomPlanetNrvDownWait));
    }
}

void ReverseGravityRoomPlanet::exeDownWait() {
}

void ReverseGravityRoomPlanet::exeUpStart() {
    if (MR::isFirstStep(this)) {
        MR::startBck(this, "ChangeBlue");
    }

    if (MR::isBckStopped(this)) {
        setNerve(GET_NERVE(ReverseGravityRoomPlanet, ReverseGravityRoomPlanetNrvUpWait));
    }
}

void ReverseGravityRoomPlanet::exeUpWait() {
}

void ReverseGravityRoomPlanet::initCaseUseSwitchA(const MapObjActorInitInfo& /*rInfo*/) {
    MR::listenStageSwitchOnOffA(this, MR::Functor(this, &ReverseGravityRoomPlanet::startSwitchOn),
                                MR::Functor(this, &ReverseGravityRoomPlanet::startSwitchOff));
}

void ReverseGravityRoomPlanet::startSwitchOn() {
    setNerve(GET_NERVE(ReverseGravityRoomPlanet, ReverseGravityRoomPlanetNrvUpStart));
}

void ReverseGravityRoomPlanet::startSwitchOff() {
    setNerve(GET_NERVE(ReverseGravityRoomPlanet, ReverseGravityRoomPlanetNrvDownStart));
}
