#include "Game/System/NebulaMods.hpp"
#include <nebula_game_api.h>
#include "Game/Util/SceneUtil.hpp"
#include "Game/NameObj/NameObjFinder.hpp"
#include "Game/Scene/PlacementStateChecker.hpp"
#include "Game/Scene/SceneNameObjMovementController.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Scene/ScenePlayingResult.hpp"
#include "Game/Scene/StageDataHolder.hpp"
#include "Game/System/GalaxyStatusAccessor.hpp"
#include "Game/System/GameDataFunction.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/System/PlacedHiddenStarScenarioTable.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/System/Arenas.hpp"
#include "Game/Util/PlacementId.hpp"
#include "Game/Util/PlacementLink.hpp"
#include "Game/Util/PlacementUtil.hpp"
#include "Game/Util/PlayerUtil.hpp"
#include "Game/Util/SequenceUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/StringUtil.hpp"

namespace {
    // Dodoryu's one-hit run in CosmosGardenGalaxy scenario 4.
    const bool cSuddenDeathDodoryu = true;
    // Hidden stars take their scenario from the game's table.
    const bool cHiddenStarTable = true;
    const PlacementId cInitializeStartIdInfo(0, 0);

    void getRailInfoFromRailId(TableIter* pIter, const Table** ppInfo, const StageDataHolder* pHolder, int railId) {
        *pIter = pHolder->getCommonPathPointInfo(ppInfo, railId);
    }

    ScenePlayingResult* getScenePlayingResult() {
        return MR::getSceneObj< ScenePlayingResult >(SceneObj_ScenePlayingResult);
    }

    bool isInitializeState(SceneInitializeState state) {
        return SingletonHolder< GameSystem >::get()->mSceneController->isSceneInitializeState(state);
    }
};  // namespace

namespace MR {
    s32 getCurrentScenarioNo() {
        return SingletonHolder< GameSystem >::get()->mSceneController->getCurrentScenarioNo();
    }

    s32 getCurrentSelectedScenarioNo() {
        return SingletonHolder< GameSystem >::get()->mSceneController->getCurrentSelectedScenarioNo();
    }

    void setCurrentScenarioNo(s32 scenarioNo, s32 selectedScenarioNo) {
        return SingletonHolder< GameSystem >::get()->mSceneController->setCurrentScenarioNo(scenarioNo, selectedScenarioNo);
    }

    bool isScenarioDecided() {
        return SingletonHolder< GameSystem >::get()->mSceneController->isScenarioDecided();
    }

    const char* getCurrentStageName() {
        return SingletonHolder< GameSystem >::get()->mSceneController->mCurrSceneControlInfo.mStage;
    }

    bool isCurrentScene(SceneKind kind) {
        return SingletonHolder< GameSystem >::get()->mSceneController->mCurrSceneControlInfo.mScene == kind;
    }

    bool isEqualStageName(const char* pStageName) {
        const char* pStage = SingletonHolder< GameSystem >::get()->mSceneController->mCurrSceneControlInfo.mStage;

        if (pStage == nullptr) {
            return false;
        }

        // What a mod says of the stage: every rule the game keeps by a stage's name asks here.
        const s32 ruled = NebulaMods::stageRule(pStage, pStageName);
        if (ruled >= 0) {
            return ruled != 0;
        }

        return isEqualStringCase(pStage, pStageName);
    }

    bool isStageBeginPrologueEvent() {
        return isEqualStageName("PeachCastleGardenGalaxy");
    }

    bool isStageBeginFadeWipe() {
        return isEqualStageName("HeavensDoorGalaxy");
    }

    bool isStageBeginTitleWipe() {
        return isStageFileSelect();
    }

    bool isStageBeginWithoutWipe() {
        return isStageEpilogueDemo();
    }

    bool isStageDisablePauseMenu() {
        bool disabled = false;
        if (isStageFileSelect() || isStageEpilogueDemo()) {
            disabled = true;
        }

        return disabled;
    }

    bool isStageAstroLocation() {
        return isEqualStageName("AstroGalaxy") || isEqualStageName("AstroDome") || isEqualStageName("LibraryRoom");
    }

