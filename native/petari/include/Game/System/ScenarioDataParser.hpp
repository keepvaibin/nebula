#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/NameIndex.hpp"

class GalaxyStatusAccessor;
class Table;
class TableIter;
class ScenarioData;

class ScenarioDataParser : public NameObj {
public:
    ScenarioDataParser(const char* pName);

    // The galaxy of that name (any case), through the name index: null when there is none.
    const ScenarioData* getScenarioData(const char*) const;
    const ScenarioData* getScenarioData(s32) const;
    s32 getScenarioDataNum() const {
        return mScenarioDataNum;
    }
    GalaxyStatusAccessor makeAccessor(const char*) const;

private:
    // Every stage folder with a scenario file, in galaxy info table order; built once, sized to the folders.
    ScenarioData** mScenarioData = nullptr;
    s32 mScenarioDataNum = 0;
    // Galaxy name (any case) -> its index in mScenarioData, the first of a name in sorted order.
    MR::NameIndexOf< MR::NameKeyCase > mNames;
};

class ScenarioDataIter {
public:
    ScenarioDataIter(const ScenarioDataParser*, int index);

    bool isEnd() const;
    void goNext();
    GalaxyStatusAccessor makeAccessor() const;

    const ScenarioDataParser* mParser;
    int mCur;
};

class ScenarioData {
public:
    // A galaxy's scenario data, from its archive pFilePath; its name is the first galaxyNameLength characters of pGalaxyName.
    ScenarioData(const char* pGalaxyName, u32 galaxyNameLength, const char* pFilePath);

    s32 getScenarioNum() const;
    s32 getPowerStarNum() const;
    bool getValueString(const char*, s32, const char**) const;
    const char* getZoneName(s32) const;
    TableIter getScenarioDataIter(s32) const;
    bool getValueU32(const char*, s32, u32*) const;
    bool getValueBool(const char*, s32, bool*) const;
    s32 getZoneNum() const;
    s32 getZoneId(const char*) const;

    Table* mScenarioData;
    char* mGalaxyName;
    Table* mZoneList;
    // Its row in the galaxy info table; one not listed sorts last.
    u32 mSortKey = 0xFFFFFFFFu;
};

namespace ScenarioDataFunction {
    ScenarioDataParser* getScenarioDataParser();
    u32 getCurrentCommonLayers(const char* pZoneName);
    u32 getCurrentScenarioLayers(const char* pZoneName, s32 scenarioNo);
};  // namespace ScenarioDataFunction

namespace MR {
    ScenarioDataIter makeBeginScenarioDataIter();
};  // namespace MR
