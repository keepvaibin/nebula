#include <nebula_game_api.h>
extern "C" const NebulaHostServices* nebula_host(void);
#include "Game/Scene/StageDataHolder.hpp"
#include "Game/NameObj/NameObjFactory.hpp"
#include "Game/Scene/PlacementInfoOrdered.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/System/ScenarioDataParser.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/PlacementId.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/MemoryUtil.hpp"
#include "Game/Util/MtxUtil.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/StringUtil.hpp"
#include "Game/System/AssetFolder.hpp"

template TableIter Table::findElement< s32 >(const char*, s32, int) const;
template TableIter Table::findElement< const char* >(const char*, const char*, int) const;

namespace {
    const char* const cLayerDirName[33] = {"Common", "LayerA", "LayerB", "LayerC", "LayerD", "LayerE", "LayerF", "LayerG", "LayerH", "LayerI", "LayerJ", "LayerK", "LayerL", "LayerM", "LayerN", "LayerO", "LayerP", "LayerQ", "LayerR", "LayerS", "LayerT", "LayerU", "LayerV", "LayerW", "LayerX", "LayerY", "LayerZ", "LayerAA", "LayerAB", "LayerAC", "LayerAD", "LayerAE", "LayerAF"};

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
    mPlacementMtx.identity();
}

