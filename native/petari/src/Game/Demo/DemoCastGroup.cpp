#include "Game/Demo/DemoCastGroup.hpp"
#include "Game/LiveActor/LiveActorGroup.hpp"
#include "Game/Util/PlacementId.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/LumaCore.hpp"

DemoCastGroup::DemoCastGroup(const char* pName) : NameObj(pName), mInfo(nullptr), mGroup(nullptr) {
    lumacore::setClass(this, lumacore::kClassId< DemoCastGroup >);
}

bool DemoCastGroup::tryRegisterDemoActor(LiveActor* pActor, const TableIter& rIter, const PlacementId& rInfo) {
    if (!(*mInfo == rInfo)) {
        return false;
    }

    registerDemoActor(pActor, rIter);
    return true;
}

bool DemoCastGroup::tryRegisterDemoActor(LiveActor* pActor, const char* pName, const TableIter& rIter) {
    if (!MR::isEqualString(mName, pName)) {
        return false;
    }

    registerDemoActor(pActor, rIter);
    return true;
}

void DemoCastGroup::registerDemoActor(LiveActor* pActor, const TableIter& /* unused */) {
    mGroup->registerActor(pActor);
}

void DemoCastGroup::init(const TableIter& rIter) {
    setName(MR::getDemoName(rIter));
    mGroup = new LiveActorGroup("関連者保持", 0xC0);
    mGroup->initWithoutIter();
    mInfo = new PlacementId(MR::getDemoGroupLinkID(rIter), rIter);
}
