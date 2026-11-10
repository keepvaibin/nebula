#include "Game/NPC/CollectTico.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/NPC/StrayTico.hpp"
#include "Game/Util/ActorSwitchUtil.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/EffectUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include <nebula_geometry.hpp>
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_exeCompleteDemo_f1, "exeCompleteDemo.f1", 300.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_exeCompleteDemo_f2, "exeCompleteDemo.f2", 360.0f);

// The step counts of this file's functions, as tunables: "<file>.<function>.<k>".
NEBULA_TUNABLE_NAMED(s32, cStep_exeFlash_1, "exeFlash.1", 10);

namespace NrvCollectTico {
    NEW_NERVE(CollectTicoNrvWait, CollectTico, Wait);
    NEW_NERVE(CollectTicoNrvTryStartDemo, CollectTico, TryStartDemo);
    NEW_NERVE(CollectTicoNrvCompleteDemo, CollectTico, CompleteDemo);
    NEW_NERVE(CollectTicoNrvFlash, CollectTico, Flash);
    NEW_NERVE(CollectTicoNrvAppearPowerStar, CollectTico, AppearPowerStar);
};  // namespace NrvCollectTico

CollectTico::CollectTico(const char* pName) : LiveActor(pName) {
    mStrayTicos = nullptr;
    mTicoNum = 0;
    mIsDemoCast = 0;
}

void CollectTico::init(const TableIter& rIter) {
    MR::connectToSceneNpcMovement(this);
    mTicoNum = MR::getChildObjNum(rIter);
    mStrayTicos = new StrayTico*[mTicoNum];
    for (s32 i = 0; i < mTicoNum; i++) {
        mStrayTicos[i] = new StrayTico("はぐれチコ", this);
        MR::initChildObj(mStrayTicos[i], rIter, i);
    }

    initEffectKeeper(0, "CollectTico");
    initSound(2, false);
    initNerve(GET_NERVE(CollectTico, CollectTicoNrvWait));
    if (MR::tryRegisterDemoCast(this, rIter)) {
        MR::registerDemoActionFunctor(this, MR::Functor(this, &CollectTico::startAppearPowerStar), "集めチコスター出現");
        mIsDemoCast = 1;
    }

    MR::useStageSwitchWriteA(this, rIter);
    MR::declarePowerStar(this);
    MR::invalidateClipping(this);
    makeActorAppeared();
}

void CollectTico::exeWait() {
    bool needsDemo = true;
    for (s32 i = 0; i < mTicoNum; i++) {
        if (!mStrayTicos[i]->isRescued()) {
            needsDemo = false;
            break;
        }
    }

    if (needsDemo && !MR::isPlayerDead()) {
        MR::requestStartDemo(this, "チコ集めコンプリート", GET_NERVE(CollectTico, CollectTicoNrvCompleteDemo),
                             GET_NERVE(CollectTico, CollectTicoNrvTryStartDemo));
    }
}

void CollectTico::exeCompleteDemo() {
    if (MR::isFirstStep(this)) {
        mPosition.set(*MR::getPlayerPos());
        MR::calcGravity(this);

        mStarPos.scaleAdd(cNum_exeCompleteDemo_f1, -mGravity, mPosition);
        mPosition.set(mStarPos);

        for (s32 i = 0; i < mTicoNum; i++) {
            MR::requestMovementOn(mStrayTicos[i]);
            mStrayTicos[i]->requestCompleteDemo(mStarPos, mGravity, cNum_exeCompleteDemo_f2 * ((f32)i / mTicoNum));
        }
    }

    MR::startLevelSound(this, "SE_SM_LV_STRAYTICO_COMPLETE");
    bool needFlash = true;
    for (s32 i = 0; i < mTicoNum; i++) {
        if (!mStrayTicos[i]->isCompleteDemoEnd()) {
            needFlash = false;
            break;
        }
    }

    if (needFlash) {
        setNerve(GET_NERVE(CollectTico, CollectTicoNrvFlash));
    }
}

void CollectTico::exeFlash() {
    if (MR::isFirstStep(this)) {
        MR::emitEffect(this, "CollectTicoLight");
        MR::startSound(this, "SE_SM_STRAYTICO_FUSION");
    }

    if (MR::isStep(this, cStep_exeFlash_1)) {
        for (s32 i = 0; i < mTicoNum; i++) {
            mStrayTicos[i]->kill();
        }
    }

    if (MR::isStep(this, NebulaFrameClock::stepsOfSeconds(1))) {
        if (!mIsDemoCast) {
            MR::requestAppearPowerStar(this, mStarPos);
            setNerve(GET_NERVE(CollectTico, CollectTicoNrvAppearPowerStar));
        }

        MR::endDemo(this, "チコ集めコンプリート");
        if (MR::isValidSwitchA(this)) {
            MR::onSwitchA(this);
        }
    }
}

void CollectTico::exeAppearPowerStar() {
    if (MR::isFirstStep(this)) {
        for (s32 i = 0; i < mTicoNum; i++) {
            mStrayTicos[i]->noticeEnd();
        }

        kill();
    }
}

s32 CollectTico::calcNoRescuedCount() const {
    s32 count = 0;
    for (s32 i = 0; i < mTicoNum; i++) {
        if (!mStrayTicos[i]->isRescued()) {
            count++;
        }
    }

    return count;
}

void CollectTico::startAppearPowerStar() {
    MR::requestAppearPowerStar(this, mStarPos);
    setNerve(GET_NERVE(CollectTico, CollectTicoNrvAppearPowerStar));
}

void CollectTico::exeTryStartDemo() {
}