    bool isStageSwimAngleLimit() {
        return isEqualStageName("OceanRingGalaxy");
    }

    bool isStageStarPieceFollowGroupLimit() {
        return isEqualStageName("EggStarGalaxy") && getCurrentScenarioNo() == 2;
    }

    bool isStageFileSelect() {
        return isEqualStageName("FileSelect");
    }

    static bool isStageKoopaVs1Or2() {
        return isEqualStageName("KoopaBattleVs1Galaxy") || isEqualStageName("KoopaBattleVs2Galaxy");
    }

    bool isStageKoopaVs() {
        return isStageKoopaVs1Or2() || isStageKoopaVs3();
    }

    bool isStageKoopaVs3() {
        return isEqualStageName("KoopaBattleVs3Galaxy");
    }

    bool isStageEpilogueDemo() {
        return isEqualStageName("EpilogueDemoStage");
    }

    bool isBeginScenarioStarter() {
        if (hasRetryGalaxySequence()) {
            return false;
        }

        return NameObjFinder::find("シナリオスターター");
    }

    bool isStageSuddenDeathDodoryu() {
        if (!::cSuddenDeathDodoryu) {
            return false;
        }
        return isEqualStageName("CosmosGardenGalaxy") && getCurrentScenarioNo() == 4;
    }

    void setInitializeStatePlacementPlayer() {
        SingletonHolder< GameSystem >::get()->mSceneController->setSceneInitializeState(SceneInitializeState_PlacementPlayer);
    }

    void setInitializeStatePlacementHighPriority() {
        SingletonHolder< GameSystem >::get()->mSceneController->setSceneInitializeState(SceneInitializeState_PlacementHighPriority);
    }

    void setInitializeStatePlacement() {
        SingletonHolder< GameSystem >::get()->mSceneController->setSceneInitializeState(SceneInitializeState_Placement);
    }

    void setInitializeStateAfterPlacement() {
        SingletonHolder< GameSystem >::get()->mSceneController->setSceneInitializeState(SceneInitializeState_AfterPlacement);
    }

    bool isInitializeStateEnd() {
        return SingletonHolder< GameSystem >::get()->mSceneController->isSceneInitializeState(SceneInitializeState_End);
    }

    bool isInitializeStatePlacementSomething() {
        return (::isInitializeState(SceneInitializeState_PlacementPlayer) || ::isInitializeState(SceneInitializeState_PlacementHighPriority)) ||
               ::isInitializeState(SceneInitializeState_Placement);
    }

    void stopSceneForScenarioOpeningCamera() {
        getSceneNameObjMovementController()->requestStopSceneFor(MovementControlType_StopPlayer, nullptr);
    }

    void playSceneForScenarioOpeningCamera() {
        getSceneNameObjMovementController()->requestPlaySceneFor(MovementControlType_StopPlayer, nullptr);
    }

    const PlacementId& getCurrentMarioStartIdInfo() {
        return *SingletonHolder< GameSystem >::get()->mSceneController->mCurrSceneControlInfo.mStartIdInfo;
    }

    s32 getStartPosNum() {
        return getStageDataHolder()->getStartPosNum();
    }

    bool getMarioStartPose(s32 marioNo, Vec3f* pTrans, Vec3f* pRotate) {
        const TableIter iter = getStageDataHolder()->makeMarioTableIter(PlacementId(marioNo, 0));
        if (!iter.isValid()) {
            return false;
        }
        MR::getPlacementTrans(iter, pTrans);
        MR::getPlacementRotate(iter, pRotate);
        return true;
    }

    s32 getCurrentStartZoneId() {
        return getStageDataHolder()->getCurrentStartZoneId();
    }

    const PlacementId& getInitializeStartIdInfo() {
        return ::cInitializeStartIdInfo;
    }

    AssetFolder* getStageArchive(const char* pStageName) {
        // A zone's archive lives with the stage's files, also on the file select stage.
        return getArchive(MR::FormattedName("/StageData/%s", pStageName).c_str(), Arenas::sceneFiles());
    }

    s32 getGeneralPosNum() {
        return getStageDataHolder()->getGeneralPosNum();
    }