void StageDataHolder::init(const TableIter& rIter) {
    if (!mZoneID) {
        initTableData();
    }

    if (mLoadCommon) {
        u32 commonLayer = ScenarioDataFunction::getCurrentCommonLayers(mStageName);
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
    const Table* pInfo = findJmpInfoFromArray(&mPathObjs, "CommonPathInfo");
    TableIter pathIter = pInfo->findElement< s32 >("l_id", idx, 0);
    return getCommonPathPointInfoFromRailDataIndex(ppOut, pathIter.mIndex);
}

TableIter StageDataHolder::getCommonPathPointInfoFromRailDataIndex(const Table** ppInfo, int idx) const {
    const Table* pInfo = findJmpInfoFromArray(&mPathObjs, "CommonPathInfo");
    char buf[256];
    nebula_snprintf(buf, sizeof(buf), "CommonPathPointInfo.%d", idx);
    *ppInfo = findJmpInfoFromArray(&mPathObjs, buf);
    return TableIter(pInfo, idx);
}

s32 StageDataHolder::getCommonPathInfoElementNum() const {
    return findJmpInfoFromArray(&mPathObjs, "CommonPathInfo")->getNumEntries();
}

s32 StageDataHolder::getStartPosNum() const {
    s32 cur = 0;

    for (const Table* i = &mStartObjs[0]; i != mStartObjs.end(); i++) {
        cur += i->getNumEntries();
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
    bool ret = marioIter.getValue< s32 >("Camera_id", &cameraID);

    if (ret) {
        return cameraID;
    }

    return -1;
}

void StageDataHolder::getStartCameraIdInfoFromStartDataIndex(PlacementId* pInfo, int startDataIdx) const {
    TableIter startIter = getStartTableIterFromStartDataIndex(startDataIdx);
    s32 cameraID;
    startIter.getValue< s32 >("Camera_id", &cameraID);
    pInfo->initialize(cameraID, startIter);
}

s32 StageDataHolder::getGeneralPosNum() const {
    s32 count = 0;

    if (mGeneralPosObjs.size() > 0) {
        for (const Table* pInfo = &mGeneralPosObjs[0]; pInfo != mGeneralPosObjs.end(); pInfo++) {
            count += pInfo->getNumEntries();
        }
    }

    for (s32 i = 0; i < mStageDataHolderCount; i++) {
        count += mStageDataArray[i]->getGeneralPosNum();
    }

    return count;
}

TableIter StageDataHolder::getGeneralPosInfoFromDataIndex(int idx_) const {
    int idx = idx_;

    for (Table* pInfo = mGeneralPosObjs.mArr; pInfo != mGeneralPosObjs.end(); pInfo++) {
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

    if (mChildObjs.size() > 0) {
        for (const Table* pInfo = mChildObjs.begin(); pInfo != mChildObjs.end(); pInfo++) {
            for (s32 i = 0; i < pInfo->getNumEntries(); i++) {
                s32 parentID;
                pInfo->getValue(i, "ParentID", &parentID);
                if (parentID == linkID) {
                    count++;
                }
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
        for (s32 i = 0; i < pInfo->getNumEntries(); i++) {
            s32 parentID;
            pInfo->getValue(i, "ParentID", &parentID);
            if (parentID == linkID) {
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
    const TableIter englishName = mObjNameTbl->findElement< const char* >("en_name", pName, 0);

    if (englishName == mObjNameTbl->end()) {
        return nullptr;
    }

    const char* japaneseName;
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
    const StageDataHolder* hldr = getStageDataHolderFromZoneId(rInfo.mZoneID);
    s32 id = rInfo.mId;
    const Table* i = hldr->mStartObjs.begin();

    while (i != hldr->mStartObjs.end()) {
        TableIter iter = i->findElement< s32 >("MarioNo", id, 0);
        bool res = !(iter == i->end());

        if (res) {
            return iter;
        }

        i++;
    }

    return TableIter();
}

TableIter StageDataHolder::makeCurrentMarioTableIter() const {
    PlacementId startID = MR::getCurrentMarioStartIdInfo();
    TableIter iter = makeMarioTableIter(startID);

    if (iter.isValid()) {
        return iter;
    }

    return TableIter();
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
    s32 count = 0;
    u32 mask = ::getAllLayers(mStageName, MR::getCurrentScenarioNo());
    MR::FormattedName buf;

    for (u32 i = 0; i < 33; i++) {
        if ((mask & (1 << i)) != 0) {
            buf.format("%s/%s", pName, ::cLayerDirName[i]);
            count += mArchive->countFile(buf.c_str());
        }
    }

    if (count != 0) {
        Table* pInfoArray = new Table[count];
        pInfo->mMaxSize = count;
        pInfo->mArr = pInfoArray;

        for (u32 i = 0; i < 33; i++) {
            if ((mask & (1 << i)) != 0) {
                buf.format("%s/%s", pName, ::cLayerDirName[i]);
                pInfoArray = attachJmpInfoToArray(pInfoArray, buf.c_str());
            }
        }
    }
}

void StageDataHolder::initAllLayerJmpInfo(MR::AssignableArray< Table >* pInfo, const char* pPlacementDir, const char* pMapPartsDir) {
    u32 mask = ::getAllLayers(mStageName, MR::getCurrentScenarioNo());
    initLayerJmpInfo(pInfo, pPlacementDir, pMapPartsDir, mask);
}

void StageDataHolder::initLayerJmpInfo(MR::AssignableArray< Table >* pInfo, const char* pPlacementDir, const char* pMapPartsDir, u32 layerMask) {
    s32 count = 0;
    MR::FormattedName path;

    for (u32 i = 0; i < 33; i++) {
        if (layerMask & (1 << i)) {
            path.format("%s/%s", pPlacementDir, ::cLayerDirName[i]);
            count += mArchive->countFile(path.c_str());
            path.format("%s/%s", pMapPartsDir, ::cLayerDirName[i]);
            count += mArchive->countFile(path.c_str());
        }
    }

    if (count != 0) {
        Table* pCurrent = new Table[count];
        pInfo->mMaxSize = count;
        pInfo->mArr = pCurrent;
        for (u32 i = 0; i < 33; i++) {
            if (layerMask & (1 << i)) {
                path.format("%s/%s", pPlacementDir, ::cLayerDirName[i]);
                pCurrent = attachJmpInfoToArray(pCurrent, path.c_str());
                path.format("%s/%s", pMapPartsDir, ::cLayerDirName[i]);
                pCurrent = attachJmpInfoToArray(pCurrent, path.c_str());
            }
        }
    }
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

TableIter StageDataHolder::getStartTableIterFromStartDataIndex(int idx_) const {
    int idx = idx_;

    for (Table* pInfo = mStartObjs.mArr; pInfo != mStartObjs.end(); pInfo++) {
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
