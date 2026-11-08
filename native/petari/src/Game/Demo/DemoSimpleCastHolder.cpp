#include "Game/Demo/DemoSimpleCastHolder.hpp"
#include "Game/LiveActor/LiveActor.hpp"
#include "Game/Util/ObjUtil.hpp"

DemoSimpleCastHolder::DemoSimpleCastHolder(s32 liveActorCapacity, s32 layoutActorCapacity, s32 nameObjCapacity) {
    mLiveActors.init(liveActorCapacity);
    mLayoutActors.init(layoutActorCapacity);
    mNameObjs.init(nameObjCapacity);
}

void DemoSimpleCastHolder::registerActor(LiveActor* pActor) {
    mLiveActors.push_back(pActor);
}

void DemoSimpleCastHolder::registerActor(LayoutActor* pActor) {
    mLayoutActors.push_back(pActor);
}

void DemoSimpleCastHolder::registerNameObj(NameObj* pNameObj) {
    mNameObjs.push_back(pNameObj);
}

void DemoSimpleCastHolder::movementOnAllCasts() {
    for (LiveActor** pActor = mLiveActors.begin(); pActor != mLiveActors.end(); pActor++) {
        MR::requestMovementOn(*pActor);
    }

    for (LayoutActor** pActor = mLayoutActors.begin(); pActor != mLayoutActors.end(); pActor++) {
        MR::requestMovementOn(*pActor);
    }

    for (NameObj** pNameObj = mNameObjs.begin(); pNameObj != mNameObjs.end(); pNameObj++) {
        MR::requestMovementOn(*pNameObj);
    }
}