    void getGeneralPosData(const char** ppName, Vec3f* pPosition, Vec3f* pRotation, PlacementLink** ppLinkInfo, int index) {
        TableIter iter = getStageDataHolder()->getGeneralPosInfoFromDataIndex(index);
        iter.getValue("PosName", ppName);
        getPlacementTrans(iter, pPosition);
        getPlacementRotate(iter, pRotation);
        *ppLinkInfo = new PlacementLink(iter, false);
    }

    s32 getChildObjNum(const TableIter& rIter) {
        return getStageDataHolder()->findPlacedStageDataHolder(rIter)->getChildObjNum(rIter);
    }

    void getChildObjName(const char** ppName, const TableIter& rIter, int index) {
        TableIter childIter = getStageDataHolder()->findPlacedStageDataHolder(rIter)->getChildObjInfoFromDataIndex(rIter, index);
        getObjectName(ppName, childIter);
    }

    void initChildObj(NameObj* pObj, const TableIter& rIter, int index) {
        TableIter childIter = getStageDataHolder()->findPlacedStageDataHolder(rIter)->getChildObjInfoFromDataIndex(rIter, index);
        pObj->init(childIter);
    }

    const char* getAppearPowerStarObjName(s32 scenarioNo) {
        return makeCurrentGalaxyStatusAccessor().getAppearPowerStarObjName(scenarioNo);
    }

    s32 getCurrentStageNormalScenarioNum() {
        return makeCurrentGalaxyStatusAccessor().getNormalScenarioNum();
    }

    s32 getCurrentStagePowerStarNum() {
        return makeCurrentGalaxyStatusAccessor().getPowerStarNum();
    }

    s32 getZoneNum() {
        return makeCurrentGalaxyStatusAccessor().getZoneNum();
    }

    const char* getZoneNameFromZoneId(s32 zoneId) {
        return makeCurrentGalaxyStatusAccessor().getZoneName(zoneId);
    }

    s32 getPlacedHiddenStarScenarioNo(const char* pStageName, s32 starId) {
        GalaxyStatusAccessor accessor = makeGalaxyStatusAccessor(pStageName);
        s32 scenarioNo = ::cHiddenStarTable ? getPlacedHiddenStarScenarioNoFromTable(pStageName, starId) : 0;
        if (scenarioNo > 0) {
            return scenarioNo;
        }

        // A star id outside the 32-bit active-star mask (a placement with a bad id) falls back to scenario 1.
        if (starId < 1 || starId > 32) {
            return 1;
        }

        s32 mask;
        s32 i = 1;
        mask = 1 << (starId - 1);
        for (; i <= accessor.getNormalScenarioNum(); i++) {
            u32 activeStars = accessor.getActivePowerStarId(i);
            if (activeStars & mask) {
                return i;
            }
        }

        return 1;
    }

    void getRailInfo(TableIter* pRailIter, const Table** ppPointInfo, const TableIter& rIter) {
        s32 railId = -1;
        getRailId(rIter, &railId);
        ::getRailInfoFromRailId(pRailIter, ppPointInfo, getStageDataHolder()->findPlacedStageDataHolder(rIter), railId);
    }

    bool getNextLinkRailInfo(TableIter* pRailIter, const Table** ppPointInfo, const TableIter& rIter) {
        // A rail row with no link column has no next rail.
        s32 railId = -1;
        getNextLinkRailID(rIter, &railId);
        if (railId == -1) {
            return false;
        }

        const StageDataHolder* pHolder = getStageDataHolder()->findPlacedStageDataHolder(rIter);
        ::getRailInfoFromRailId(pRailIter, ppPointInfo, pHolder, railId);
        return true;
    }

    s32 getCurrentStartCameraId() {
        return getStageDataHolder()->getCurrentStartCameraId();
    }

    void getStartCameraIdInfoFromStartDataIndex(PlacementId* pInfo, int index) {
        getStageDataHolder()->getStartCameraIdInfoFromStartDataIndex(pInfo, index);
    }

