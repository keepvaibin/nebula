#include <stdint.h>
#pragma once


namespace MR {
    // Category ids index the scene's category tables (SceneNameObjListExecutor); every value has a table row.
    enum MovementType {
        MovementType_None = -1,
        MovementType_Camera,
        MovementType_ScreenEffect,
        MovementType_ClippingDirector,
        MovementType_SensorHitChecker,
        MovementType_MsgSharedGroup,
        MovementType_AudCameraWatcher,
        MovementType_TalkDirector,
        MovementType_DemoDirector,
        MovementType_AreaObj,
        MovementType_Layout,
        MovementType_LayoutDecoration,
        MovementType_Movie,
        MovementType_WipeLayout,
        MovementType_LayoutOnPause,
        MovementType_MovieSubtitles,
        MovementType_EffectUpdateCheck,
        MovementType_MirrorCamera,
        MovementType_ImageEffect,
        MovementType_AudEffectDirector,
        MovementType_AudBgmConductor,
        MovementType_CameraCover,
        MovementType_SwitchWatcherHolder,
        MovementType_ClippedMapParts,
        MovementType_Planet,
        MovementType_CollisionMapObj,
        MovementType_CollisionEnemy,
        MovementType_CollisionDirector,
        MovementType_Environment,
        MovementType_MapObj,
        MovementType_MapObjDecoration,
        MovementType_Sky,
        MovementType_Player,
        MovementType_PlayerDecoration,
        MovementType_PlayerMessenger,
        MovementType_NPC,
        MovementType_Ride,
        MovementType_Enemy,
        MovementType_EnemyDecoration,
        MovementType_Item,
        MovementType_ShadowControllerHolder,
    };

    enum CalcAnimType {
        CalcAnimType_None = -1,
        CalcAnimType_ClippedMapParts,
        CalcAnimType_Planet,
        CalcAnimType_CollisionMapObj,
        CalcAnimType_CollisionEnemy,
        CalcAnimType_Environment,
        CalcAnimType_MapObj,
        CalcAnimType_NPC,
        CalcAnimType_Ride,
        CalcAnimType_Enemy,
        CalcAnimType_Player,
        CalcAnimType_PlayerDecoration,
        CalcAnimType_MapObjDecoration,
        CalcAnimType_MirrorMapObj,
        CalcAnimType_Layout,
        CalcAnimType_LayoutDecoration,
        CalcAnimType_MovieSubtitles,
        CalcAnimType_Item,
        CalcAnimType_AnimParticle,
        CalcAnimType_AnimParticleIgnorePause,
    };

    enum DrawBufferType {
        DrawBufferType_None = -1,
        DrawBufferType_ClippedMapParts,
        DrawBufferType_Sky,
        DrawBufferType_Air,
        DrawBufferType_Sun,
        DrawBufferType_Planet,
        DrawBufferType_PlanetLow,
        DrawBufferType_Environment,
        DrawBufferType_EnvironmentStrongLight,
        DrawBufferType_MapObj,
        DrawBufferType_MapObjWeakLight,
        DrawBufferType_MapObjStrongLight,
        DrawBufferType_NoShadowedMapObj,
        DrawBufferType_NoShadowedMapObjStrongLight,
        DrawBufferType_NoSilhouettedMapObj,
        DrawBufferType_NoSilhouettedMapObjWeakLight,
        DrawBufferType_NoSilhouettedMapObjStrongLight,
        DrawBufferType_NPC,
        DrawBufferType_Ride,
        DrawBufferType_Enemy,
        DrawBufferType_EnemyDecoration,
        DrawBufferType_Player,
        DrawBufferType_PlayerDecoration,
        DrawBufferType_CrystalBox,
        DrawBufferType_PlayerParts,
        DrawBufferType_IndirectMapObj,
        DrawBufferType_IndirectMapObjStrongLight,
        DrawBufferType_IndirectNpc,
        DrawBufferType_IndirectEnemy,
        DrawBufferType_IndirectPlanet,
        DrawBufferType_BloomModel,
        DrawBufferType_TripodBoss,
        DrawBufferType_Crystal,
        DrawBufferType_CrystalItem,
        DrawBufferType_GlaringLight,
        DrawBufferType_AstroDomeSky,
        DrawBufferType_Model3DFor2D,
        DrawBufferType_MirrorMapObj,
    };

