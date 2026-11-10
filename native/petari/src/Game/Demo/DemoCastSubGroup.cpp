#include "Game/Demo/DemoCastSubGroup.hpp"
#include "Game/Demo/DemoCastGroupHolder.hpp"
#include "Game/Demo/DemoFunction.hpp"
#include "Game/System/MessageHolder.hpp"
#include "Game/Util/PlacementId.hpp"
#include "Game/Util/Table.hpp"
#include "Game/System/LumaCore.hpp"

DemoCastSubGroup::DemoCastSubGroup(const char* pName) : DemoCastGroup(pName), mExecutor(nullptr) {
    lumacore::setClass(this, lumacore::kClassId< DemoCastSubGroup >);
}

void DemoCastSubGroup::init(const TableIter& rIter) {
    DemoCastGroup::init(rIter);
    DemoFunction::getDemoCastSubGroupHolder()->registerObj(this);
}

bool DemoCastSubGroup::tryRegisterDemoActor(LiveActor* pActor, const TableIter& rIter, const PlacementId& rInfo) {
    if (!DemoCastGroup::tryRegisterDemoActor(pActor, rIter, rInfo)) {
        return false;
    }

    if (mExecutor == nullptr) {
        mExecutor = DemoFunction::findDemoExecutor(mName);
        if (mExecutor == nullptr) {
            return false;
        }
    }

    mExecutor->registerDemoActor(pActor, rIter);
    return true;
}
