#include "Game/Map/Air.hpp"
#include "Game/Render/FrameList.hpp"
#include "Game/Util/MotionSignal.hpp"
#include "Game/MapObj/SpiderThread.hpp"
#include "Game/MapObj/SpinDriverPathDrawer.hpp"
#include "Game/NameObj/NameObjExecuteHolder.hpp"
#include "Game/Scene/SceneFunction.hpp"
#include "Game/Scene/SceneNameObjMovementController.hpp"
#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/Scene/StopSceneController.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/Util.hpp"
#include "Game/Util/CameraUtil.hpp"
#include "Game/Util/DrawUtil.hpp"
#include "Game/Util/ScreenUtil.hpp"
#include "Game/Render/Model.hpp"

void SceneFunction::movementStopSceneController() {
    MR::getSceneObj< StopSceneController >(SceneObj_StopSceneController)->movement();
}

void SceneFunction::executeMovementList() {
    StopSceneController* pStopScene = MR::getSceneObj< StopSceneController >(SceneObj_StopSceneController);
    if (pStopScene->isSceneStopped()) {
        return;
    }

    MR::getSceneNameObjMovementController()->movement();
    pStopScene->movementDelayRequests();
    CategoryList::execute(MR::MovementType_Camera);
    CategoryList::execute(MR::MovementType_MirrorCamera);
    MR::setStarPointerCameraMtxAtGameScene();
    MR::loadViewMtx();
    CategoryList::execute(MR::MovementType_ClippingDirector);
    MR::executeRequirementConnectMovement();
    MR::executeRequirementConnectDraw();
    MR::executeRequirementDisconnectMovement();
    MR::executeRequirementDisconnectDraw();
    CategoryList::execute(MR::MovementType_ScreenEffect);
    CategoryList::execute(MR::MovementType_SensorHitChecker);
    CategoryList::execute(MR::MovementType_MsgSharedGroup);
    CategoryList::execute(MR::MovementType_EffectUpdateCheck);
    CategoryList::execute(MR::MovementType_TalkDirector);
    CategoryList::execute(MR::MovementType_DemoDirector);
    CategoryList::execute(MR::MovementType_ClippedMapParts);
    CategoryList::execute(MR::MovementType_Planet);
    CategoryList::execute(MR::MovementType_CollisionMapObj);
    CategoryList::execute(MR::MovementType_CollisionEnemy);
    CategoryList::execute(MR::CalcAnimType_ClippedMapParts);
    CategoryList::execute(MR::CalcAnimType_Planet);
    CategoryList::execute(MR::CalcAnimType_CollisionMapObj);
    CategoryList::execute(MR::CalcAnimType_CollisionEnemy);
    CategoryList::execute(MR::MovementType_CollisionDirector);
    CategoryList::execute(MR::MovementType_Environment);
    CategoryList::execute(MR::MovementType_MapObj);
    CategoryList::execute(MR::MovementType_MapObjDecoration);
    CategoryList::execute(MR::MovementType_NPC);
    CategoryList::execute(MR::MovementType_Ride);
    CategoryList::execute(MR::MovementType_Player);
    CategoryList::execute(MR::MovementType_PlayerDecoration);
    CategoryList::execute(MR::MovementType_Enemy);
    CategoryList::execute(MR::MovementType_EnemyDecoration);
    CategoryList::execute(MR::MovementType_Item);
    CategoryList::execute(MR::MovementType_PlayerMessenger);
    CategoryList::execute(MR::MovementType_AreaObj);
    CategoryList::execute(MR::MovementType_Layout);
    CategoryList::execute(MR::MovementType_LayoutDecoration);
    CategoryList::execute(MR::MovementType_MovieSubtitles);
    CategoryList::execute(MR::MovementType_WipeLayout);
    CategoryList::execute(MR::MovementType_Movie);
    CategoryList::execute(MR::MovementType_Sky);
    CategoryList::execute(MR::MovementType_ImageEffect);
    CategoryList::execute(MR::MovementType_AudEffectDirector);
    CategoryList::execute(MR::MovementType_AudBgmConductor);
    CategoryList::execute(MR::MovementType_AudCameraWatcher);
    CategoryList::execute(MR::MovementType_CameraCover);
    CategoryList::execute(MR::MovementType_SwitchWatcherHolder);
    MR::executeRequirementDisconnectMovement();
    MR::executeRequirementDisconnectDraw();
}

void SceneFunction::executeMovementListOnPlayingMovie() {
    MR::getSceneNameObjMovementController()->movement();
    MR::getSceneObj< StopSceneController >(SceneObj_StopSceneController)->movementDelayRequests();
    CategoryList::execute(MR::MovementType_Layout);
    CategoryList::execute(MR::MovementType_LayoutDecoration);
    CategoryList::execute(MR::MovementType_MovieSubtitles);
    CategoryList::execute(MR::MovementType_WipeLayout);
    CategoryList::execute(MR::MovementType_Movie);
}

