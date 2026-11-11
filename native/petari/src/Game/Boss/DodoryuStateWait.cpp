#include "Game/Boss/DodoryuStateWait.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/Boss/Dodoryu.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Util/ActorShadowUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/SoundUtil.hpp"
#include "Game/Util/Tunable.hpp"
#include "Game/System/LumaCore.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_exeWait_f1, "exeWait.f1", 8.0f);

namespace {
    NEW_NERVE(DodoryuStateWaitNrvWait, DodoryuStateWait, Wait);
};  // namespace

DodoryuStateWait::DodoryuStateWait(Dodoryu* pHost, const char* pName) : DodoryuStateBase(pHost, pName), mNextStateCounter() {
    lumacore::setClass(this, lumacore::kClassId< DodoryuStateWait >);
    initWithoutIter();
}

void DodoryuStateWait::init(const TableIter& /*rIter*/) {
    MR::invalidateClipping(this);
    initNerve(GET_NERVE_ANON(DodoryuStateWaitNrvWait));
    makeActorAppeared();
}

void DodoryuStateWait::exeWait() {
    if (MR::isFirstStep(this)) {
        MR::invalidateShadowAll(mHost);
        MR::startBck(mHost, "UnderGroundWalk");
        MR::startBtp(mHost, "Normal");
        mHost->setHillAppearNumHalf();
        mHost->startHill();
        mHost->shiftMoveStateRail(cNum_exeWait_f1);

        if (MR::isStageSuddenDeathDodoryu() && MR::hasRetryGalaxySequence()) {
            MR::forceCloseWipeCircle();
        }
    }

    MR::startLevelSound(mHost, "SE_BM_LV_DODORYU_MOVE_U_SHALLOW");

    if (mHost->mIsSwitchOn && !MR::isPlayerInBind()) {
        mNextStateCounter++;
    } else {
        mNextStateCounter = 0;
    }

    if (mNextStateCounter > NebulaFrameClock::stepsOfMilliseconds(500)) {
        if (MR::isStageSuddenDeathDodoryu() && MR::hasRetryGalaxySequence()) {
            MR::openWipeCircle();
        }

        mHost->nextState();
    }
}
