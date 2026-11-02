#pragma once

#include "Game/Map/PlanetMap.hpp"

class NameObjArchiveListCollector;

struct PlanetMapData {
    inline const char* getData(s32 index) const {
        return mData[index];
    }

    void setData(s32 index, const char* pValue) {
        mData[index] = pValue;
    }

    const char* mPlanetName;
    const char* mData[5];
    const char* mForceScenarioData[8];
};

struct UniqueEntry {
    const char* _0;
    const char* _4;
};

typedef NameObj* (*CreatorFuncPtr)(const char*);

struct PlanetEntry {
    const char* mName;
    CreatorFuncPtr mCreateFunc;
};

class PlanetMapCreator : public NameObj {
public:
    PlanetMapCreator(const char*);

    CreatorFuncPtr getCreateFunc(const char*);
    void makeArchiveListPlanet(NameObjArchiveListCollector*, const JMapInfoIter&, const char*);
    void createPlanetMapDataTable(JMapInfo*);
    void addTableData(const JMapInfo*, s32);
    PlanetMapData* getTableData(const char*) const;
    bool isScenarioForceLow(const PlanetMapData*) const;

    PlanetMapData** mPlanetMapData;
    s32 mTableCount;
};

class PlanetMapFarClippable : public PlanetMap {
public:
    PlanetMapFarClippable(const char* pName) : PlanetMap(pName) {
    }

    f32 getFarClipDistance() const {
        return 50.0f;
    }
};

class PlanetMapCreatorFunction {
public:
    static void makeArchiveList(NameObjArchiveListCollector*, const JMapInfoIter&, const char*);

    static bool isLoadArchiveAfterScenarioSelected(const char*);
    static bool isRegisteredObj(const char*);

    static CreatorFuncPtr getPlanetMapCreator(const char*);
};