void SceneFunction::executeCalcAnimList() {
    MR::loadViewMtx();
    CategoryList::execute(MR::CalcAnimType_Environment);
    CategoryList::execute(MR::CalcAnimType_MapObj);
    CategoryList::execute(MR::CalcAnimType_NPC);
    CategoryList::execute(MR::CalcAnimType_Ride);
    CategoryList::execute(MR::CalcAnimType_Enemy);
    CategoryList::execute(MR::CalcAnimType_Player);
    CategoryList::execute(MR::CalcAnimType_PlayerDecoration);
    CategoryList::execute(MR::CalcAnimType_MapObjDecoration);
    CategoryList::execute(MR::CalcAnimType_Item);
    CategoryList::execute(MR::CalcAnimType_Layout);
    CategoryList::execute(MR::CalcAnimType_LayoutDecoration);
    CategoryList::execute(MR::CalcAnimType_MirrorMapObj);
    CategoryList::execute(MR::MovementType_ShadowControllerHolder);
    CategoryList::execute(MR::CalcAnimType_AnimParticle);
}

void SceneFunction::executeCalcAnimListOnPlayingMovie() {
    CategoryList::execute(MR::CalcAnimType_MovieSubtitles);
}

void SceneFunction::executeCalcViewAndEntryList() {
    Mtx34f mtx;
    mtx.identity();
    NebulaMtxCopy(mtx, ModelSys::mViewMtx);
    CategoryList::entryDrawBuffer2D();
    MR::loadViewMtx();
    CategoryList::entryDrawBuffer3D();
}

void SceneFunction::executeCalcViewAndEntryList2D() {
    Mtx34f mtx;
    mtx.identity();
    NebulaMtxCopy(mtx, ModelSys::mViewMtx);
    CategoryList::entryDrawBuffer2D();
}

void SceneFunction::executeDrawBufferListNormalOpaBeforeVolumeShadow() {
    Frame::phase(MotionSignal::ScenePhase_OpaBeforeVolumeShadow);
    CategoryList::drawOpa(MR::DrawBufferType_CrystalItem);
    CategoryList::drawXlu(MR::DrawBufferType_CrystalItem);
    CategoryList::drawOpa(MR::DrawBufferType_Crystal);
    CategoryList::drawXlu(MR::DrawBufferType_Crystal);
    CategoryList::drawOpa(MR::DrawBufferType_AstroDomeSky);
    CategoryList::drawXlu(MR::DrawBufferType_AstroDomeSky);

    if (MR::isExistPriorDrawAir()) {
        CategoryList::drawOpa(MR::DrawBufferType_Sky);
        CategoryList::drawOpa(MR::DrawBufferType_Air);
        CategoryList::drawOpa(MR::DrawBufferType_Sun);
        CategoryList::drawXlu(MR::DrawBufferType_Sky);
        CategoryList::drawXlu(MR::DrawBufferType_Air);
        CategoryList::drawXlu(MR::DrawBufferType_Sun);
    }

    if (MR::isDrawSpinDriverPathAtOpa()) {
        MR::resetDrawState();
        CategoryList::execute(MR::DrawType_SpinDriverPathDrawer);
    }

    CategoryList::drawOpa(MR::DrawBufferType_Planet);
    CategoryList::drawOpa(MR::DrawBufferType_PlanetLow);
    CategoryList::execute(MR::DrawType_FlexibleSphere);
    CategoryList::drawOpa(MR::DrawBufferType_Environment);
    CategoryList::drawOpa(MR::DrawBufferType_MapObj);
    CategoryList::drawOpa(MR::DrawBufferType_MapObjWeakLight);
    CategoryList::drawOpa(MR::DrawBufferType_MapObjStrongLight);
    CategoryList::drawOpa(MR::DrawBufferType_TripodBoss);
}

void SceneFunction::executeDrawBufferListNormalOpaBeforeSilhouette() {
    Frame::phase(MotionSignal::ScenePhase_OpaBeforeSilhouette);
    CategoryList::drawOpa(MR::DrawBufferType_NoShadowedMapObj);
    CategoryList::drawOpa(MR::DrawBufferType_NoShadowedMapObjStrongLight);
}

