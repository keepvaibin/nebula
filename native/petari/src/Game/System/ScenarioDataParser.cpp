#include "Game/Util/SortUtil.hpp"
#include "Game/System/ScenarioDataParser.hpp"
#include "Game/System/GalaxyNameSortTable.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/AssetFolder.hpp"
#include <algorithm>
#include <cstring>
#include "Game/System/GameAssets.hpp"

template const bool Table::getValue< s32 >(int, const char*, s32*) const;

namespace {
    inline const char* getGalaxyName(const ScenarioData* pData) {
        return pData->mGalaxyName;
    }

    struct GalaxyNameSortLt {
        bool operator()(ScenarioData* pLhs, ScenarioData* pRhs) {
            return static_cast< u32 >(GalaxyNameSortTable::getGalaxySortIndex(getGalaxyName(pLhs))) <
                   static_cast< u32 >(GalaxyNameSortTable::getGalaxySortIndex(getGalaxyName(pRhs)));
        }
    };

    // FNV-1a over the name folded as MR::isEqualStringCase folds it (ASCII letters only).
    u32 hashGalaxyName(const char* pName) {
        u32 hash = 2166136261u;
        for (; *pName != '\0'; pName++) {
            u32 c = static_cast< u8 >(*pName);
            if (c >= 'A' && c <= 'Z') {
                c += 'a' - 'A';
            }
            hash = (hash ^ c) * 16777619u;
        }
        return hash;
    }

    // A galaxy's scenario archive is named for it and ends in this.
    const char cScenarioSuffix[] = "Scenario";

    // The length of the galaxy name of a scenario archive's entry, or 0 for any other entry.
    u32 getGalaxyNameLength(const GameAssets::FolderEntry& rEntry) {
        const u32 suffixLength = sizeof(cScenarioSuffix) - 1;
        const u32 length = static_cast< u32 >(strlen(rEntry.mName));
        if (!rEntry.mIsFolder || length <= suffixLength || strcmp(rEntry.mName + length - suffixLength, cScenarioSuffix) != 0) {
            return 0;
        }
        return length - suffixLength;
    }

    const ScenarioData* getCurrentScenarioData() {
        return ScenarioDataFunction::getScenarioDataParser()->getScenarioData(MR::getCurrentStageName());
    }
};  // namespace

ScenarioData::ScenarioData(const char* pGalaxyName, u32 galaxyNameLength, const char* pFilePath) : mScenarioData(), mGalaxyName(), mZoneList() {
    mGalaxyName = new char[galaxyNameLength + 1];
    memcpy(mGalaxyName, pGalaxyName, galaxyNameLength);
    mGalaxyName[galaxyNameLength] = '\0';

    AssetFolder* pArchive = MR::getArchive(pFilePath);

    mScenarioData = new Table();
    mScenarioData->attach(pArchive->getResource("/ScenarioData.bcsv"));

    mZoneList = new Table();
    mZoneList->attach(pArchive->getResource("/ZoneList.bcsv"));
}

s32 ScenarioData::getScenarioNum() const {
    s32 count = 0;
    for (s32 scenarioNo = 1; scenarioNo <= mScenarioData->getNumEntries(); scenarioNo++) {
        bool hidden = false;
        getValueBool("IsHidden", scenarioNo, &hidden);
        if (!hidden) {
            count++;
        }
    }

    return count;
}

s32 ScenarioData::getPowerStarNum() const {
    s32 count = 0;
    for (s32 scenarioNo = 1; scenarioNo <= mScenarioData->getNumEntries(); scenarioNo++) {
        u32 powerStarId = 0;
        getValueU32("PowerStarId", scenarioNo, &powerStarId);
        if (powerStarId != 0) {
            count++;
        }
    }

    return count;
}

bool ScenarioData::getValueString(const char* pKey, s32 scenarioNo, const char** ppOut) const {
    TableIter iter = getScenarioDataIter(scenarioNo);
    bool isExist = iter.getValue(pKey, ppOut);

    if (isExist) {
        if (MR::isEqualString(*ppOut, "")) {
            *ppOut = nullptr;
        }
    }

    return isExist;
}

const char* ScenarioData::getZoneName(s32 zoneId) const {
    const char* pZoneName = nullptr;
    mZoneList->getValue< const char* >(zoneId, "ZoneName", &pZoneName);

    return pZoneName;
}

TableIter ScenarioData::getScenarioDataIter(s32 scenarioNo) const {
    TableIter iter = mScenarioData->findElement< s32 >("ScenarioNo", scenarioNo, 0);
    return iter;
}

bool ScenarioData::getValueU32(const char* pKey, s32 scenarioNo, u32* pOut) const {
    TableIter iter = getScenarioDataIter(scenarioNo);
    return iter.getValue(pKey, pOut);
}

bool ScenarioData::getValueBool(const char* pKey, s32 scenarioNo, bool* pOut) const {
    TableIter iter = getScenarioDataIter(scenarioNo);
    return iter.getValue(pKey, pOut);
}

