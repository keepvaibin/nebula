#include <nebula_game_api.h>
extern "C" const NebulaHostServices* nebula_host(void);
#include "Game/Scene/StageDataHolder.hpp"
#include "Game/NameObj/NameObjFactory.hpp"
#include "Game/Scene/PlacementInfoOrdered.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/System/LumaMake.hpp"
#include "Game/System/ScenarioDataParser.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/PlacementId.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/AssetFolder.hpp"
#include "Game/System/LumaCore.hpp"

namespace {
    // One per bit of a layer mask.
    const char* const cLayerDirName[32] = {"Common", "LayerA", "LayerB", "LayerC", "LayerD", "LayerE", "LayerF", "LayerG", "LayerH", "LayerI", "LayerJ", "LayerK", "LayerL", "LayerM", "LayerN", "LayerO", "LayerP", "LayerQ", "LayerR", "LayerS", "LayerT", "LayerU", "LayerV", "LayerW", "LayerX", "LayerY", "LayerZ", "LayerAA", "LayerAB", "LayerAC", "LayerAD", "LayerAE"};

    static bool isPrioPlacementObjInfo(const char* pName) {
        return MR::isEqualStringCase(pName, "AreaObjInfo") || MR::isEqualStringCase(pName, "PlanetObjInfo") ||
               MR::isEqualStringCase(pName, "DemoObjInfo") || MR::isEqualStringCase(pName, "CameraCubeInfo");
    }

    u32 getAllLayers(const char* pStageName, s32 scenarioNo) {
        u32 commonLayers = ScenarioDataFunction::getCurrentCommonLayers(pStageName);
        u32 scenarioLayers = ScenarioDataFunction::getCurrentScenarioLayers(pStageName, scenarioNo);
        return commonLayers | scenarioLayers;
    }

    static void calcPlacementInfoNum(int* pPriorityCount, int* pNormalCount, int* pAfterScenarioCount, const MR::AssignableArray< Table >& rArray) {
        *pPriorityCount = 0;
        *pNormalCount = 0;
        if (pAfterScenarioCount != nullptr) {
            *pAfterScenarioCount = 0;
        }

        for (const Table* pInfo = rArray.begin(); pInfo != rArray.end(); pInfo++) {
            if (::isPrioPlacementObjInfo(pInfo->getName())) {
                *pPriorityCount += pInfo->getNumEntries();
            } else {
                *pNormalCount += pInfo->getNumEntries();
                if (pAfterScenarioCount != nullptr) {
                    *pAfterScenarioCount += PlacementInfoOrdered::countAfterScenario(pInfo);
                }
            }
        }
    }

