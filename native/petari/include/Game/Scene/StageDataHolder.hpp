#include <stdint.h>
#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/Array.hpp"
#include "Game/Util/NameIndex.hpp"
#include <nebula_geometry.hpp>

class AssetFolder;
class PlacementId;
class PlacementInfoOrdered;

class StageDataHolder : public NameObj {
public:
    StageDataHolder(const char* pStageName, int zoneID, bool loadCommon);

    virtual void init(const TableIter& rIter) override;

    void initAfterScenarioSelected();
    void initPlacement();
    bool initPlacementStep(uint64_t deadline);
    TableIter getCommonPathPointInfo(const Table** ppInfo, int index) const;
    TableIter getCommonPathPointInfoFromRailDataIndex(const Table** ppInfo, int index) const;
    s32 getCommonPathInfoElementNum() const;
    s32 getStartPosNum() const;
    s32 getCurrentStartZoneId() const;
    s32 getCurrentStartCameraId() const;
    void getStartCameraIdInfoFromStartDataIndex(PlacementId* pInfo, int index) const;
    s32 getGeneralPosNum() const;
    TableIter getGeneralPosInfoFromDataIndex(int index) const;
    s32 getChildObjNum(const TableIter& rIter) const;
    TableIter getChildObjInfoFromDataIndex(const TableIter& rIter, int index) const;

    TableIter makeMarioTableIter(const PlacementId& rInfo) const;

    const StageDataHolder* findPlacedStageDataHolder(const TableIter& rIter) const;
    const StageDataHolder* getStageDataHolderFromZoneId(int zoneID) const;
    StageDataHolder* getStageDataHolderFromZoneId(int zoneID);
    bool isPlacedZone(int zoneID) const;
    const char* getJapaneseObjectName(const char* pName) const;
    void* getStageArchiveResource(const char* pName);
    s32 getStageArchiveResourceSize(void* pData);

    TableIter makeCurrentMarioTableIter() const;

    void initPlacementMario();

    void initTableData();

    void initPlacementInfoOrderedScenario();
    void initPlacementInfoOrderedCommon();

    void createLocalStageDataHolder(const MR::AssignableArray< Table >& rInfoArray, bool loadCommon);

    void initJmpInfo(MR::AssignableArray< Table >* pInfo, const char* pName);

    void initLayerJmpInfo(MR::AssignableArray< Table >* pInfo, const char* pPlacementDir, const char* pMapPartsDir, u32 layerMask);
    void initAllLayerJmpInfo(MR::AssignableArray< Table >* pInfo, const char* pPlacementDir, const char* pMapPartsDir);
    void initAllLayerJmpInfo(MR::AssignableArray< Table >* pInfo, const char* pName);

    const Table* findJmpInfoFromArray(const MR::AssignableArray< Table >* pInfo, const char* pName) const;
    TableIter getStartTableIterFromStartDataIndex(int index) const;

    void calcPlacementMtx(const TableIter& rIter);

    Table* attachJmpInfoToArray(Table* pInfo, const char* pDirectory);

    // Nebula (classes contract, "placement"): the layer name and the index among this zone's placements of that
    // layer (its Placement files, then its MapParts files, each file's rows in order) of row `entry` of pTable.
    // False: pTable is no placement table of this zone.
    bool findPlacementKey(const Table* pTable, s32 entry, const char** ppLayer, u32* pIndex) const;
    // The placement row of this zone by layer name (ASCII case folded) and index. False: none.
    bool findPlacementRow(const char* pLayer, u32 index, TableIter* pOut) const;

    MR::AssignableArray< Table > mPlacementObjs;
    MR::AssignableArray< Table > mStartObjs;
    MR::AssignableArray< Table > mGeneralPosObjs;
    MR::AssignableArray< Table > mChildObjs;
    MR::AssignableArray< Table > mListObjs;
    MR::AssignableArray< Table > mPathObjs;
    Table* mObjNameTbl;
    MR::NameIndex mObjNameIndex;  // en_name -> row of mObjNameTbl
    s32 mStageDataHolderCount;
    MR::GrowTable< StageDataHolder*, 0x18 > mStageDataArray;
    AssetFolder* mArchive;
    const char* mStageName;
    Pos3f mPlacementMtx;
    s32 mZoneID;
    u8 mLoadCommon;
    MR::AssignableArray< Table > mCommonPlacements;
    MR::AssignableArray< Table > mScenarioPlacements;
    PlacementInfoOrdered* mCommonPriorityOrder;
    PlacementInfoOrdered* mCommonNormalOrder;
    PlacementInfoOrdered* mScenarioPriorityOrder;
    PlacementInfoOrdered* mScenarioNormalOrder;
    PlacementInfoOrdered* mAfterScenarioOrder;
    unsigned mPlacementStage = 0;
    // The layers mCommonPlacements and mScenarioPlacements were read with (findPlacementKey).
    u32 mCommonLayerMask = 0;
    u32 mScenarioLayerMask = 0;
};

namespace MR {
    StageDataHolder* getStageDataHolder();
};  // namespace MR
