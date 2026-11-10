#include "Game/LiveActor/AllLiveActorGroup.hpp"
#include "Game/LiveActor/ClippingDirector.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/LumaCore.hpp"

AllLiveActorGroup::AllLiveActorGroup() : LiveActorGroup("AllLiveActorGroup", 2560), mActorNum() {
    lumacore::setClass(this, lumacore::kClassId< AllLiveActorGroup >);
}

void AllLiveActorGroup::startInitActorSystemInfo() {
    mActorNum = getObjNum();
}

// The actors registered since the last call.
void AllLiveActorGroup::initActorSystemInfo(const TableIter& rIter) {
    for (; mActorNum < getObjNum(); mActorNum++) {
        MR::getClippingDirector()->initActorSystemInfo(getActor(mActorNum), rIter);
    }
}

void AllLiveActorGroup::endInitActorSystemInfo() {
    MR::getClippingDirector()->endInitActorSystemInfo();
}

namespace MR {
    AllLiveActorGroup* getAllLiveActorGroup() {
        return MR::getSceneObj< AllLiveActorGroup >(SceneObj_AllLiveActorGroup);
    }

    void startInitLiveActorSystemInfo() {
        getAllLiveActorGroup()->startInitActorSystemInfo();
    }

    void initLiveActorSystemInfo(const TableIter& rIter) {
        getAllLiveActorGroup()->initActorSystemInfo(rIter);
    }

    void endInitLiveActorSystemInfo() {
        getAllLiveActorGroup()->endInitActorSystemInfo();
    }
};  // namespace MR
