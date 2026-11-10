#include "Game/Boss/SkeletalFishBossInfo.hpp"
#include "Game/Boss/SkeletalFishBoss.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/ObjUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/Util/Tunable.hpp"

NEBULA_TUNABLE_NAMED(f32, cNum_createLevelStatus_f1, "createLevelStatus.f1", 20.0f);
NEBULA_TUNABLE_NAMED(f32, cNum_createLevelStatus_f2, "createLevelStatus.f2", 2000.0f);

namespace {
    static const char* sParamFileName = "Param";
    static const char* sParamNameLevelNum = "LevelNum";
    static const char* sParamNameEnergy = "EnergyLv";
    static const char* sParamNameSpeed = "SpeedLv";
    static const char* sParamNameRailID = "RailIDLv";
    static const char* sParamNameGuardOffset = "GuardOffsetLv";
    static const char* sParamNameGuardAppearNum = "GuardAppearNumLv";
    static const char* sParamNameGuard = "Id";
    static const char* sParamNameGuardPos = "GuardPosLv";
    static const char* sParamNameGuardWait = "GuardWaitLv";
};  // namespace

SkeletalFishBossInfo::SkeletalFishBossInfo(SkeletalFishBoss* pBoss, s32 levelNum, s32 guardNum, const char* pName)
    : NameObj(pName), mFishBoss(pBoss), mLevelNum(levelNum), mGuardNum(guardNum), mLevelStatusArray() {
    createLevelStatus();
}

void SkeletalFishBossInfo::init(const TableIter& /*rIter*/) {
    const Table* pCsvParser = MR::tryCreateCsvParser(mFishBoss, "%s.bcsv", ::sParamFileName);

    if (pCsvParser != nullptr) {
        loadLevelStatus(pCsvParser->begin());
    }
}

SkeletalFishBossInfo::LevelStatus* SkeletalFishBossInfo::getLevelStatus(s32 idx) const {
    return mLevelStatusArray + idx;
}

void SkeletalFishBossInfo::createLevelStatus() {
    mLevelStatusArray = new LevelStatus[mLevelNum];

    for (s32 i = 0; i < mLevelNum; i++) {
        getLevelStatus(i)->mEnergyLevel = 1;
        getLevelStatus(i)->mSpeedLevel = cNum_createLevelStatus_f1;
        getLevelStatus(i)->mRailIDLevel = 0;
        getLevelStatus(i)->mGuardOffsLevel = cNum_createLevelStatus_f2;
        getLevelStatus(i)->mGuardAppearNumLevel = 0;
        getLevelStatus(i)->mStatusArray = new GuardStatus[mGuardNum];

        for (s32 j = 0; j < mGuardNum; j++) {
            mLevelStatusArray[i].getGuardStatus(j)->mGuardPosLevel.zero();
            mLevelStatusArray[i].getGuardStatus(j)->mGuardWaitLevelID = 0;
        }
    }
}

SkeletalFishBossInfo::GuardStatus::GuardStatus() {
}

void SkeletalFishBossInfo::loadLevelStatus(const TableIter& rIter) {
    s32 levelNum;

    if (!rIter.getValue< s32 >(::sParamNameLevelNum, &levelNum)) {
        return;
    }

    if (levelNum > mLevelNum) {
        levelNum = mLevelNum;
    }

    for (s32 i = 0; i < levelNum; i++) {
        MR::FormattedName paramName;

        paramName.format("%s%d", ::sParamNameEnergy, i);
        rIter.getValue< s32 >(paramName.c_str(), &getLevelStatus(i)->mEnergyLevel);

        paramName.format("%s%d", ::sParamNameSpeed, i);
        rIter.getValue< f32 >(paramName.c_str(), &getLevelStatus(i)->mSpeedLevel);

        paramName.format("%s%d", ::sParamNameRailID, i);
        rIter.getValue< s32 >(paramName.c_str(), &getLevelStatus(i)->mRailIDLevel);

        paramName.format("%s%d", ::sParamNameGuardOffset, i);
        rIter.getValue< f32 >(paramName.c_str(), &getLevelStatus(i)->mGuardOffsLevel);

        paramName.format("%s%d", ::sParamNameGuardAppearNum, i);
        rIter.getValue< s32 >(paramName.c_str(), &getLevelStatus(i)->mGuardAppearNumLevel);

        loadGuardStatus(rIter, i, mLevelStatusArray[i].mGuardAppearNumLevel);
    }
}

void SkeletalFishBossInfo::loadGuardStatus(const TableIter& rIter, s32 levelIdx, s32 guardLevel) {
    LevelStatus* pStatus = getLevelStatus(levelIdx);

    // A level has mGuardNum guard places; a file asking for more fills only those.
    if (guardLevel > mGuardNum) {
        guardLevel = mGuardNum;
    }

    for (s32 i = 0; i < guardLevel; i++) {
        MR::FormattedName paramName;

        paramName.format("%s%d%s%d", ::sParamNameGuardPos, levelIdx, ::sParamNameGuard, i);
        MR::getPlacementV3f(rIter, paramName.c_str(), &pStatus->getGuardStatus(i)->mGuardPosLevel);

        paramName.format("%s%d%s%d", ::sParamNameGuardWait, levelIdx, ::sParamNameGuard, i);
        rIter.getValue< s32 >(paramName.c_str(), &pStatus->getGuardStatus(i)->mGuardWaitLevelID);
    }
}
