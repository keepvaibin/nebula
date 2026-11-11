#include "Game/LiveActor/ClippingDirector.hpp"
#include "Game/LiveActor/ClippingActorHolder.hpp"
#include "Game/LiveActor/ClippingGroupHolder.hpp"
#include "Game/LiveActor/ClippingJudge.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/System/LumaCore.hpp"

ClippingDirector::ClippingDirector() : NameObj("クリッピング指揮"), mJudge(nullptr), mActorHolder(nullptr), mGroupHolder(nullptr) {
    lumacore::setClass(this, lumacore::kClassId< ClippingDirector >);
    mJudge = new ClippingJudge("クリッピング判定者");
    mJudge->initWithoutIter();

    mActorHolder = new ClippingActorHolder();
    mActorHolder->initWithoutIter();

    mGroupHolder = new ClippingGroupHolder();
    mGroupHolder->initWithoutIter();

    MR::connectToScene(this, MR::MovementType_ClippingDirector, MR::CalcAnimType_None, MR::DrawBufferType_None, MR::DrawType_None);
}

void ClippingDirector::endInitActorSystemInfo() {
    mActorHolder->initViewGroupTable();
}

void ClippingDirector::movement() {
    mJudge->movement();
    mActorHolder->movement();
    mGroupHolder->movement();
}

void ClippingDirector::registerActor(LiveActor* pActor) {
    mActorHolder->registerActor(pActor);
}

void ClippingDirector::initActorSystemInfo(LiveActor* pActor, const TableIter& rIter) {
    mActorHolder->initSystemInfo(pActor, rIter);
}

void ClippingDirector::joinToGroupClipping(LiveActor* pActor, const TableIter& rIter, int capacity) {
    s32 clippingID = -1;
    MR::getPlacementClippingGroupID(rIter, &clippingID);

    if (clippingID >= 0) {
        ClippingActorInfo* inf = mActorHolder->startGroupClipping(pActor, rIter);
        mGroupHolder->createAndAdd(inf, rIter, capacity);
    }
}

void ClippingDirector::entryLodCtrl(LodCtrl* pLod, const TableIter& rIter) {
    mActorHolder->entryLodCtrl(pLod, rIter);
}

namespace MR {
    ClippingDirector* getClippingDirector() {
        return MR::getSceneObj< ClippingDirector >(SceneObj_ClippingDirector);
    }

    void addToClippingTarget(LiveActor* pActor) {
        getClippingDirector()->mActorHolder->addToClippingTarget(pActor);
    }

    void removeFromClippingTarget(LiveActor* pActor) {
        getClippingDirector()->mActorHolder->removeFromClippingTarget(pActor);
    }
};  // namespace MR
