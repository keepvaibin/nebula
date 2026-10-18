#include "Game/LiveActor/ClippingDirector.hpp"
#include "Game/LiveActor/ClippingActorHolder.hpp"
#include "Game/LiveActor/ClippingGroupHolder.hpp"
#include "Game/LiveActor/ClippingJudge.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Util/JMapUtil.hpp"
#include "Game/Util/ObjUtil.hpp"

ClippingDirector::ClippingDirector() : NameObj("\203\116\203\212\203\142\203\163\203\223\203\117\216\167\212\366" /* クリッピング指揮 */), mJudge(nullptr), mActorHolder(nullptr), mGroupHolder(nullptr) {
    mJudge = new ClippingJudge("\203\116\203\212\203\142\203\163\203\223\203\117\224\273\222\350\216\322" /* クリッピング判定者 */);
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

void ClippingDirector::initActorSystemInfo(LiveActor* pActor, const JMapInfoIter& rIter) {
    mActorHolder->initSystemInfo(pActor, rIter);
}

void ClippingDirector::joinToGroupClipping(LiveActor* pActor, const JMapInfoIter& rIter, int a3) {
    s32 clippingID = -1;
    MR::getJMapInfoClippingGroupID(rIter, &clippingID);

    if (clippingID >= 0) {
        ClippingActorInfo* inf = mActorHolder->startGroupClipping(pActor, rIter);
        mGroupHolder->createAndAdd(inf, rIter, a3);
    }
}

void ClippingDirector::entryLodCtrl(LodCtrl* pLod, const JMapInfoIter& rIter) {
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
