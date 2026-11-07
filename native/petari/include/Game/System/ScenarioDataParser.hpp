#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/Array.hpp"

class GalaxyStatusAccessor;
class Table;
class TableIter;
class ScenarioData;

class ScenarioDataParser : public NameObj {
public:
    /// @brief Creates a new `ScenarioDataParser`.
    /// @param pName A pointer to the null-terminated name of the object.
    ScenarioDataParser(const char* pName);

    // The galaxy of that name (any case), through the name index: null when there is none.
    const ScenarioData* getScenarioData(const char*) const;
    const ScenarioData* getScenarioData(s32) const;
    s32 getScenarioDataNum() const {
        return mScenarioDataNum;
    }
    GalaxyStatusAccessor makeAccessor(const char*) const;

private:
    static constexpr u32 cNoSlot = 0xFFFFFFFFu;

    // Every stage folder with a scenario file, in GalaxyNameSortTable order; built once, sized to the folders.
    ScenarioData** mScenarioData = nullptr;
    s32 mScenarioDataNum = 0;
    // Open addressing on the case-folded name, at most half full: an index into mScenarioData, or cNoSlot.
    u32* mNameSlots = nullptr;
    u32 mNameSlotMask = 0;
};

class ScenarioDataIter {
public:
    ScenarioDataIter(const ScenarioDataParser*, int);

    bool isEnd() const;
    void goNext();
    GalaxyStatusAccessor makeAccessor() const;

    const ScenarioDataParser* mParser;
    int mCur;
};

class ScenarioData {
public:
    ScenarioData(const char*);

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
};

namespace ScenarioDataFunction {
    ScenarioDataParser* getScenarioDataParser();
    u32 getCurrentCommonLayers(const char*);
    u32 getCurrentScenarioLayers(const char*, s32);
};  // namespace ScenarioDataFunction

namespace MR {
    ScenarioDataIter makeBeginScenarioDataIter();
};  // namespace MR
