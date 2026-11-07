#include <stdint.h>
#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/Array.hpp"
#include <nebula_geometry.hpp>

class AssetFolder;
class PlacementId;
class PlacementInfoOrdered;

class StageDataHolder : public NameObj {
public:
    StageDataHolder(const char* pStageName, int zoneID, bool loadCommon);

    virtual void init(const TableIter& rIter);

    void initAfterScenarioSelected();
    void requestFileLoadCommon();
    void requestFileLoadScenario();
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

    MR::AssignableArray< Table > mPlacementObjs;
    MR::AssignableArray< Table > mStartObjs;
    MR::AssignableArray< Table > mGeneralPosObjs;
    MR::AssignableArray< Table > mChildObjs;
    MR::AssignableArray< Table > mListObjs;
    MR::AssignableArray< Table > mPathObjs;
    Table* mObjNameTbl;
    s32 mStageDataHolderCount;
    MR::GrowTable< StageDataHolder*, 0x18 > mStageDataArray;
    AssetFolder* mArchive;
    const char* _A8;
    Pos3f mPlacementMtx;
    s32 mZoneID;
    u8 _E0;
    u8 _E1;
    u8 _E2;
    u8 _E3;
    MR::AssignableArray< Table > _EC;
    MR::AssignableArray< Table > _F4;
    PlacementInfoOrdered* _FC;
    PlacementInfoOrdered* _100;
    PlacementInfoOrdered* _104;
    PlacementInfoOrdered* _108;
    PlacementInfoOrdered* _10C;
    unsigned mPlacementStage = 0;
};

namespace MR {
    StageDataHolder* getStageDataHolder();
};  // namespace MR
