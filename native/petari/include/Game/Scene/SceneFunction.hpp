#pragma once


namespace MR {
    /* thanks to https://github.com/magcius/noclip.website/blob/master/src/SuperMarioGalaxy/NameObj.ts */
    enum MovementType {
        MovementType_None = -1,
        MovementType_UNK_0x00,
        MovementType_StopSceneDelayRequest,
        MovementType_Camera,
        MovementType_ScreenEffect,
        MovementType_ClippingDirector,
        MovementType_SensorHitChecker,
        MovementType_MsgSharedGroup,
        MovementType_UNK_0x07,
        MovementType_UNK_0x08,
        MovementType_AudCameraWatcher,
        MovementType_TalkDirector,
        MovementType_DemoDirector,
        MovementType_UNK_0x0C,
        MovementType_AreaObj,
        MovementType_Layout,
        MovementType_LayoutDecoration,
        MovementType_Movie,
        MovementType_WipeLayout,
        MovementType_LayoutOnPause,
        MovementType_MovieSubtitles,
        MovementType_UNK_0x14,
        MovementType_UNK_0x15,
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
        CalcAnimType_UNK_0x11,
        CalcAnimType_UNK_0x12,
        CalcAnimType_AnimParticle,
        CalcAnimType_AnimParticleIgnorePause,
        CalcAnimType_UNK_0x15,
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
        DrawBufferType_UNK_0x17,
        DrawBufferType_UNK_0x18,
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
        DrawBufferType_0x25,
        DrawBufferType_0x26,
        DrawBufferType_MirrorMapObj,
        DrawBufferType_0x28,
    };

    enum DrawType {
        DrawType_None = -1,
        DrawType_SwingRope,
        DrawType_0x1,
        DrawType_Creeper,
        DrawType_0x3,
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
        DrawType_0x11,
        DrawType_SpinDriverPathDrawer,
        DrawType_GCapture,
        DrawType_ClipAreaDropLaser,
        DrawType_SpiderThread,
        DrawType_SpaceCocoon,
        DrawType_0x17,
        DrawType_WarpPodPath,     // TODO: DrawType_NormalMap?
        DrawType_FlexibleSphere,  // TODO: DrawType_DynamicCollisionObj?
        DrawType_KirairaChain,
        DrawType_WaterPlant,
        DrawType_VolumeModel,
        DrawType_Flag,
        DrawType_AstroDomeSkyClear,
        DrawType_AstroDomeOrbit,
        DrawType_0x20,
        DrawType_OceanBowlBloomDrawer,
        DrawType_Player,
        DrawType_0x23,
        DrawType_0x24,
        DrawType_MiiFacePartsHolder,
        DrawType_ShadowSurface,
        DrawType_ShadowVolume,
        DrawType_0x28,
        DrawType_AlphaShadow,
        DrawType_ClipArea,
        DrawType_FallOutFieldDraw,
        DrawType_ClipFieldFillDraw,
        DrawType_CaptureScreenIndirect,
        DrawType_CaptureScreenCamera,
        DrawType_CenterScreenBlur,
        DrawType_0x30,
        DrawType_Fur,
        DrawType_0x32,
        DrawType_0x33,
        DrawType_BloomEffectPreDraw,
        DrawType_BloomEffectPostDraw,
        DrawType_BloomModel,
        DrawType_ImageEffect,
        DrawType_DashRing,
        DrawType_BrightSun,
        DrawType_WaterCameraFilter,
        DrawType_0x3B,
        DrawType_Layout,
        DrawType_LayoutDecoration,
        DrawType_LayoutOnPause,
        DrawType_Movie,
        DrawType_MovieSubtitles,
        DrawType_CometScreenFilter,
        DrawType_WipeLayout,
        DrawType_TalkLayout,
        DrawType_0x44,
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
        DrawType_0x52,
    };

    enum LightType {
        LightType_None = -1,  // TODO: better name?
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
    static void startStageFileLoad();
    static void waitDoneStageFileLoad();
    static void startActorFileLoadCommon();
    static void startActorFileLoadScenario();
    static void startActorPlacement();
    static void initAfterScenarioSelected();
    static void initForNameObj();
    static void initForLiveActor();
    static void initEffectSystem(u32, u32);
    static void allocateDrawBufferActorList();
    static void createHioBasicNode(Scene*);
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