    // A draw order slot: the draw tables are indexed by it, in this order.
    enum DrawType {
        DrawType_None = -1,
        DrawType_SwingRope,
        DrawType_Creeper,
        DrawType_PlantStalk,
        DrawType_Plant,
        DrawType_Trapeze,
        DrawType_OceanBowl,
        DrawType_OceanRing,
        DrawType_OceanRingPipeInside,
        DrawType_OceanRingPipeOutside,
        DrawType_OceanSphere,
        DrawType_WaterRoad,
        DrawType_WhirlPoolAccelerator,
        DrawType_ElectricRailHolder,
        DrawType_BigBubble,
        DrawType_BigBubbleGoal,
        DrawType_SpinDriverPathDrawer,
        DrawType_GCapture,
        DrawType_ClipAreaDropLaser,
        DrawType_SpiderThread,
        DrawType_SpaceCocoon,
        DrawType_FootPrint,
        // Normal-mapped map objects and the warp pod paths.
        DrawType_NormalMap,
        // Meshes rebuilt every frame: FlexibleSphere, TrampleStar, DynamicCollisionObj.
        DrawType_FlexibleSphere,
        DrawType_KirairaChain,
        DrawType_WaterPlant,
        DrawType_VolumeModel,
        DrawType_Flag,
        DrawType_AstroDomeSkyClear,
        DrawType_AstroDomeOrbit,
        DrawType_OceanBowlBloomDrawer,
        DrawType_Player,
        DrawType_PlayerIndirect,
        DrawType_MiiFacePartsHolder,
        DrawType_ShadowSurface,
        DrawType_ShadowVolume,
        DrawType_PlayerSilhouette,
        DrawType_AlphaShadow,
        DrawType_ClipArea,
        DrawType_FallOutFieldDraw,
        DrawType_ClipFieldFillDraw,
        DrawType_CaptureScreenIndirect,
        DrawType_CaptureScreenCamera,
        DrawType_CenterScreenBlur,
        DrawType_Fur,
        DrawType_PlayerSearchLight,
        DrawType_BloomEffectPreDraw,
        DrawType_BloomEffectPostDraw,
        DrawType_BloomModel,
        DrawType_ImageEffect,
        DrawType_DashRing,
        DrawType_BrightSun,
        DrawType_WaterCameraFilter,
        DrawType_Layout,
        DrawType_LayoutDecoration,
        DrawType_LayoutOnPause,
        DrawType_Movie,
        DrawType_MovieSubtitles,
        DrawType_CometScreenFilter,
        DrawType_WipeLayout,
        DrawType_TalkLayout,
        DrawType_TalkPartsLayout,
        DrawType_CinemaFrame,
        DrawType_GalaxyNamePlate,
        DrawType_EffectDraw3D,
        DrawType_EffectDrawIndirect,
        DrawType_EffectDrawAfterIndirect,
        DrawType_EffectDraw2D,
        DrawType_EffectDrawFor2DModel,
        DrawType_EffectDrawForBloomEffect,
        DrawType_EffectDrawAfterImageEffect,
        DrawType_MessageBoardCapture,
        DrawType_MiiFaceIcon,
        DrawType_MiiFaceNew,
        DrawType_CameraCover,
    };

    enum LightType {
        LightType_None = -1,
        LightType_Player,
        LightType_Strong,
        LightType_Weak,
        LightType_Planet,
        LightType_Coin,
    };

    enum CameraType {
        CameraType_3D,
        CameraType_2D,
        CameraType_Mirror,
    };
};  // namespace MR

class Scene;

class SceneFunction {
public:
    static void movementStopSceneController();
    static void executeMovementList();
    static void executeMovementListOnPlayingMovie();
    static void executeCalcAnimList();
    static void executeCalcAnimListOnPlayingMovie();
    static void executeCalcViewAndEntryList();
    static void executeCalcViewAndEntryList2D();
    static void executeDrawBufferListNormalOpaBeforeVolumeShadow();
    static void executeDrawBufferListNormalOpaBeforeSilhouette();
    static void executeDrawBufferListNormalOpa();
    static void executeDrawBufferListNormalXlu();
    static void executeDrawListOpa();
    static void executeDrawListXlu();
    static void executeDrawSilhouetteAndFillShadow();
    static void executeDrawAlphaShadow();
    static void executeDrawAfterIndirect();
    static void executeDrawImageEffect();
    static void executeDrawList2DNormal();
    static void executeDrawList2DMovie();
    static void createStageDataHolder();
    static void startActorPlacement();
    static bool startActorPlacementStep(uint64_t deadline);
    static void initAfterScenarioSelected();
    static void initForNameObj();
    static void initForLiveActor();
    static void initEffectSystem(u32 emitterCount);
    static void allocateDrawBufferActorList();
};

class CategoryList {
public:
    static void execute(MR::MovementType);
    static void execute(MR::CalcAnimType);
    static void execute(MR::DrawType);
    static void entryDrawBuffer2D();
    static void entryDrawBuffer3D();
    static void entryDrawBufferMirror();
    static void drawOpa(MR::DrawBufferType);
    static void drawXlu(MR::DrawBufferType);
    static void requestMovementOn(MR::MovementType);
    static void requestMovementOff(MR::MovementType);
};