void SceneFunction::executeDrawBufferListNormalOpa() {
    Frame::phase(MotionSignal::ScenePhase_Opa);
    CategoryList::drawOpa(MR::DrawBufferType_NoSilhouettedMapObj);
    CategoryList::drawOpa(MR::DrawBufferType_NoSilhouettedMapObjWeakLight);
    CategoryList::drawOpa(MR::DrawBufferType_NoSilhouettedMapObjStrongLight);
    CategoryList::drawOpa(MR::DrawBufferType_NPC);
    CategoryList::drawOpa(MR::DrawBufferType_Ride);
    CategoryList::drawOpa(MR::DrawBufferType_Enemy);
    CategoryList::drawOpa(MR::DrawBufferType_EnemyDecoration);
    CategoryList::drawOpa(MR::DrawBufferType_PlayerDecoration);

    if (!MR::isExistPriorDrawAir()) {
        CategoryList::drawOpa(MR::DrawBufferType_Sky);
        CategoryList::drawOpa(MR::DrawBufferType_Air);
        CategoryList::drawOpa(MR::DrawBufferType_Sun);
        CategoryList::drawXlu(MR::DrawBufferType_Sky);
        CategoryList::drawXlu(MR::DrawBufferType_Air);
        CategoryList::drawXlu(MR::DrawBufferType_Sun);
    }
}

void SceneFunction::executeDrawBufferListNormalXlu() {
    Frame::phase(MotionSignal::ScenePhase_Xlu);
    CategoryList::drawXlu(MR::DrawBufferType_Planet);
    CategoryList::drawXlu(MR::DrawBufferType_PlanetLow);
    CategoryList::drawXlu(MR::DrawBufferType_Environment);
    CategoryList::drawXlu(MR::DrawBufferType_EnvironmentStrongLight);
    CategoryList::drawXlu(MR::DrawBufferType_MapObj);
    CategoryList::drawXlu(MR::DrawBufferType_MapObjWeakLight);
    CategoryList::drawXlu(MR::DrawBufferType_MapObjStrongLight);
    CategoryList::drawXlu(MR::DrawBufferType_TripodBoss);
    CategoryList::drawXlu(MR::DrawBufferType_NoShadowedMapObj);
    CategoryList::drawXlu(MR::DrawBufferType_NoShadowedMapObjStrongLight);
    CategoryList::drawXlu(MR::DrawBufferType_NoSilhouettedMapObj);
    CategoryList::drawXlu(MR::DrawBufferType_NoSilhouettedMapObjWeakLight);
    CategoryList::drawXlu(MR::DrawBufferType_NoSilhouettedMapObjStrongLight);
    CategoryList::drawXlu(MR::DrawBufferType_NPC);
    CategoryList::drawXlu(MR::DrawBufferType_Ride);
    CategoryList::drawXlu(MR::DrawBufferType_Enemy);
    CategoryList::drawXlu(MR::DrawBufferType_EnemyDecoration);
    CategoryList::drawXlu(MR::DrawBufferType_PlayerDecoration);
}

void SceneFunction::executeDrawListOpa() {
    Frame::phase(MotionSignal::ScenePhase_ListOpa);
    MR::resetDrawState();
    CategoryList::execute(MR::DrawType_WhirlPoolAccelerator);
    CategoryList::execute(MR::DrawType_OceanRingPipeOutside);
    CategoryList::execute(MR::DrawType_SwingRope);
    CategoryList::execute(MR::DrawType_PlantStalk);
    CategoryList::execute(MR::DrawType_Plant);
    CategoryList::execute(MR::DrawType_Creeper);
    CategoryList::execute(MR::DrawType_Trapeze);
    CategoryList::execute(MR::DrawType_SpaceCocoon);
    CategoryList::execute(MR::DrawType_FootPrint);
    CategoryList::execute(MR::DrawType_NormalMap);
    CategoryList::execute(MR::DrawType_KirairaChain);
    CategoryList::execute(MR::DrawType_WaterPlant);
    CategoryList::execute(MR::DrawType_BigBubbleGoal);
    CategoryList::execute(MR::DrawType_MiiFacePartsHolder);
    CategoryList::execute(MR::DrawType_AstroDomeOrbit);
    MR::loadLightPlayer();
    CategoryList::execute(MR::DrawType_Player);
    CategoryList::execute(MR::DrawType_Fur);
    CategoryList::execute(MR::DrawType_OceanSphere);
    CategoryList::execute(MR::DrawType_Flag);
}

void SceneFunction::executeDrawListXlu() {
    Frame::phase(MotionSignal::ScenePhase_ListXlu);
    MR::resetDrawState();
    CategoryList::execute(MR::DrawType_VolumeModel);
    if (!MR::isDrawSpinDriverPathAtOpa()) {
        CategoryList::execute(MR::DrawType_SpinDriverPathDrawer);
    }
    CategoryList::execute(MR::DrawType_ClipAreaDropLaser);
    CategoryList::execute(MR::DrawType_SpiderThread);
    CategoryList::execute(MR::DrawType_DashRing);
}

