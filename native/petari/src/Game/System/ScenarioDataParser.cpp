#include "Game/Util/SortUtil.hpp"
#include "Game/System/ScenarioDataParser.hpp"
#include "Game/System/GalaxyInfoTable.hpp"
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
#include "Game/System/LumaCore.hpp"

namespace {
    struct GalaxyNameSortLt {
        bool operator()(const ScenarioData* pLhs, const ScenarioData* pRhs) const {
            return pLhs->mSortKey < pRhs->mSortKey;
        }
    };

    // A galaxy's scenario archive is named for it and ends in this.
    const char cScenarioSuffix[] = "Scenario";

    // The length of the galaxy name of a scenario archive's entry, or 0 for any other entry.
    u32 getGalaxyNameLength(const GameAssets::FolderEntry& rEntry) {
        const u32 suffixLength = sizeof(cScenarioSuffix) - 1;
        const u32 length = static_cast< u32 >(strlen(rEntry.mName));
        if (!rEntry.mIsFolder || length <= suffixLength || !MR::isEqualString(rEntry.mName + length - suffixLength, cScenarioSuffix)) {
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

s32 ScenarioData::getScenarioNoMax() const {
    s32 maxNo = 0;
    for (s32 idx = 0; idx < mScenarioData->getNumEntries(); idx++) {
        s32 scenarioNo = 0;
        mScenarioData->getValue< s32 >(idx, "ScenarioNo", &scenarioNo);
        if (maxNo < scenarioNo) {
            maxNo = scenarioNo;
        }
    }

    return maxNo;
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
    const bool isExist = iter.getValue(pKey, ppOut);
    // An empty string reads as no value.
    if (isExist && MR::isEqualString(*ppOut, "")) {
        *ppOut = nullptr;
    }

    return isExist;
}

const char* ScenarioData::getZoneName(s32 zoneId) const {
    const char* pZoneName = nullptr;
    mZoneList->getValue< const char* >(zoneId, "ZoneName", &pZoneName);

    return pZoneName;
}

TableIter ScenarioData::getScenarioDataIter(s32 scenarioNo) const {
    return mScenarioData->findElement< s32 >("ScenarioNo", scenarioNo, 0);
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

ScenarioDataIter::ScenarioDataIter(const ScenarioDataParser* pParser, int index) : mParser(pParser), mCur(index) {
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
    lumacore::setClass(this, lumacore::kClassId< ScenarioDataParser >);
    // The galaxies are the scenario archives in /StageData (mods add more, so there is no fixed number).
    GameAssets::FolderEntry dirent;
    u32 galaxyNum = 0;
    for (u32 i = 0; GameAssets::folderEntry("/StageData", i, &dirent); i++) {
        if (getGalaxyNameLength(dirent) != 0) {
            galaxyNum++;
        }
    }
    mScenarioData = new ScenarioData*[galaxyNum > 0 ? galaxyNum : 1];

    Table galaxyInfo;
    galaxyInfo.attach(NebulaDataTables::getGalaxyInfoTable());
    for (u32 i = 0; static_cast< u32 >(mScenarioDataNum) < galaxyNum && GameAssets::folderEntry("/StageData", i, &dirent); i++) {
        const u32 galaxyNameLength = getGalaxyNameLength(dirent);
        if (galaxyNameLength == 0) {
            continue;
        }

        MR::FormattedName filePath;
        filePath.format("/StageData/%s", dirent.mName);
        ScenarioData* const pData = new ScenarioData(dirent.mName, galaxyNameLength, filePath.c_str());
        const TableIter row = galaxyInfo.findElement("name", static_cast< const char* >(pData->mGalaxyName), 0);
        pData->mSortKey = row == galaxyInfo.end() ? 0xFFFFFFFFu : static_cast< u32 >(row.mIndex);
        mScenarioData[mScenarioDataNum++] = pData;
    }

    MR::sort(mScenarioData, mScenarioData + mScenarioDataNum, GalaxyNameSortLt());

    // The first galaxy of each name in sorted order, as a scan of the list finds it.
    mNames.reset(static_cast< u32 >(mScenarioDataNum));
    for (s32 i = 0; i < mScenarioDataNum; i++) {
        mNames.add(mScenarioData[i]->mGalaxyName, static_cast< u32 >(i));
    }
}

const ScenarioData* ScenarioDataParser::getScenarioData(const char* pGalaxyName) const {
    if (pGalaxyName == nullptr) {
        return nullptr;
    }

    const u32 index = mNames.find(pGalaxyName);
    return index == MR::NameIndexOf< MR::NameKeyCase >::cNoValue ? nullptr : mScenarioData[index];
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

    // The common layer is bit 0 of every zone.
    u32 getCurrentCommonLayers(const char* /*pZoneName*/) {
        return 1;
    }

    u32 getCurrentScenarioLayers(const char* pZoneName, s32 scenarioNo) {
        u32 layerNum;

        if (::getCurrentScenarioData()->getValueU32(pZoneName, scenarioNo, &layerNum)) {
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
