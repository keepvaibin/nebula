#pragma once

#include "Game/Map/PlanetMap.hpp"
#include "Game/Util/NameIndex.hpp"


struct PlanetMapData {
    inline const char* getData(s32 index) const {
        return mData[index];
    }

    void setData(s32 index, const char* pValue) {
        mData[index] = pValue;
    }

    const char* mPlanetName;
    const char* mData[5];
    // ForceLowScenarioName0.. as many columns as the table has.
    const char** mForceScenarioData;
    s32 mForceScenarioNum;
};

typedef NameObj* (*CreatorFuncPtr)(const char*);

struct PlanetEntry {
    const char* mName;
    CreatorFuncPtr mCreateFunc;
};

class PlanetMapCreator : public NameObj {
public:
    PlanetMapCreator(const char*);

    CreatorFuncPtr getCreateFunc(const char* pName);
    void createPlanetMapDataTable(Table*);
    void addTableData(const Table*, s32);
    PlanetMapData* getTableData(const char*) const;
    bool isScenarioForceLow(const PlanetMapData*) const;

    PlanetMapData** mPlanetMapData;
    s32 mTableCount;
    MR::NameIndex mIndex;  // planet name -> its first row
};

class PlanetMapFarClippable : public PlanetMap {
public:
    PlanetMapFarClippable(const char* pName) : PlanetMap(pName) {
    }

    f32 getFarClipDistance() const override {
        return 50.0f;
    }
};

class PlanetMapCreatorFunction {
public:

    static bool isLoadArchiveAfterScenarioSelected(const char*);
    static bool isRegisteredObj(const char*);

    static CreatorFuncPtr getPlanetMapCreator(const char* pName);
};
