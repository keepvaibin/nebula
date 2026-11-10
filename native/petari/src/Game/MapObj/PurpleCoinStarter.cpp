#include "Game/MapObj/PurpleCoinStarter.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Map/StageSwitch.hpp"
#include "Game/MapObj/PurpleCoinHolder.hpp"
#include "Game/Util.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

PurpleCoinStarter::PurpleCoinStarter(const char* pName) : NameObj(pName), mHost(), mStageSwitchCtrl() {
    lumacore::setClass(this, lumacore::kClassId< PurpleCoinStarter >);
}

void PurpleCoinStarter::init(const TableIter& rIter) {
    mStageSwitchCtrl = MR::createStageSwitchCtrl(this, rIter);
    MR::listenNameObjStageSwitchOnAppear(this, mStageSwitchCtrl, MR::Functor(this, &PurpleCoinStarter::start));
    MR::createPurpleCoinHolder();
    MR::registPurpleCoinStarter(this);
}

void PurpleCoinStarter::setHost(PurpleCoinHolder* pHost) {
    mHost = pHost;
}

void PurpleCoinStarter::start() {
    mHost->start();
}