void SceneFunction::executeDrawSilhouetteAndFillShadow() {
    Frame::phase(MotionSignal::ScenePhase_SilhouetteAndFillShadow);
    MR::fillSilhouetteColor();
    MR::loadViewMtx();
    MR::loadProjectionMtx();
}

void SceneFunction::executeDrawAlphaShadow() {
    Frame::phase(MotionSignal::ScenePhase_AlphaShadow);
    CategoryList::execute(MR::DrawType_AlphaShadow);
    MR::loadViewMtx();
}

void SceneFunction::executeDrawAfterIndirect() {
    Frame::phase(MotionSignal::ScenePhase_AfterIndirect);
    CategoryList::drawOpa(MR::DrawBufferType_IndirectPlanet);
    CategoryList::drawOpa(MR::DrawBufferType_IndirectMapObj);
    CategoryList::drawOpa(MR::DrawBufferType_IndirectMapObjStrongLight);
    CategoryList::drawOpa(MR::DrawBufferType_IndirectNpc);
    CategoryList::drawOpa(MR::DrawBufferType_IndirectEnemy);
    CategoryList::drawOpa(MR::DrawBufferType_GlaringLight);
    CategoryList::drawOpa(MR::DrawBufferType_PlayerParts);
    CategoryList::drawOpa(MR::DrawBufferType_CrystalBox);
    CategoryList::drawXlu(MR::DrawBufferType_IndirectPlanet);
    CategoryList::drawXlu(MR::DrawBufferType_IndirectMapObj);
    CategoryList::drawXlu(MR::DrawBufferType_IndirectMapObjStrongLight);
    CategoryList::drawXlu(MR::DrawBufferType_IndirectNpc);
    CategoryList::drawXlu(MR::DrawBufferType_IndirectEnemy);
    CategoryList::drawXlu(MR::DrawBufferType_GlaringLight);
    CategoryList::drawXlu(MR::DrawBufferType_PlayerParts);
    CategoryList::drawXlu(MR::DrawBufferType_CrystalBox);
    MR::resetDrawState();
    CategoryList::execute(MR::DrawType_GCapture);
    CategoryList::execute(MR::DrawType_WaterRoad);
    CategoryList::execute(MR::DrawType_BigBubble);
    CategoryList::execute(MR::DrawType_ElectricRailHolder);
    CategoryList::execute(MR::DrawType_OceanRing);
    CategoryList::execute(MR::DrawType_OceanBowl);
    CategoryList::execute(MR::DrawType_EffectDrawIndirect);
    CategoryList::execute(MR::DrawType_EffectDrawAfterIndirect);
    CategoryList::execute(MR::DrawType_OceanRingPipeInside);
    CategoryList::execute(MR::DrawType_WaterCameraFilter);
}

void SceneFunction::executeDrawImageEffect() {
    Frame::phase(MotionSignal::ScenePhase_ImageEffect);
    if (MR::isNormalBloomOn()) {
        MR::resetDrawState();
        CategoryList::execute(MR::DrawType_BloomEffectPreDraw);

        if (MR::isNormalBloomForEachModel()) {
            CategoryList::drawOpa(MR::DrawBufferType_BloomModel);
            CategoryList::drawXlu(MR::DrawBufferType_BloomModel);
            CategoryList::execute(MR::DrawType_BloomModel);
            CategoryList::execute(MR::DrawType_EffectDrawForBloomEffect);
            MR::drawSpiderThreadBloom();
            CategoryList::execute(MR::DrawType_OceanBowlBloomDrawer);
        }

        CategoryList::execute(MR::DrawType_BloomEffectPostDraw);
    }

    CategoryList::execute(MR::DrawType_ImageEffect);
}

void SceneFunction::executeDrawList2DNormal() {
    MR::drawInitFor2DModel();
    CategoryList::drawOpa(MR::DrawBufferType_Model3DFor2D);
    CategoryList::drawXlu(MR::DrawBufferType_Model3DFor2D);
    CategoryList::execute(MR::DrawType_CometScreenFilter);
    CategoryList::execute(MR::DrawType_GalaxyNamePlate);
    CategoryList::execute(MR::DrawType_Layout);
    CategoryList::execute(MR::DrawType_LayoutDecoration);
    CategoryList::execute(MR::DrawType_CinemaFrame);
    CategoryList::execute(MR::DrawType_TalkLayout);
    CategoryList::execute(MR::DrawType_TalkPartsLayout);
    MR::drawInitFor2DModel();
    CategoryList::execute(MR::DrawType_EffectDraw2D);
    CategoryList::execute(MR::DrawType_EffectDrawFor2DModel);
}

void SceneFunction::executeDrawList2DMovie() {
    MR::drawInitFor2DModel();
    CategoryList::execute(MR::DrawType_Movie);
    CategoryList::execute(MR::DrawType_MovieSubtitles);
}
