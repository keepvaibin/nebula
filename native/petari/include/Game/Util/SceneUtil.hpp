#pragma once

#include "Game/Scene/SceneFactory.hpp"
#include <nebula_geometry.hpp>


class AssetFolder;
class PlacementId;
class Table;
class TableIter;
class PlacementLink;
class NameObj;

namespace MR {
    s32 getCurrentScenarioNo();
    s32 getCurrentSelectedScenarioNo();
    void setCurrentScenarioNo(s32, s32);
    bool isScenarioDecided();
    const char* getCurrentStageName();
    bool isCurrentScene(SceneKind);
    bool isEqualStageName(const char*);
    bool isStageBeginPrologueEvent();
    bool isStageBeginFadeWipe();
    bool isStageBeginTitleWipe();
    bool isStageBeginWithoutWipe();
    bool isStageDisablePauseMenu();
    bool isStageAstroLocation();
    bool isStageSwimAngleLimit();
    bool isStageStarPieceFollowGroupLimit();
    bool isStageFileSelect();
    bool isStageKoopaVs();
    bool isStageKoopaVs3();
    bool isStageEpilogueDemo();
    bool isBeginScenarioStarter();
    bool isStageSuddenDeathDodoryu();
    void setInitializeStatePlacementPlayer();
    void setInitializeStatePlacementHighPriority();
    void setInitializeStatePlacement();
    void setInitializeStateAfterPlacement();
    bool isInitializeStateEnd();
    bool isInitializeStatePlacementSomething();
    void stopSceneForScenarioOpeningCamera();
    void playSceneForScenarioOpeningCamera();
    const PlacementId& getCurrentMarioStartIdInfo();
    s32 getStartPosNum();
    // A start of the main zone (its MarioNo): where Mario arrives and the way he faces (degrees); false if it has none.
    bool getMarioStartPose(s32 marioNo, Vec3f* pTrans, Vec3f* pRotate);
    s32 getCurrentStartZoneId();
    const PlacementId& getInitializeStartIdInfo();
    AssetFolder* getStageArchive(const char*);
    s32 getGeneralPosNum();
    void getGeneralPosData(const char**, Vec3f*, Vec3f*, PlacementLink**, int);
    s32 getChildObjNum(const TableIter&);
    void getChildObjName(const char**, const TableIter&, int);
    void initChildObj(NameObj*, const TableIter&, int);
    const char* getAppearPowerStarObjName(s32);
    s32 getCurrentStageNormalScenarioNum();
    s32 getCurrentStagePowerStarNum();
    s32 getZoneNum();
    const char* getZoneNameFromZoneId(s32);
    s32 getPlacedHiddenStarScenarioNo(const char*, s32);
    void getRailInfo(TableIter*, const Table**, const TableIter&);
    bool getNextLinkRailInfo(TableIter*, const Table**, const TableIter&);
    s32 getCurrentStartCameraId();
    void getStartCameraIdInfoFromStartDataIndex(PlacementId*, int);
    s32 getPlacedRailNum(s32);
    void getCameraRailInfo(TableIter*, const Table**, s32 railId, s32 zoneId);
    bool getCameraRailInfoFromRailDataIndex(TableIter*, const Table**, int, s32);
    void getStageCameraData(void**, s32*, s32);
    void getCurrentScenarioStartAnimCameraData(void**, s32*);
    void incCoin(int);
    // Spends num coins: false (nothing spent) with fewer held or num below 0. No 1-up, life or coin stat.
    bool payCoin(s32 num);
    void incPurpleCoin();
    s32 getCoinNum();
    s32 getPurpleCoinNum();
    s32 getPowerStarNum();
    bool isPlacementLocalStage();
    s32 getPlacedZoneId(const TableIter&);
    const Pos3f* getZonePlacementMtx(const TableIter&);
    const Pos3f* getZonePlacementMtx(s32);
    const char* getJapaneseObjectName(const char*);
    void setCurrentPlacementZoneId(s32);
    void clearCurrentPlacementZoneId();
    s32 getCurrentPlacementZoneId();
    const char* getCurrentPlacementZoneName();
};  // namespace MR
