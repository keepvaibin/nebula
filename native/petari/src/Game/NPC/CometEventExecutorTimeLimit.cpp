#include "Game/NPC/CometEventExecutorTimeLimit.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/Screen/TimeLimitLayout.hpp"
#include "Game/Util/DemoUtil.hpp"
#include "Game/Util/LiveActorUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    // A mission with no limit in the table or a mod: thirty minutes, the console's 108,000 steps.
    const u32 sCometTimeLimitDefault = NebulaFrameClock::stepsOfSeconds(30 * 60);
};  // namespace

namespace NrvCometEventExecutorTimeLimit {
    NEW_NERVE(CometEventExecutorTimeLimitStartEvent, CometEventExecutorTimeLimit, StartEvent);
};  // namespace NrvCometEventExecutorTimeLimit

CometEventExecutorTimeLimit::CometEventExecutorTimeLimit(u32 timeLimitSteps)
    : LiveActor("制限時間コメット実行者"), mTimeLimitLayout(nullptr), mTimeLimitSteps(timeLimitSteps) {
    lumacore::setClass(this, lumacore::kClassId< CometEventExecutorTimeLimit >);
}

void CometEventExecutorTimeLimit::init(const TableIter&) {
    MR::connectToSceneLayoutMovement(this);
    MR::invalidateClipping(this);

    mTimeLimitLayout = new TimeLimitLayout(getTimeLimit());
    mTimeLimitLayout->setDisplayModeOnNormal(true);
    mTimeLimitLayout->initWithoutIter();
    mTimeLimitLayout->kill();

    MR::connectToSceneLayout(mTimeLimitLayout);
    initNerve(GET_NERVE(CometEventExecutorTimeLimit, CometEventExecutorTimeLimitStartEvent));
    kill();
}

void CometEventExecutorTimeLimit::appear() {
    LiveActor::appear();
    setNerve(GET_NERVE(CometEventExecutorTimeLimit, CometEventExecutorTimeLimitStartEvent));
}

void CometEventExecutorTimeLimit::kill() {
    LiveActor::kill();
    mTimeLimitLayout->kill();
}

void CometEventExecutorTimeLimit::exeStartEvent() {
    if (MR::isFirstStep(this)) {
        mTimeLimitLayout->appear();
        mTimeLimitLayout->setTimeLimit(getTimeLimit());
    }

    if (MR::canStartDemo() && mTimeLimitLayout->isReadyToTimeUp()) {
        MR::forceKillPlayerByWaterRace();
    }
}

u32 CometEventExecutorTimeLimit::getTimeLimit() const {
    return mTimeLimitSteps != 0 ? mTimeLimitSteps : ::sCometTimeLimitDefault;
}
