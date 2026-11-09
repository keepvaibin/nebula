#include "Game/NPC/CometEventKeeper.hpp"
#include "Game/System/NebulaFrameClock.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/NPC/CometEventExecutorTimeLimit.hpp"
#include "Game/Screen/GalaxyCometScreenFilter.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/NebulaMods.hpp"

struct GalaxyTimeLimitInfo {
    const char* mGalaxyName;
    s32 mScenarioNo;
    u32 mTimeLimit;
};

namespace {
    static const GalaxyTimeLimitInfo sTimeLimitInfoTable[] = {
        {"EggStarGalaxy", 4, 240},      {"StarDustGalaxy", 4, 240}, {"SandClockGalaxy", 4, 270},
        {"CannonFleetGalaxy", 4, 360},  {"StarDustGalaxy", 5, 120}, {"PhantomGalaxy", 5, 60},
        {"CosmosGardenGalaxy", 5, 150}, {"FactoryGalaxy", 5, 180},  {"ReverseKingdomGalaxy", 5, 210},
    };
};  // namespace

CometEventKeeper::CometEventKeeper() : mExecutorTimeLimit(nullptr), mScreenFilter(nullptr), mCometName(nullptr){
}

void CometEventKeeper::init() {
    initCometStatus();

    if (isStartEvent("Red") || isStartEvent("Black")) {
        const u32 modLimit = NebulaMods::cometTimeLimit(MR::getCurrentStageName(), MR::getCurrentScenarioNo());
        mExecutorTimeLimit = new CometEventExecutorTimeLimit((modLimit != 0 ? modLimit : getTimeLimitFromTable(::sTimeLimitInfoTable, ARRAY_SIZE(::sTimeLimitInfoTable))) / static_cast< s32 >(NebulaFrameClock::cStepsPerSecond));
        mExecutorTimeLimit->initWithoutIter();
        mExecutorTimeLimit->kill();
    }

    if (mCometName != nullptr) {
        mScreenFilter = new GalaxyCometScreenFilter();
        mScreenFilter->initWithoutIter();
        mScreenFilter->setCometType(mCometName);
    }
}

bool CometEventKeeper::isStartEvent(const char* pCometName) const {
    if (mCometName == nullptr) {
        return false;
    }

    return MR::isEqualString(mCometName, pCometName);
}

void CometEventKeeper::startCometEventIfExist() {
    if (mExecutorTimeLimit == nullptr) {
        return;
    }

    mExecutorTimeLimit->appear();
}

void CometEventKeeper::endCometEvent() {
    if (mExecutorTimeLimit == nullptr) {
        return;
    }

    mExecutorTimeLimit->kill();
}

u32 CometEventKeeper::getTimeLimitFromTable(const GalaxyTimeLimitInfo* pTable, int size) {
    for (int i = 0; i < size; i++) {
        const GalaxyTimeLimitInfo* pInfo = &pTable[i];

        if (pInfo->mScenarioNo != MR::getCurrentScenarioNo()) {
            continue;
        }

        if (!MR::isEqualString(MR::getCurrentStageName(), pInfo->mGalaxyName)) {
            continue;
        }

        return NebulaFrameClock::stepsOfSeconds(pInfo->mTimeLimit);
    }

    return 0;
}

void CometEventKeeper::initCometStatus() {
    mCometName = MR::makeCurrentGalaxyStatusAccessor().getCometName(MR::getCurrentScenarioNo());
}