s32 ScenarioData::getZoneNum() const {
    return mZoneList->getNumEntries();
}

s32 ScenarioData::getZoneId(const char* pZoneName) const {
    for (s32 i = 0; i < getZoneNum(); i++) {
        const char* pName = nullptr;
        mZoneList->getValue< const char* >(i, "ZoneName", &pName);

        if (MR::isEqualStringCase(pName, pZoneName)) {
            return i;
        }
    }

    return 0;
}

ScenarioDataIter::ScenarioDataIter(const ScenarioDataParser* pParser, int param2) : mParser(pParser), mCur(param2) {
}

bool ScenarioDataIter::isEnd() const {
    return mParser->getScenarioDataNum() <= mCur;
}

void ScenarioDataIter::goNext() {
    if (!isEnd()) {
        mCur++;
    }
}

GalaxyStatusAccessor ScenarioDataIter::makeAccessor() const {
    return GalaxyStatusAccessor(mParser->getScenarioData(mCur));
}

ScenarioDataParser::ScenarioDataParser(const char* pName) : NameObj(pName) {
    // The galaxies are the scenario archives in /StageData (mods add more, so there is no fixed number).
    GameAssets::FolderEntry dirent;
    u32 galaxyNum = 0;
    for (u32 i = 0; GameAssets::folderEntry("/StageData", i, &dirent); i++) {
        if (getGalaxyNameLength(dirent) != 0) {
            galaxyNum++;
        }
    }
    mScenarioData = new ScenarioData*[galaxyNum > 0 ? galaxyNum : 1];

    for (u32 i = 0; static_cast< u32 >(mScenarioDataNum) < galaxyNum && GameAssets::folderEntry("/StageData", i, &dirent); i++) {
        const u32 galaxyNameLength = getGalaxyNameLength(dirent);
        if (galaxyNameLength == 0) {
            continue;
        }

        MR::FormattedName filePath;
        filePath.format("/StageData/%s", dirent.mName);
        mScenarioData[mScenarioDataNum++] = new ScenarioData(dirent.mName, galaxyNameLength, filePath.c_str());
    }

    MR::sort(mScenarioData, mScenarioData + mScenarioDataNum, GalaxyNameSortLt());

    // The name index: the first galaxy of each name in sorted order, as a scan of the list finds it.
    u32 slotNum = 4;
    while (slotNum < static_cast< u32 >(mScenarioDataNum) * 2) {
        slotNum <<= 1;
    }
    mNameSlots = new u32[slotNum];
    mNameSlotMask = slotNum - 1;
    for (u32 i = 0; i < slotNum; i++) {
        mNameSlots[i] = cNoSlot;
    }
    for (s32 i = 0; i < mScenarioDataNum; i++) {
        const char* pGalaxyName = mScenarioData[i]->mGalaxyName;
        u32 slot = hashGalaxyName(pGalaxyName) & mNameSlotMask;
        while (mNameSlots[slot] != cNoSlot && !MR::isEqualStringCase(mScenarioData[mNameSlots[slot]]->mGalaxyName, pGalaxyName)) {
            slot = (slot + 1) & mNameSlotMask;
        }
        if (mNameSlots[slot] == cNoSlot) {
            mNameSlots[slot] = static_cast< u32 >(i);
        }
    }
}

const ScenarioData* ScenarioDataParser::getScenarioData(const char* pGalaxyName) const {
    if (pGalaxyName == nullptr) {
        return nullptr;
    }

    for (u32 slot = hashGalaxyName(pGalaxyName) & mNameSlotMask; mNameSlots[slot] != cNoSlot; slot = (slot + 1) & mNameSlotMask) {
        const ScenarioData* pScenarioData = mScenarioData[mNameSlots[slot]];
        if (MR::isEqualStringCase(pScenarioData->mGalaxyName, pGalaxyName)) {
            return pScenarioData;
        }
    }

    return nullptr;
}

const ScenarioData* ScenarioDataParser::getScenarioData(s32 index) const {
    return mScenarioData[index];
}

GalaxyStatusAccessor ScenarioDataParser::makeAccessor(const char* pGalaxyName) const {
    return GalaxyStatusAccessor(getScenarioData(pGalaxyName));
}

namespace ScenarioDataFunction {
    ScenarioDataParser* getScenarioDataParser() {
        return SingletonHolder< GameSystem >::get()->mSceneController->mScenarioParser;
    }

    u32 getCurrentCommonLayers(const char* pParam1) {
        ::getCurrentScenarioData();

        return 1;
    }

    u32 getCurrentScenarioLayers(const char* pParam1, s32 param2) {
        u32 layerNum;

        if (::getCurrentScenarioData()->getValueU32(pParam1, param2, &layerNum)) {
            return layerNum * 2;
        }

        return 0;
    }
};  // namespace ScenarioDataFunction

namespace MR {
    ScenarioDataIter makeBeginScenarioDataIter() {
        return ScenarioDataIter(ScenarioDataFunction::getScenarioDataParser(), 0);
    }
};  // namespace MR
