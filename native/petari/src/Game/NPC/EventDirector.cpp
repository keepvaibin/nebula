#include "Game/NPC/EventDirector.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/PowerStarHolder.hpp"
#include "Game/NPC/CometEventKeeper.hpp"
#include "Game/NPC/PowerStarEventKeeper.hpp"
#include "Game/NPC/StageStateKeeper.hpp"
#include "Game/NPC/TimeAttackEventKeeper.hpp"
#include "Game/Scene/SceneObjHolder.hpp"

EventDirector::EventDirector()
    : NameObj("\203\103\203\170\203\223\203\147\216\167\212\366" /* イベント指揮 */), mPowerStarEventKeeper(), mStageStateKeeper(), mPowerStarHolder(), mCometEventKeeper(), mTimeAttackEventKeeper() {
}

void EventDirector::init(const JMapInfoIter& rIter) {
    mPowerStarEventKeeper = new PowerStarEventKeeper();
    mStageStateKeeper = new StageStateKeeper();
    mPowerStarHolder = new PowerStarHolder("\203\160\203\217\201\133\203\130\203\136\201\133\225\333\216\235" /* パワースター保持 */);
    mPowerStarHolder->initWithoutIter();
    mCometEventKeeper = new CometEventKeeper();
    mCometEventKeeper->init();
    mTimeAttackEventKeeper = new TimeAttackEventKeeper();
    mTimeAttackEventKeeper->init(mCometEventKeeper->isStartTimeLimitEvent());
}

void MR::declareEventPowerStar(const char* pParam1, s32 param2, bool param3) {
    EventFunction::getPowerStarEventKeeper()->declareStar(pParam1, "\203\135\201\133\203\223\226\263\202\265" /* ゾーン無し */, param2, param3);
}

PowerStarEventKeeper* EventFunction::getPowerStarEventKeeper() {
    return MR::getSceneObj< EventDirector >(SceneObj_EventDirector)->mPowerStarEventKeeper;
}

StageStateKeeper* EventFunction::getStageStateKeeper() {
    return MR::getSceneObj< EventDirector >(SceneObj_EventDirector)->mStageStateKeeper;
}

PowerStarHolder* EventFunction::getPowerStarHolder() {
    return MR::getSceneObj< EventDirector >(SceneObj_EventDirector)->mPowerStarHolder;
}

CometEventKeeper* EventFunction::getCometEventKeeper() {
    return MR::getSceneObj< EventDirector >(SceneObj_EventDirector)->mCometEventKeeper;
}

TimeAttackEventKeeper* EventFunction::getTimeAttackEventKeeper() {
    return MR::getSceneObj< EventDirector >(SceneObj_EventDirector)->mTimeAttackEventKeeper;
}

bool EventFunction::isStartCometEvent(const char* pParam1) {
    return getCometEventKeeper()->isStartEvent(pParam1);
}

void EventFunction::startCometEvent() {
    getCometEventKeeper()->startCometEventIfExist();
    getTimeAttackEventKeeper()->startEventIfExecute();
}

void EventFunction::endCometEvent() {
    getCometEventKeeper()->endCometEvent();
    getTimeAttackEventKeeper()->endEvent();
}

namespace MR {
    void initEventSystemAfterPlacement() {
        EventFunction::getPowerStarEventKeeper()->initStarInfoTableAfterPlacement();
    }

    void declareEventPowerStar(const NameObj* pParam1, s32 param2, bool param3) {
        const char* pVar1 = pParam1->mName;

        EventFunction::getPowerStarEventKeeper()->declareStar(pVar1, nullptr, param2, param3);
    }

    bool isSuccessEventPowerStar(const char* pParam1, s32 param2) {
        return EventFunction::getPowerStarEventKeeper()->isSuccess(pParam1, param2);
    }

    bool isGreenEventPowerStar(const char* pParam1, s32 param2) {
        return EventFunction::getPowerStarEventKeeper()->isGreen(pParam1, param2);
    }

    bool isRedEventPowerStar(const char* pParam1, s32 param2) {
        return EventFunction::getPowerStarEventKeeper()->isRed(pParam1, param2);
    }

    bool isGrandEventPowerStar(const char* pParam1, s32 param2) {
        return EventFunction::getPowerStarEventKeeper()->isGrand(pParam1, param2);
    }

    void appearEventPowerStar(const char* pParam1, s32 param2, const TVec3f* pParam3, bool param4, bool param5) {
        EventFunction::getPowerStarEventKeeper()->requestAppearPowerStar(pParam1, param2, pParam3, param4, param5);
    }

    bool isEndEventPowerStarAppearDemo(const char* pParam1) {
        int starId = EventFunction::getPowerStarEventKeeper()->findStarID(pParam1);

        return PowerStarFunction::isEndPowerStarAppearDemo(starId);
    }
};  // namespace MR
