#include "Game/NPC/EventDirector.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/PowerStarHolder.hpp"
#include "Game/NPC/CometEventKeeper.hpp"
#include "Game/NPC/PowerStarEventKeeper.hpp"
#include "Game/NPC/StageStateKeeper.hpp"
#include "Game/Scene/SceneObjHolder.hpp"

EventDirector::EventDirector()
    : NameObj("イベント指揮"), mPowerStarEventKeeper(), mStageStateKeeper(), mPowerStarHolder(), mCometEventKeeper() {
}

void EventDirector::init(const TableIter& rIter) {
    mPowerStarEventKeeper = new PowerStarEventKeeper();
    mStageStateKeeper = new StageStateKeeper();
    mPowerStarHolder = new PowerStarHolder("パワースター保持");
    mPowerStarHolder->initWithoutIter();
    mCometEventKeeper = new CometEventKeeper();
    mCometEventKeeper->init();
}

void MR::declareEventPowerStar(const char* pDemoName, s32 starId, bool killInDemo) {
    EventFunction::getPowerStarEventKeeper()->declareStar(pDemoName, starId, killInDemo);
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

bool EventFunction::isStartCometEvent(const char* pParam1) {
    return getCometEventKeeper()->isStartEvent(pParam1);
}

void EventFunction::startCometEvent() {
    getCometEventKeeper()->startCometEventIfExist();
}

void EventFunction::endCometEvent() {
    getCometEventKeeper()->endCometEvent();
}

namespace MR {
    void initEventSystemAfterPlacement() {
        EventFunction::getPowerStarEventKeeper()->initStarInfoTableAfterPlacement();
    }

    void declareEventPowerStar(const NameObj* pDemoObj, s32 starId, bool killInDemo) {
        EventFunction::getPowerStarEventKeeper()->declareStar(pDemoObj->mName, starId, killInDemo);
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

    void appearEventPowerStar(const char* pParam1, s32 param2, const Vec3f* pParam3, bool param4, bool param5) {
        EventFunction::getPowerStarEventKeeper()->requestAppearPowerStar(pParam1, param2, pParam3, param4, param5);
    }

    bool isEndEventPowerStarAppearDemo(const char* pParam1) {
        int starId = EventFunction::getPowerStarEventKeeper()->findStarID(pParam1);

        return PowerStarFunction::isEndPowerStarAppearDemo(starId);
    }
};  // namespace MR
