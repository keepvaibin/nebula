#include "Game/NPC/PowerStarEventKeeper.hpp"
#include "Game/MapObj/PowerStarHolder.hpp"
#include "Game/NPC/EventDirector.hpp"
#include "Game/NPC/StageStateKeeper.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/Util/EventUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StringUtil.hpp"

PowerStarEventKeeper::PowerStarEventKeeper() : mStartScenario(1), mPowerStarNum(1), mInfoTbl() {
    mPowerStarNum = MR::getCurrentStagePowerStarNum();
    initStarInfoTable();
}

void PowerStarEventKeeper::initStarInfoTable() {
    mInfoTbl = new PowerStarInfo[mPowerStarNum];

    for (s32 starID = 0; starID < mPowerStarNum; starID++) {
        PowerStarInfo* pInfo = getStarInfo(starID + mStartScenario - 1);
        pInfo->mIsExist = false;
        pInfo->mIsAppearAtStart = false;
        pInfo->mIsKillInDemo = false;
        pInfo->mDemoName = nullptr;
    }

    for (s32 scenarioNo = mStartScenario; scenarioNo <= mPowerStarNum; scenarioNo++) {
        if (isExistPowerStarAtScenario(scenarioNo)) {
            getStarInfo(scenarioNo - 1)->mIsExist = true;
        }

        if (MR::getAppearPowerStarObjName(scenarioNo) == nullptr) {
            getStarInfo(scenarioNo - 1)->mIsAppearAtStart = true;
        }
    }
}

s32 PowerStarEventKeeper::findStarID(const char* pName) const {
    if (MR::getAppearPowerStarObjName(MR::getCurrentScenarioNo()) != nullptr &&
        MR::isEqualString(pName, MR::getAppearPowerStarObjName(MR::getCurrentScenarioNo()))) {
        return MR::getCurrentScenarioNo();
    }

    for (s32 starID = mStartScenario; starID <= mPowerStarNum; starID++) {
        if (starID == MR::getCurrentScenarioNo()) {
            continue;
        }

        if (MR::getAppearPowerStarObjName(starID) == nullptr) {
            continue;
        }

        if (MR::isEqualString(pName, MR::getAppearPowerStarObjName(starID))) {
            return starID;
        }
    }

    return -1;
}

void PowerStarEventKeeper::declareStar(const char* pDemoName, s32 starID, bool killInDemo) {
    if (starID < 0) {
        starID = findStarID(pDemoName);
    }

    if (!isActiveStarId(starID)) {
        return;
    }

    PowerStarInfo* pInfo = getStarInfo(starID - 1);
    pInfo->mDemoName = pDemoName;
    pInfo->mIsKillInDemo = killInDemo;
}

bool PowerStarEventKeeper::isGreen(const char* pName, s32 starID) const {
    if (starID < 0) {
        starID = findStarID(pName);
    }

    if (!isActiveStarId(starID)) {
        return false;
    }

    return MR::isPowerStarGreenInCurrentStage(starID);
}

bool PowerStarEventKeeper::isRed(const char* pName, s32 starID) const {
    if (starID < 0) {
        starID = findStarID(pName);
    }

    if (!isActiveStarId(starID)) {
        return false;
    }

    return MR::isPowerStarRedInCurrentStage(starID);
}

bool PowerStarEventKeeper::isGrand(const char* pName, s32 starID) const {
    if (starID < 0) {
        starID = findStarID(pName);
    }

    if (!isActiveStarId(starID)) {
        return false;
    }

    return MR::isGrandStarInCurrentStage(starID);
}

void PowerStarEventKeeper::initStarInfoTableAfterPlacement() {
    PowerStarInfo* pInfo;
    for (s32 powerStarNo = 0; powerStarNo < mPowerStarNum; powerStarNo++) {
        pInfo = getStarInfo(powerStarNo + mStartScenario - 1);

        if (!pInfo->mIsExist) {
            continue;
        }

        if (pInfo->mDemoName != nullptr) {
            PowerStar* pPowerStar = PowerStarFunction::findPowerStar(mStartScenario + powerStarNo);
            // A star number no star is placed for has no demo flag to set.
            if (pPowerStar == nullptr) {
                continue;
            }
            pPowerStar->mIsInDemo = true;

            if (pInfo->mIsKillInDemo) {
                pPowerStar->kill();
            }

            continue;
        }

        if (pInfo->mIsAppearAtStart) {
            PowerStar* const pAppearStar = PowerStarFunction::findPowerStar(mStartScenario + powerStarNo);
            if (pAppearStar != nullptr) {
                pAppearStar->appear();
            }
        }
    }
}

void PowerStarEventKeeper::requestAppearPowerStar(const char* pName, s32 starNum, const Vec3f* pPos, bool forceAppear, bool withoutDemo) {
    if (starNum < 0) {
        starNum = findStarID(pName);
    }

    // An object no mission's table names has no star to give.
    if (starNum <= 0) {
        return;
    }

    StageStateKeeper* pStateKeeper = EventFunction::getStageStateKeeper();
    pStateKeeper->mIsPowerStarAppeared = true;

    if (withoutDemo) {
        EventFunction::getPowerStarHolder()->appearPowerStarWithoutDemo(starNum);
    } else {
        EventFunction::getPowerStarHolder()->requestAppearPowerStar(starNum, pPos, forceAppear);
    }
}

bool PowerStarEventKeeper::isExistPowerStarAtScenario(int scenarioNo) const {
    GalaxyStatusAccessor accessor = MR::makeCurrentGalaxyStatusAccessor();
    return accessor.getActivePowerStarId(MR::getCurrentScenarioNo()) & calcStarID(scenarioNo);
}

// A star this galaxy's current scenario has (ids from 1).
bool PowerStarEventKeeper::isActiveStarId(s32 starId) const {
    if (starId <= 0) {
        return false;
    }

    return isExistPowerStarAtScenario(starId);
}

bool PowerStarEventKeeper::isSuccess(const char* pName, s32 starNum) const {
    if (starNum < 0) {
        starNum = findStarID(pName);
    }

    if (!isActiveStarId(starNum)) {
        return false;
    }

    return MR::hasPowerStarInCurrentStage(starNum);
}