    s32 getPlacedRailNum(s32 zoneId) {
        if (getStageDataHolder()->isPlacedZone(zoneId)) {
            return getStageDataHolder()->getStageDataHolderFromZoneId(zoneId)->getCommonPathInfoElementNum();
        }

        return 0;
    }

    void getCameraRailInfo(TableIter* pRailIter, const Table** ppPointInfo, s32 railId, s32 zoneId) {
        ::getRailInfoFromRailId(pRailIter, ppPointInfo, getStageDataHolder()->getStageDataHolderFromZoneId(zoneId), railId);
    }

    bool getCameraRailInfoFromRailDataIndex(TableIter* pRailIter, const Table** ppPointInfo, int index, s32 zoneId) {
        *pRailIter = getStageDataHolder()->getStageDataHolderFromZoneId(zoneId)->getCommonPathPointInfoFromRailDataIndex(ppPointInfo, index);
        return isEqualRailUsage(*pRailIter, "Camera");
    }

    void getStageCameraData(void** ppData, s32* pSize, s32 zoneId) {
        if (!getStageDataHolder()->isPlacedZone(zoneId)) {
            *ppData = nullptr;
            *pSize = 0;
            return;
        }

        StageDataHolder* pHolder = getStageDataHolder()->getStageDataHolderFromZoneId(zoneId);
        *ppData = pHolder->getStageArchiveResource("CameraParam.bcam");
        *pSize = pHolder->getStageArchiveResourceSize(*ppData);
    }

    void getCurrentScenarioStartAnimCameraData(void** ppData, s32* pSize) {
        StageDataHolder* pHolder = getStageDataHolder();
        *ppData = pHolder->getStageArchiveResource(MR::FormattedName("StartScenario%d.canm", getCurrentScenarioNo()).c_str());
        if (*ppData != nullptr) {
            *pSize = pHolder->getStageArchiveResourceSize(*ppData);
        } else {
            *pSize = 0;
        }
    }

    void incCoin(int term) {
        term = NebulaMods::fire(NEBULA_MOD_EVENT_COIN, term);
        ::getScenePlayingResult()->incCoin(term);
        incPlayerLife(term);
    }

    bool payCoin(s32 num) {
        return ::getScenePlayingResult()->payCoin(num);
    }

    void incPurpleCoin() {
        ::getScenePlayingResult()->incPurpleCoin();
    }

    s32 getCoinNum() {
        return ::getScenePlayingResult()->getCoinNum();
    }

    s32 getPurpleCoinNum() {
        return ::getScenePlayingResult()->mPurpleCoinNum;
    }

    s32 getPowerStarNum() {
        return GameDataFunction::calcCurrentPowerStarNum();
    }

    bool isPlacementLocalStage() {
        return getStageDataHolder()->mStageDataHolderCount > 0;
    }

    s32 getPlacedZoneId(const TableIter& rIter) {
        return getStageDataHolder()->findPlacedStageDataHolder(rIter)->mZoneID;
    }

    const Pos3f* getZonePlacementMtx(const TableIter& rIter) {
        return &getStageDataHolder()->findPlacedStageDataHolder(rIter)->mPlacementMtx;
    }

    const Pos3f* getZonePlacementMtx(s32 zoneId) {
        return &getStageDataHolder()->getStageDataHolderFromZoneId(zoneId)->mPlacementMtx;
    }

    const char* getJapaneseObjectName(const char* pName) {
        const char* pJapaneseName = getStageDataHolder()->getJapaneseObjectName(pName);
        if (pJapaneseName != nullptr) {
            return pJapaneseName;
        }

        return pName;
    }

    void setCurrentPlacementZoneId(s32 zoneId) {
        getPlacementStateChecker()->setCurrentPlacementZoneId(zoneId);
    }

    void clearCurrentPlacementZoneId() {
        getPlacementStateChecker()->clearCurrentPlacementZoneId();
    }

    s32 getCurrentPlacementZoneId() {
        return getPlacementStateChecker()->getCurrentPlacementZoneId();
    }

    const char* getCurrentPlacementZoneName() {
        return getZoneNameFromZoneId(getCurrentPlacementZoneId());
    }
};  // namespace MR