    // Each placement table of a zone with its layer (cLayerDirName's index), in the order initLayerJmpInfo attached
    // them; `each` returning true stops the walk (and the walk returns true).
    template < class Each >
    bool eachPlacementTable(AssetFolder* pArchive, const MR::AssignableArray< Table >& rArray, u32 layerMask, Each each) {
        const char* const dirs[] = {"/jmp/Placement", "/jmp/MapParts"};
        const Table* pTable = rArray.begin();
        MR::FormattedName path;
        for (u32 i = 0; i < ARRAY_SIZEU(::cLayerDirName); i++) {
            if ((layerMask & (1u << i)) == 0) {
                continue;
            }
            for (const char* const pDir : dirs) {
                path.format("%s/%s", pDir, ::cLayerDirName[i]);
                const s32 count = pArchive->countFile(path.c_str());
                for (s32 k = 0; k < count && pTable != rArray.end(); k++, pTable++) {
                    if (each(i, pTable)) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    static void attachJmpInfoToPlacementInfoOrdered(PlacementInfoOrdered* pPriorityInfo, PlacementInfoOrdered* pNormalInfo,
                                                    PlacementInfoOrdered* pDeferredInfo, const MR::AssignableArray< Table >& rArray) {
        for (const Table* pInfo = rArray.begin(); pInfo != rArray.end(); pInfo++) {
            if (::isPrioPlacementObjInfo(pInfo->getName())) {
                pPriorityInfo->attach(pInfo, nullptr);
            } else {
                pNormalInfo->attach(pInfo, pDeferredInfo);
            }
        }
    }
};  // namespace

StageDataHolder::StageDataHolder(const char* pStageName, int zoneID, bool loadCommon)
    : NameObj("StageDataHolder"), mObjNameTbl(), mStageDataHolderCount(), mArchive(MR::getStageArchive(pStageName)), mStageName(pStageName), mZoneID(zoneID),
      mLoadCommon(loadCommon), mCommonPriorityOrder(), mCommonNormalOrder(), mScenarioPriorityOrder(), mScenarioNormalOrder(), mAfterScenarioOrder() {
    lumacore::setClass(this, lumacore::kClassId< StageDataHolder >);
    mPlacementMtx.identity();
}

void StageDataHolder::init(const TableIter& /*rIter*/) {
    if (!mZoneID) {
        initTableData();
    }

    if (mLoadCommon) {
        u32 commonLayer = ScenarioDataFunction::getCurrentCommonLayers(mStageName);
        mCommonLayerMask = commonLayer;
        initLayerJmpInfo(&mCommonPlacements, "/jmp/Placement", "/jmp/MapParts", commonLayer);
    }

    createLocalStageDataHolder(mCommonPlacements, true);

    if (!mZoneID) {
        initPlacementInfoOrderedCommon();
    }
}

void StageDataHolder::initAfterScenarioSelected() {
    s32 curScenarioNo = MR::getCurrentScenarioNo();
    u32 curLayers = ScenarioDataFunction::getCurrentScenarioLayers(mStageName, curScenarioNo);

    if (!mLoadCommon) {
        curLayers |= ScenarioDataFunction::getCurrentCommonLayers(mStageName);
    }

    mScenarioLayerMask = curLayers;
    initLayerJmpInfo(&mScenarioPlacements, "/jmp/Placement", "/jmp/MapParts", curLayers);
    createLocalStageDataHolder(mScenarioPlacements, false);

    for (s32 i = 0; i < mStageDataHolderCount; i++) {
        mStageDataArray[i]->initAfterScenarioSelected();
    }

    if (!mZoneID) {
        initPlacementInfoOrderedScenario();
    }

    initAllLayerJmpInfo(&mPlacementObjs, "/jmp/Placement", "/jmp/MapParts");
    initAllLayerJmpInfo(&mStartObjs, "/jmp/Start");

    if (mArchive->countFile("/jmp/GeneralPos") > 0) {
        initAllLayerJmpInfo(&mGeneralPosObjs, "/jmp/GeneralPos");
    }

    if (mArchive->countFile("/jmp/ChildObj") > 0) {
        initAllLayerJmpInfo(&mChildObjs, "/jmp/ChildObj");
    }

    initJmpInfo(&mListObjs, "/jmp/List");
    initJmpInfo(&mPathObjs, "/jmp/Path");
}

void StageDataHolder::initPlacement() {
    while (!initPlacementStep(UINT64_MAX)) {}
}

bool StageDataHolder::initPlacementStep(uint64_t deadline) {
    while (mPlacementStage < 6u) {
        if (mPlacementStage == 0u) {
            MR::setInitializeStatePlacementPlayer();
            initPlacementMario();
        } else {
            PlacementInfoOrdered* lists[] = {mCommonPriorityOrder, mScenarioPriorityOrder, mCommonNormalOrder, mScenarioNormalOrder, mAfterScenarioOrder};
            if (mPlacementStage <= 2u) MR::setInitializeStatePlacementHighPriority();
            else MR::setInitializeStatePlacement();
            if (!lists[mPlacementStage - 1u]->initPlacementStep(deadline)) return false;
        }
        ++mPlacementStage;
        if (nebula_host()->clock_ns(nebula_host()->user) >= deadline) return mPlacementStage == 6u;
    }
    return true;
}

TableIter StageDataHolder::getCommonPathPointInfo(const Table** ppOut, int idx) const {
    // A zone with no rail table, or no rail of that id: no points (*ppOut null), never a read through the table.
    const Table* pInfo = findJmpInfoFromArray(&mPathObjs, "CommonPathInfo");
    if (pInfo == nullptr) {
        *ppOut = nullptr;
        return TableIter();
    }
    TableIter pathIter = pInfo->findElement< s32 >("l_id", idx, 0);
    return getCommonPathPointInfoFromRailDataIndex(ppOut, pathIter.mIndex);
}

TableIter StageDataHolder::getCommonPathPointInfoFromRailDataIndex(const Table** ppInfo, int idx) const {
    const Table* pInfo = findJmpInfoFromArray(&mPathObjs, "CommonPathInfo");
    if (pInfo == nullptr || idx < 0 || idx >= pInfo->getNumEntries()) {
        *ppInfo = nullptr;
        return TableIter();
    }
    *ppInfo = findJmpInfoFromArray(&mPathObjs, MR::FormattedName("CommonPathPointInfo.%d", idx).c_str());
    return TableIter(pInfo, idx);
}

s32 StageDataHolder::getCommonPathInfoElementNum() const {
    // A zone with no rail table (a mod's new stage that has no rails) has none.
    const Table* const pInfo = findJmpInfoFromArray(&mPathObjs, "CommonPathInfo");
    return pInfo != nullptr ? pInfo->getNumEntries() : 0;
}

s32 StageDataHolder::getStartPosNum() const {
    s32 cur = 0;

    for (const Table* pInfo = mStartObjs.begin(); pInfo != mStartObjs.end(); pInfo++) {
        cur += pInfo->getNumEntries();
    }

    for (s32 i = 0; i < mStageDataHolderCount; i++) {
        cur += mStageDataArray[i]->getStartPosNum();
    }

    return cur;
}

s32 StageDataHolder::getCurrentStartZoneId() const {
    return findPlacedStageDataHolder(makeCurrentMarioTableIter())->mZoneID;
}

s32 StageDataHolder::getCurrentStartCameraId() const {
    TableIter marioIter = makeCurrentMarioTableIter();
    s32 cameraID;

    if (marioIter.getValue< s32 >("Camera_id", &cameraID)) {
        return cameraID;
    }

    return -1;
}

void StageDataHolder::getStartCameraIdInfoFromStartDataIndex(PlacementId* pInfo, int startDataIdx) const {
    TableIter startIter = getStartTableIterFromStartDataIndex(startDataIdx);
    s32 cameraID = -1;
    startIter.getValue< s32 >("Camera_id", &cameraID);
    pInfo->initialize(cameraID, startIter);
}

s32 StageDataHolder::getGeneralPosNum() const {
    s32 count = 0;

    for (const Table* pInfo = mGeneralPosObjs.begin(); pInfo != mGeneralPosObjs.end(); pInfo++) {
        count += pInfo->getNumEntries();
    }

    for (s32 i = 0; i < mStageDataHolderCount; i++) {
        count += mStageDataArray[i]->getGeneralPosNum();
    }

    return count;
}

TableIter StageDataHolder::getGeneralPosInfoFromDataIndex(int idx) const {
    for (const Table* pInfo = mGeneralPosObjs.begin(); pInfo != mGeneralPosObjs.end(); pInfo++) {
        if (idx < pInfo->getNumEntries()) {
            return TableIter(pInfo, idx);
        }

        idx -= pInfo->getNumEntries();
    }

    for (s32 i = 0; i < mStageDataHolderCount; i++) {
        StageDataHolder* pHolder = mStageDataArray[i];
        int startPosNum = pHolder->getGeneralPosNum();

        if (idx < startPosNum) {
            return pHolder->getGeneralPosInfoFromDataIndex(idx);
        }

        idx -= startPosNum;
    }

    return TableIter();
}

s32 StageDataHolder::getChildObjNum(const TableIter& rIter) const {
    s32 linkID;
    s32 count = 0;
    MR::getPlacementLinkID(rIter, &linkID);

    // A table without the column has no children.
    for (const Table* pInfo = mChildObjs.begin(); pInfo != mChildObjs.end(); pInfo++) {
        const s32 column = pInfo->searchItemInfo("ParentID");
        if (column < 0) {
            continue;
        }
        for (s32 i = 0; i < pInfo->getNumEntries(); i++) {
            s32 parentID;
            if (pInfo->getValueFast(i, column, &parentID) && parentID == linkID) {
                count++;
            }
        }
    }

    return count;
}

TableIter StageDataHolder::getChildObjInfoFromDataIndex(const TableIter& rIter, int index) const {
    s32 linkID;
    s32 count = 0;
    MR::getPlacementLinkID(rIter, &linkID);

    for (const Table* pInfo = mChildObjs.begin(); pInfo != mChildObjs.end(); pInfo++) {
        const s32 column = pInfo->searchItemInfo("ParentID");
        if (column < 0) {
            continue;
        }
        for (s32 i = 0; i < pInfo->getNumEntries(); i++) {
            s32 parentID;
            if (pInfo->getValueFast(i, column, &parentID) && parentID == linkID) {
                if (count == index) {
                    return TableIter(pInfo, i);
                }

                count++;
            }
        }
    }

    return TableIter();
}

const StageDataHolder* StageDataHolder::findPlacedStageDataHolder(const TableIter& rIter) const {
    // Made without a placement: it stands in the main stage.
    if (rIter.mInfo == nullptr) {
        return this;
    }

    // The holder whose stage archive holds the row's table.
    const TableData* pData = rIter.mInfo->mData;
    const MR::AssignableArray< Table >* const arrays[] = {&mPlacementObjs, &mStartObjs, &mGeneralPosObjs, &mChildObjs, &mListObjs, &mPathObjs, &mCommonPlacements, &mScenarioPlacements};
    for (const MR::AssignableArray< Table >* pArray : arrays) {
        for (const Table* pInfo = pArray->begin(); pInfo != pArray->end(); pInfo++) {
            if (pInfo->mData == pData) {
                return this;
            }
        }
    }

    for (s32 i = 0; i < mStageDataHolderCount; i++) {
        const StageDataHolder* pHolder = mStageDataArray[i]->findPlacedStageDataHolder(rIter);

        if (pHolder != nullptr) {
            return pHolder;
        }
    }

    // A row LumaCore.make wrote (LumaMake.cpp): its objects stand in the main stage.
    if (mZoneID == 0 && luma::make::isMadeTable(pData)) {
        return this;
    }

    return nullptr;
}

const StageDataHolder* StageDataHolder::getStageDataHolderFromZoneId(int zoneID) const {
    if (zoneID == 0) {
        return this;
    }

    for (s32 i = 0; i < mStageDataHolderCount; i++) {
        StageDataHolder* pHolder = mStageDataArray[i];
        s32 curZoneID = pHolder->mZoneID;

        if (zoneID == curZoneID) {
            return pHolder;
        }
    }

    return nullptr;
}

StageDataHolder* StageDataHolder::getStageDataHolderFromZoneId(int zoneID) {
    return const_cast< StageDataHolder* >(static_cast< const StageDataHolder* >(this)->getStageDataHolderFromZoneId(zoneID));
}

bool StageDataHolder::isPlacedZone(int zoneID) const {
    if (!zoneID) {
        return true;
    }

    for (s32 i = 0; i < mStageDataHolderCount; i++) {
        if (zoneID == mStageDataArray[i]->mZoneID) {
            return true;
        }
    }

    return false;
}

const char* StageDataHolder::getJapaneseObjectName(const char* pName) const {
    const u32 row = mObjNameIndex.find(pName);

    if (row == MR::NameIndex::cNoValue) {
        return nullptr;
    }

    const TableIter englishName(mObjNameTbl, static_cast< s32 >(row));
    const char* japaneseName = nullptr;
    englishName.getValue< const char* >("jp_name", &japaneseName);
    return japaneseName;
}

void* StageDataHolder::getStageArchiveResource(const char* pName) {
    return mArchive->getResource(pName);
}

s32 StageDataHolder::getStageArchiveResourceSize(void* pData) {
    return mArchive->getResSize(pData);
}

void StageDataHolder::initPlacementMario() {
    TableIter iter = makeCurrentMarioTableIter();
    MR::setCurrentPlacementZoneId(MR::getPlacedZoneId(iter));
    const char* objName = "";
    MR::getObjectName(&objName, iter);
    CreationFuncPtr funcPtr = NameObjFactory::getCreator(objName);

    NameObj* obj = funcPtr("マリオアクター");
    obj->init(iter);
    MR::clearCurrentPlacementZoneId();
}

TableIter StageDataHolder::makeMarioTableIter(const PlacementId& rInfo) const {
    const StageDataHolder* pHolder = getStageDataHolderFromZoneId(rInfo.mZoneID);

    for (const Table* pTable = pHolder->mStartObjs.begin(); pTable != pHolder->mStartObjs.end(); pTable++) {
        TableIter iter = pTable->findElement< s32 >("MarioNo", rInfo.mId, 0);

        if (!(iter == pTable->end())) {
            return iter;
        }
    }

    return TableIter();
}

TableIter StageDataHolder::makeCurrentMarioTableIter() const {
    return makeMarioTableIter(MR::getCurrentMarioStartIdInfo());
}

void StageDataHolder::initJmpInfo(MR::AssignableArray< Table >* pInfo, const char* pName) {
    const u32 count = mArchive->countFile(pName);

    if (count != 0) {
        Table* pInfoArray = new Table[count];
        pInfo->mArr = pInfoArray;
        pInfo->mMaxSize = count;
        attachJmpInfoToArray(pInfoArray, pName);
    }
}

void StageDataHolder::initAllLayerJmpInfo(MR::AssignableArray< Table >* pInfo, const char* pName) {
    initLayerJmpInfo(pInfo, pName, nullptr, ::getAllLayers(mStageName, MR::getCurrentScenarioNo()));
}

void StageDataHolder::initAllLayerJmpInfo(MR::AssignableArray< Table >* pInfo, const char* pPlacementDir, const char* pMapPartsDir) {
    initLayerJmpInfo(pInfo, pPlacementDir, pMapPartsDir, ::getAllLayers(mStageName, MR::getCurrentScenarioNo()));
}

// Each set layer's folder under pPlacementDir, then under pMapPartsDir if there is one.
void StageDataHolder::initLayerJmpInfo(MR::AssignableArray< Table >* pInfo, const char* pPlacementDir, const char* pMapPartsDir, u32 layerMask) {
    const char* const dirs[] = {pPlacementDir, pMapPartsDir};
    const u32 dirNum = pMapPartsDir != nullptr ? 2 : 1;
    s32 count = 0;
    MR::FormattedName path;

    for (u32 i = 0; i < ARRAY_SIZEU(::cLayerDirName); i++) {
        if ((layerMask & (1u << i)) != 0) {
            for (u32 dir = 0; dir < dirNum; dir++) {
                path.format("%s/%s", dirs[dir], ::cLayerDirName[i]);
                count += mArchive->countFile(path.c_str());
            }
        }
    }

    if (count != 0) {
        Table* pCurrent = new Table[count];
        pInfo->mMaxSize = count;
        pInfo->mArr = pCurrent;
        for (u32 i = 0; i < ARRAY_SIZEU(::cLayerDirName); i++) {
            if ((layerMask & (1u << i)) != 0) {
                for (u32 dir = 0; dir < dirNum; dir++) {
                    path.format("%s/%s", dirs[dir], ::cLayerDirName[i]);
                    pCurrent = attachJmpInfoToArray(pCurrent, path.c_str());
                }
            }
        }
    }
}

bool StageDataHolder::findPlacementKey(const Table* pTable, s32 entry, const char** ppLayer, u32* pIndex) const {
    const MR::AssignableArray< Table >* const arrays[] = {&mCommonPlacements, &mScenarioPlacements};
    const u32 masks[] = {mCommonLayerMask, mScenarioLayerMask};
    for (u32 a = 0; a < 2; a++) {
        u32 layer = 0;
        u32 before = 0;  // the layer's rows in its tables before pTable
        const bool found = ::eachPlacementTable(mArchive, *arrays[a], masks[a], [&](u32 l, const Table* p) {
            if (l != layer) {
                layer = l;
                before = 0;
            }
            if (p == pTable) {
                return true;
            }
            before += static_cast< u32 >(p->getNumEntries());
            return false;
        });
        if (found) {
            *ppLayer = ::cLayerDirName[layer];
            *pIndex = before + static_cast< u32 >(entry);
            return true;
        }
    }
    return false;
}

bool StageDataHolder::findPlacementRow(const char* pLayer, u32 index, TableIter* pOut) const {
    const MR::AssignableArray< Table >* const arrays[] = {&mCommonPlacements, &mScenarioPlacements};
    const u32 masks[] = {mCommonLayerMask, mScenarioLayerMask};
    for (u32 a = 0; a < 2; a++) {
        u32 left = index;
        const bool found = ::eachPlacementTable(mArchive, *arrays[a], masks[a], [&](u32 l, const Table* p) {
            if (!MR::isEqualStringCase(::cLayerDirName[l], pLayer)) {
                return false;
            }
            const u32 rows = static_cast< u32 >(p->getNumEntries());
            if (left < rows) {
                *pOut = TableIter(p, static_cast< s32 >(left));
                return true;
            }
            left -= rows;
            return false;
        });
        if (found) {
            return true;
        }
    }
    return false;
}

Table* StageDataHolder::attachJmpInfoToArray(Table* pInfo, const char* pDirectory) {
    const s32 folder = mArchive->findFolder(pDirectory);
    if (folder < 0) {
        return pInfo;
    }

    AssetFolder::Item item;
    for (u32 i = 0; mArchive->getItem(folder, i, &item); i++, pInfo++) {
        pInfo->attach(item.mData);
        pInfo->setName(item.mName);
    }

    return pInfo;
}

void StageDataHolder::initTableData() {
    void* tableFile = MR::getArchive("/StageData/ObjNameTable")->getResource("ObjNameTable.tbl");

    mObjNameTbl = new Table();
    mObjNameTbl->attach(tableFile);

    // en_name -> its first row, as findElement found it.
    const s32 column = mObjNameTbl->searchItemInfo("en_name");
    if (column >= 0) {
        mObjNameIndex.reset(static_cast< u32 >(mObjNameTbl->getNumEntries()));
        for (s32 row = 0; row < mObjNameTbl->getNumEntries(); row++) {
            const char* pEnglishName;
            mObjNameTbl->getValueFast(row, column, &pEnglishName);
            mObjNameIndex.add(pEnglishName, static_cast< u32 >(row));
        }
    }
}

void StageDataHolder::initPlacementInfoOrderedCommon() {
    int priorityCount, normalCount, afterScenarioCount;
    ::calcPlacementInfoNum(&priorityCount, &normalCount, &afterScenarioCount, mCommonPlacements);

    for (s32 i = 0; i < mStageDataHolderCount; i++) {
        int localPriorityCount, localNormalCount, localAfterScenarioCount;
        ::calcPlacementInfoNum(&localPriorityCount, &localNormalCount, &localAfterScenarioCount, mStageDataArray[i]->mCommonPlacements);
        priorityCount += localPriorityCount;
        normalCount += localNormalCount;
        afterScenarioCount += localAfterScenarioCount;
    }

    mCommonPriorityOrder = new PlacementInfoOrdered(priorityCount);
    mCommonNormalOrder = new PlacementInfoOrdered(normalCount - afterScenarioCount);
    mAfterScenarioOrder = new PlacementInfoOrdered(afterScenarioCount);
    ::attachJmpInfoToPlacementInfoOrdered(mCommonPriorityOrder, mCommonNormalOrder, mAfterScenarioOrder, mCommonPlacements);

    for (s32 i = 0; i < mStageDataHolderCount; i++) {
        ::attachJmpInfoToPlacementInfoOrdered(mCommonPriorityOrder, mCommonNormalOrder, mAfterScenarioOrder, mStageDataArray[i]->mCommonPlacements);
    }

    mCommonPriorityOrder->sort();
    mCommonNormalOrder->sort();
}

void StageDataHolder::initPlacementInfoOrderedScenario() {
    int priorityCount, normalCount;
    ::calcPlacementInfoNum(&priorityCount, &normalCount, nullptr, mScenarioPlacements);

    for (s32 i = 0; i < mStageDataHolderCount; i++) {
        int localPriorityCount, localNormalCount;
        ::calcPlacementInfoNum(&localPriorityCount, &localNormalCount, nullptr, mStageDataArray[i]->mScenarioPlacements);
        priorityCount += localPriorityCount;
        normalCount += localNormalCount;
    }

    mScenarioPriorityOrder = new PlacementInfoOrdered(priorityCount);
    mScenarioNormalOrder = new PlacementInfoOrdered(normalCount);
    ::attachJmpInfoToPlacementInfoOrdered(mScenarioPriorityOrder, mScenarioNormalOrder, nullptr, mScenarioPlacements);

    for (s32 i = 0; i < mStageDataHolderCount; i++) {
        ::attachJmpInfoToPlacementInfoOrdered(mScenarioPriorityOrder, mScenarioNormalOrder, nullptr, mStageDataArray[i]->mScenarioPlacements);
    }

    mScenarioPriorityOrder->sort();
    mScenarioNormalOrder->sort();
    mAfterScenarioOrder->sort();
}

const Table* StageDataHolder::findJmpInfoFromArray(const MR::AssignableArray< Table >* pInfoArr, const char* pName) const {
    for (const Table* pInfo = pInfoArr->begin(); pInfo != pInfoArr->end(); pInfo++) {
        if (MR::isEqualStringCase(pInfo->getName(), pName)) {
            return pInfo;
        }
    }

    return nullptr;
}

TableIter StageDataHolder::getStartTableIterFromStartDataIndex(int idx) const {
    for (const Table* pInfo = mStartObjs.begin(); pInfo != mStartObjs.end(); pInfo++) {
        if (idx < pInfo->getNumEntries()) {
            return TableIter(pInfo, idx);
        }

        idx -= pInfo->getNumEntries();
    }

    for (s32 i = 0; i < mStageDataHolderCount; i++) {
        StageDataHolder* pHolder = mStageDataArray[i];
        int startPosNum = pHolder->getStartPosNum();

        if (idx < startPosNum) {
            return pHolder->getStartTableIterFromStartDataIndex(idx);
        }

        idx -= startPosNum;
    }

    return TableIter();
}

void StageDataHolder::createLocalStageDataHolder(const MR::AssignableArray< Table >& rInfoArray, bool loadCommon) {
    for (const Table* pInfo = rInfoArray.begin(); pInfo != rInfoArray.end(); pInfo++) {
        if (MR::isEqualStringCase(pInfo->getName(), "StageObjInfo")) {
            for (s32 i = 0; i < pInfo->getNumEntries(); i++) {
                TableIter iter(pInfo, i);
                const char* pStageName = "";
                MR::getObjectName(&pStageName, iter);
                s32 zoneID = MR::makeCurrentGalaxyStatusAccessor().getZoneId(pStageName);
                mStageDataArray.at(mStageDataHolderCount) = new StageDataHolder(pStageName, zoneID, loadCommon);
                mStageDataArray[mStageDataHolderCount]->initWithoutIter();
                mStageDataArray[mStageDataHolderCount]->calcPlacementMtx(iter);
                mStageDataHolderCount++;
            }
        }
    }
}

void StageDataHolder::calcPlacementMtx(const TableIter& rIter) {
    Vec3f pos;
    rIter.getValue< f32 >("pos_x", &pos.x);
    rIter.getValue< f32 >("pos_y", &pos.y);
    rIter.getValue< f32 >("pos_z", &pos.z);

    Vec3f rot;
    rIter.getValue< f32 >("dir_x", &rot.x);
    rIter.getValue< f32 >("dir_y", &rot.y);
    rIter.getValue< f32 >("dir_z", &rot.z);

    MR::makeMtxTR(mPlacementMtx, pos, rot);
}

namespace MR {
    StageDataHolder* getStageDataHolder() {
        return getSceneObj< StageDataHolder >(SceneObj_StageDataHolder);
    }
};  // namespace MR
