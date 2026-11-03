#pragma once

#include "Game/NameObj/NameObj.hpp"
#include "Game/Util/Array.hpp"
#include <JSystem/JGeometry.hpp>

class AssetFolder;
class JMapIdInfo;
class PlacementInfoOrdered;

class StageDataHolder : public NameObj {
public:
    StageDataHolder(const char* pStageName, int zoneID, bool loadCommon);

    virtual void init(const JMapInfoIter& rIter);

    void initAfterScenarioSelected();
    void requestFileLoadCommon();
    void requestFileLoadScenario();
    void initPlacement();
    JMapInfoIter getCommonPathPointInfo(const JMapInfo** ppInfo, int index) const;
    JMapInfoIter getCommonPathPointInfoFromRailDataIndex(const JMapInfo** ppInfo, int index) const;
    s32 getCommonPathInfoElementNum() const;
    s32 getStartPosNum() const;
    s32 getCurrentStartZoneId() const;
    s32 getCurrentStartCameraId() const;
    void getStartCameraIdInfoFromStartDataIndex(JMapIdInfo* pInfo, int index) const;
    s32 getGeneralPosNum() const;
    JMapInfoIter getGeneralPosInfoFromDataIndex(int index) const;
    s32 getChildObjNum(const JMapInfoIter& rIter) const;
    JMapInfoIter getChildObjInfoFromDataIndex(const JMapInfoIter& rIter, int index) const;

    JMapInfoIter makeMarioJMapInfoIter(const JMapIdInfo& rInfo) const;

    const StageDataHolder* findPlacedStageDataHolder(const JMapInfoIter& rIter) const;
    const StageDataHolder* getStageDataHolderFromZoneId(int zoneID) const;
    StageDataHolder* getStageDataHolderFromZoneId(int zoneID);
    bool isPlacedZone(int zoneID) const;
    const char* getJapaneseObjectName(const char* pName) const;
    void* getStageArchiveResource(const char* pName);
    s32 getStageArchiveResourceSize(void* pData);

    JMapInfoIter makeCurrentMarioJMapInfoIter() const;

    void initPlacementMario();

    void initTableData();

    void calcDataAddress();
    void updateDataAddress(const MR::AssignableArray< JMapInfo >* pInfoArray);

    void initPlacementInfoOrderedScenario();
    void initPlacementInfoOrderedCommon();

    void createLocalStageDataHolder(const MR::AssignableArray< JMapInfo >& rInfoArray, bool loadCommon);

    void initJmpInfo(MR::AssignableArray< JMapInfo >* pInfo, const char* pName);

    void initLayerJmpInfo(MR::AssignableArray< JMapInfo >* pInfo, const char* pPlacementDir, const char* pMapPartsDir, u32 layerMask);
    void initAllLayerJmpInfo(MR::AssignableArray< JMapInfo >* pInfo, const char* pPlacementDir, const char* pMapPartsDir);
    void initAllLayerJmpInfo(MR::AssignableArray< JMapInfo >* pInfo, const char* pName);

    const JMapInfo* findJmpInfoFromArray(const MR::AssignableArray< JMapInfo >* pInfo, const char* pName) const;
    JMapInfoIter getStartJMapInfoIterFromStartDataIndex(int index) const;

    void calcPlacementMtx(const JMapInfoIter& rIter);

    JMapInfo* attachJmpInfoToArray(JMapInfo* pInfo, const char* pDirectory);

    MR::AssignableArray< JMapInfo > mPlacementObjs;
    MR::AssignableArray< JMapInfo > mStartObjs;
    MR::AssignableArray< JMapInfo > mGeneralPosObjs;
    MR::AssignableArray< JMapInfo > mChildObjs;
    MR::AssignableArray< JMapInfo > mListObjs;
    MR::AssignableArray< JMapInfo > mPathObjs;
    JMapInfo* mObjNameTbl;
    s32 mStageDataHolderCount;
    MR::GrowTable< StageDataHolder*, 0x18 > mStageDataArray;
    AssetFolder* mArchive;
    const char* _A8;
    TPos3f mPlacementMtx;
    s32 mZoneID;
    u8 _E0;
    u8 _E1;
    u8 _E2;
    u8 _E3;
    uintptr_t _E4;
    uintptr_t _E8;
    MR::AssignableArray< JMapInfo > _EC;
    MR::AssignableArray< JMapInfo > _F4;
    PlacementInfoOrdered* _FC;
    PlacementInfoOrdered* _100;
    PlacementInfoOrdered* _104;
    PlacementInfoOrdered* _108;
    PlacementInfoOrdered* _10C;
};

namespace MR {
    StageDataHolder* getStageDataHolder();
};  // namespace MR
