#include "Game/NPC/EventDirector.hpp"
#include "Game/LiveActor/Nerve.hpp"
#include "Game/MapObj/PowerStarHolder.hpp"
#include "Game/NPC/CometEventKeeper.hpp"
#include "Game/NPC/PowerStarEventKeeper.hpp"
#include "Game/NPC/StageStateKeeper.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/LumaCore.hpp"

EventDirector::EventDirector()
    : NameObj("イベント指揮"), mPowerStarEventKeeper(), mStageStateKeeper(), mPowerStarHolder(), mCometEventKeeper() {
    lumacore::setClass(this, lumacore::kClassId< EventDirector >);
}

void EventDirector::init(const TableIter& /*rIter*/) {
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

bool EventFunction::isStartCometEvent(const char* pName) {
    return getCometEventKeeper()->isStartEvent(pName);
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

    bool isSuccessEventPowerStar(const char* pName, s32 starNum) {
        return EventFunction::getPowerStarEventKeeper()->isSuccess(pName, starNum);
    }

    bool isGreenEventPowerStar(const char* pName, s32 starNum) {
        return EventFunction::getPowerStarEventKeeper()->isGreen(pName, starNum);
    }

    bool isRedEventPowerStar(const char* pName, s32 starNum) {
        return EventFunction::getPowerStarEventKeeper()->isRed(pName, starNum);
    }

    bool isGrandEventPowerStar(const char* pName, s32 starNum) {
        return EventFunction::getPowerStarEventKeeper()->isGrand(pName, starNum);
    }

    void appearEventPowerStar(const char* pName, s32 starNum, const Vec3f* pPos, bool forceAppear, bool withoutDemo) {
        EventFunction::getPowerStarEventKeeper()->requestAppearPowerStar(pName, starNum, pPos, forceAppear, withoutDemo);
    }

    bool isEndEventPowerStarAppearDemo(const char* pName) {
        int starId = EventFunction::getPowerStarEventKeeper()->findStarID(pName);

        return PowerStarFunction::isEndPowerStarAppearDemo(starId);
    }
};  // namespace MR
