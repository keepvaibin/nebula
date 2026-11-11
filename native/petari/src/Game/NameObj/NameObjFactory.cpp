#include "Game/NameObj/NameObjFactory.hpp"
#include "Game/System/NebulaMods.hpp"
#include "Game/AreaObj.hpp"
#include "Game/Boss.hpp"
#include "Game/Demo.hpp"
#include "Game/Effect.hpp"
#include "Game/Enemy.hpp"
#include "Game/Gravity.hpp"
#include "Game/Map.hpp"
#include "Game/Map/OceanBowl.hpp"
#include "Game/MapObj.hpp"
#include "Game/NPC.hpp"
#include "Game/NameObj/NameObjFactoryStubs.hpp"
#include "Game/Player/MarioActor.hpp"
#include "Game/Ride.hpp"
#include "Game/Screen.hpp"
#include "Game/Util/FileUtil.hpp"
#include "Game/Util/StringUtil.hpp"

namespace {
    template < typename T >
    NameObj* createCenterOriginCube(const char* pName) {
        return new T(AreaForm::Type_Cube1, pName);
    }

    template < typename T >
    NameObj* createBaseOriginCube(const char* pName) {
        return new T(AreaForm::Type_Cube2, pName);
    }

    template < typename T >
    NameObj* createSphere(const char* pName) {
        return new T(AreaForm::Type_Sphere, pName);
    }

    template < typename T >
    NameObj* createBaseOriginCylinder(const char* pName) {
        return new T(AreaForm::Type_Cylinder, pName);
    }

    template < typename T >
    NameObj* createBowl(const char* pName) {
        return new T(AreaForm::Type_Bowl, pName);
    }

    const NameObjFactory::Name2CreateFunc cCreateTable[] = {
        {
            "SwitchCube",
            createBaseOriginCube< SwitchArea >,
        },
        {
            "SwitchSphere",
            createSphere< SwitchArea >,
        },
        {
            "SwitchCylinder",
            createBaseOriginCylinder< SwitchArea >,
        },
        {
            "CubeCameraBox",
            createCenterOriginCube< CubeCameraArea >,
        },
        {
            "CubeCameraCylinder",
            createBaseOriginCylinder< CubeCameraArea >,
        },
        {
            "CubeCameraSphere",
            createSphere< CubeCameraArea >,
        },
        {
            "CubeCameraBowl",
            createBowl< CubeCameraArea >,
        },
        {
            "EffectCylinder",
            createBaseOriginCylinder< AreaObj >,
        },
        {
            "BindEndCube",
            createCenterOriginCube< AreaObj >,
        },
        {
            "DeathCube",
            createBaseOriginCube< DeathArea >,
        },
        {
            "DeathSphere",
            createSphere< DeathArea >,
        },
        {
            "DeathCylinder",
            createBaseOriginCylinder< DeathArea >,
        },
        {
            "PullBackCube",
            createBaseOriginCube< AreaObj >,
        },
        {
            "PullBackCylinder",
            createBaseOriginCylinder< AreaObj >,
        },
        {
            "RestartCube",
            createBaseOriginCube< RestartCube >,
        },
        {
            "WarpCube",
            createBaseOriginCube< WarpCube >,
        },
        {
            "TripodBossStepStartArea",
            createSphere< TripodBossStepStartArea >,
        },
        {
            "ChangeBgmCube",
            createBaseOriginCube< ChangeBgmCube >,
        },
        {
            "BgmProhibitArea",
            createSphere< BgmProhibitArea >,
        },
        {
            "SoundEmitterCube",
            createBaseOriginCube< SoundEmitterCube >,
        },
        {
            "SoundEmitterSphere",
            createSphere< SoundEmitterSphere >,
        },
        {
            "WaterCube",
            createBaseOriginCube< WaterArea >,
        },
        {
            "WaterSphere",
            createSphere< WaterArea >,
        },
        {
            "WaterCylinder",
            createBaseOriginCylinder< WaterArea >,
        },
        {
            "FallsCube",
            createBaseOriginCube< AreaObj >,
        },
        {
            "PlaneModeCube",
            createBaseOriginCube< AreaObj >,
        },
        {
            "PlaneCircularModeCube",
            createBaseOriginCube< AreaObj >,
        },
        {
            "PipeModeCube",
            createBaseOriginCube< AreaObj >,
        },
        {
            "TowerModeCylinder",
            createBaseOriginCylinder< AreaObj >,
        },
        {
            "BloomCube",
            createCenterOriginCube< BloomArea >,
        },
        {
            "BloomSphere",
            createSphere< BloomArea >,
        },
        {
            "BloomCylinder",
            createBaseOriginCylinder< BloomArea >,
        },
        {
            "SimpleBloomCube",
            createCenterOriginCube< SimpleBloomArea >,
        },
        {
            "SimpleBloomSphere",
            createSphere< SimpleBloomArea >,
        },
        {
            "SimpleBloomCylinder",
            createBaseOriginCylinder< SimpleBloomArea >,
        },
        {
            "ScreenBlurCube",
            createCenterOriginCube< ScreenBlurArea >,
        },
        {
            "ScreenBlurSphere",
            createSphere< ScreenBlurArea >,
        },
        {
            "ScreenBlurCylinder",
            createBaseOriginCylinder< ScreenBlurArea >,
        },
        {
            "DepthOfFieldCube",
            createCenterOriginCube< DepthOfFieldArea >,
        },
        {
            "DepthOfFieldSphere",
            createSphere< DepthOfFieldArea >,
        },
        {
            "DepthOfFieldCylinder",
            createBaseOriginCylinder< DepthOfFieldArea >,
        },
        {
            "ClipAreaCenterBox",
            MR::createClipAreaCenterBox,
        },
        {
            "ClipAreaBottomBox",
            MR::createClipAreaBottomBox,
        },
        {
            "ClipAreaTopCone",
            MR::createClipAreaTopCone,
        },
        {
            "ClipAreaBottomCone",
            MR::createClipAreaBottomCone,
        },
        {
            "ClipAreaSphere",
            MR::createClipAreaSphere,
        },
        {
            "LightCtrlCube",
            createBaseOriginCube< LightArea >,
        },
        {
            "LightCtrlCylinder",
            createBaseOriginCylinder< LightArea >,
        },
        {
            "PlaneCollisionCube",
            createBaseOriginCube< AreaObj >,
        },
        {
            "ForbidTriangleJumpCube",
            createBaseOriginCube< AreaObj >,
        },
        {
            "ForbidWaterSearchCube",
            createBaseOriginCube< AreaObj >,
        },
        {
            "QuakeEffectAreaCube",
            createBaseOriginCube< QuakeEffectArea >,
        },
        {
            "HazeCube",
            createBaseOriginCube< HazeCube >,
        },
        {
            "LensFlareArea",
            createBaseOriginCube< AreaObj >,
        },
        {
            "CameraRepulsiveSphere",
            createNameObj< CameraRepulsiveSphere >,
        },
        {
            "CameraRepulsiveCylinder",
            createNameObj< CameraRepulsiveCylinder >,
        },
        {
            "SunLightAreaBox",
            createBaseOriginCube< SunLightArea >,
        },
        {
            "GlaringLightAreaCylinder",
            createBaseOriginCylinder< GlaringLightArea >,
        },
        {
            "BigBubbleGoalAreaBox",
            createBaseOriginCube< BigBubbleGoalArea >,
        },
        {
            "BigBubbleGoalAreaCylinder",
            createBaseOriginCylinder< BigBubbleGoalArea >,
        },
        {
            "BigBubbleGoalAreaSphere",
            createSphere< BigBubbleGoalArea >,
        },
        {
            "BigBubbleCameraBox",
            createCenterOriginCube< BigBubbleCameraArea >,
        },
        {
            "BigBubbleCameraCylinder",
            createBaseOriginCylinder< BigBubbleCameraArea >,
        },
        {
            "BigBubbleCameraSphere",
            createSphere< BigBubbleCameraArea >,
        },
        {
            "BigBubbleSwitchBox",
            createBaseOriginCube< BigBubbleSwitchArea >,
        },
        {
            "BigBubbleSwitchCylinder",
            createBaseOriginCylinder< BigBubbleSwitchArea >,
        },
        {
            "BigBubbleSwitchSphere",
            createSphere< BigBubbleSwitchArea >,
        },
        {
            "ViewGroupCtrlCube",
            createBaseOriginCube< AreaObj >,
        },
        {
            "AudioEffectCube",
            createBaseOriginCube< AudioEffectArea >,
        },
        {
            "AudioEffectSphere",
            createSphere< AudioEffectArea >,
        },
        {
            "AudioEffectCylinder",
            createBaseOriginCylinder< AudioEffectArea >,
        },
        {
            "PlayerSeCube",
            createBaseOriginCube< PlayerSeArea >,
        },
        {
            "PlayerSeSphere",
            createSphere< PlayerSeArea >,
        },
        {
            "PlayerSeCylinder",
            createBaseOriginCylinder< PlayerSeArea >,
        },
        {
            "MercatorCube",
            createBaseOriginCube< MercatorTransformCube >,
        },
        {
            "AstroChangeStageCube",
            createBaseOriginCube< AstroChangeStageCube >,
        },
        {
            "BlueStarGuidanceCube",
            createBaseOriginCube< AreaObj >,
        },
        {
            "TicoSeedGuidanceCube",
            createBaseOriginCube< AreaObj >,
        },
        {
            "MessageAreaCube",
            createBaseOriginCube< MessageArea >,
        },
        {
            "MessageAreaCylinder",
            createBaseOriginCylinder< MessageArea >,
        },
        {
            "SmokeEffectColorAreaCube",
            createBaseOriginCube< AreaObj >,
        },
        {
            "BeeWallShortDistAreaCube",
            createBaseOriginCube< AreaObj >,
        },
        {
            "ExtraWallCheckArea",
            createBaseOriginCube< AreaObj >,
        },
        {
            "ExtraWallCheckCylinder",
            createBaseOriginCylinder< AreaObj >,
        },
        {
            "MirrorAreaCube",
            createBaseOriginCube< AreaObj >,
        },
        {
            "SpinGuidanceCube",
            createBaseOriginCube< SpinGuidanceArea >,
        },
        {
            "TamakoroMoveGuidanceCube",
            createBaseOriginCube< SpinGuidanceArea >,
        },
        {
            "TamakoroJumpGuidanceCube",
            createBaseOriginCube< SpinGuidanceArea >,
        },
        {
            "HeavySteeringCube",
            createBaseOriginCube< AreaObj >,
        },
        {
            "NonSleepCube",
            createBaseOriginCube< AreaObj >,
        },
        {
            "AreaMoveSphere",
            createSphere< AreaObj >,
        },
        {
            "DodoryuClosedCylinder",
            createBaseOriginCylinder< AreaObj >,
        },
        {
            "DashChargeCylinder",
            createBaseOriginCylinder< AreaObj >,
        },
        {
            "RasterScrollCube",
            createBaseOriginCube< AreaObj >,
        },
        {
            "OnimasuCube",
            createCenterOriginCube< AreaObj >,
        },
        {
            "ForbidJumpCube",
            createBaseOriginCube< AreaObj >,
        },
        {
            "CollisionArea",
            createCenterOriginCube< CollisionArea >,
        },
        {
            "AstroOverlookAreaCylinder",
            createBaseOriginCylinder< AreaObj >,
        },
        {
            "CelestrialSphere",
            createSphere< AreaObj >,
        },
        {
            "DarkMatterCube",
            createBaseOriginCube< AreaObj >,
        },
        {
            "DarkMatterCylinder",
            createBaseOriginCylinder< AreaObj >,
        },
        {
            "Mario",
            createNameObj< MarioActor >,
        },
        {
            "MarioActor",
            createNameObj< MarioActor >,
        },
        {
            "Caretaker",
            createNameObj< Caretaker >,
        },
        {
            "Rabbit",
            createNameObj< Rabbit >,
        },
        {
            "TalkSyati",
            createNameObj< Syati >,
        },
        {
            "CollectTico",
            createNameObj< CollectTico >,
        },
        {
            "TrickRabbit",
            createNameObj< TrickRabbit >,
        },
        {
            "TrickRabbitFreeRun",
            createNameObj< TrickRabbitFreeRun >,
        },
        {
            "TrickRabbitFreeRunCollect",
            createNameObj< TrickRabbitSnowCollect >,
        },
        {
            "RunawayRabbitCollect",
            createNameObj< RunawayRabbitCollect >,
        },
        {
            "TrickRabbitGhost",
            createNameObj< TrickRabbit >,
        },
        {
            "Kinopio",
            createNameObj< Kinopio >,
        },
        {
            "KinopioAstro",
            createNameObj< KinopioAstro >,
        },
        {
            "HoneyBee",
            createNameObj< HoneyBee >,
        },
        {
            "HoneyQueen",
            createNameObj< HoneyQueen >,
        },
        {
            "Peach",
            createNameObj< Peach >,
        },
        {
            "Penguin",
            createNameObj< Penguin >,
        },
        {
            "PenguinCoach",
            createNameObj< PenguinCoach >,
        },
        {
            "PenguinMaster",
            createNameObj< PenguinMaster >,
        },
        {
            "PenguinRacer",
            createNameObj< PenguinRacer >,
        },
        {
            "PenguinRacerLeader",
            createNameObj< PenguinRacerLeader >,
        },
        {
            "PenguinSkater",
            createNameObj< PenguinSkater >,
        },
        {
            "PenguinStudent",
            createNameObj< PenguinStudent >,
        },
        {
            "SignBoard",
            createNameObj< SignBoard >,
        },
        {
            "Syati",
            createNameObj< Syati >,
        },
        {
            "Tico",
            createNameObj< Tico >,
        },
        {
            "TicoComet",
            createNameObj< TicoComet >,
        },
        {
            "TicoShop",
            createNameObj< TicoShop >,
        },
        {
            "TicoStarRing",
            createNameObj< TicoStarRing >,
        },
        {
            "TicoAstro",
            createNameObj< TicoAstro >,
        },
        {
            "TicoBaby",
            createNameObj< Tico >,
        },
        {
            "DemoRabbit",
            createNameObj< DemoRabbit >,
        },
        {
            "TicoFat",
            createNameObj< TicoFat >,
        },
        {
            "TicoGalaxy",
            createNameObj< TicoGalaxy >,
        },
        {
            "TicoRail",
            createNameObj< TicoRail >,
        },
        {
            "TicoReading",
            createNameObj< TicoReading >,
        },
        {
            "TicoDomeLecture",
            createNameObj< TicoDomeLecture >,
        },
        {
            "Rosetta",
            createNameObj< Rosetta >,
        },
        {
            "RosettaReading",
            createNameObj< RosettaReading >,
        },
        {
            "RosettaPictureBook",
            createNameObj< RosettaPictureBook >,
        },
        {
            "LuigiNPC",
            createNameObj< LuigiNPC >,
        },
        {
            "LuigiEvent",
            createNameObj< LuigiNPC >,
        },
        {
            "TeresaRacer",
            createNameObj< TeresaRacer >,
        },
        {
            "NPCSupportRail",
            createNameObj< NPCSupportRail >,
        },
        {
            "GhostPlayer",
            createNameObj< GhostPlayer >,
        },
        {
            "Butler",
            createNameObj< Butler >,
        },
        {
            "ButlerMap",
            createNameObj< ButlerMap >,
        },
        {
            "ButlerExplain",
            createNameObj< ButlerExplain >,
        },
        {
            "DemoKoopaJrShip",
            createNameObj< DemoKoopaJrShip >,
        },
        {
            "Creeper",
            createNameObj< Creeper >,
        },
        {
            "JetTurtle",
            createNameObj< JetTurtle >,
        },
        {
            "Fluff",
            createNameObj< Fluff >,
        },
        {
            "FluffWind",
            createNameObj< FluffWind >,
        },
        {
            "Tamakoro",
            createNameObj< Tamakoro >,
        },
        {
            "TamakoroWithTutorial",
            MR::createTamakoroWithTutorial,
        },
        {
            "GoldenTurtle",
            createNameObj< GoldenTurtle >,
        },
        {
            "JumpBranch",
            createNameObj< JumpBranch >,
        },
        {
            "Plant",
            createNameObj< Plant >,
        },
        {
            "Pole",
            createNameObj< Pole >,
        },
        {
            "PoleNoModel",
            createNameObj< Pole >,
        },
        {
            "PoleSquare",
            createNameObj< Pole >,
        },
        {
            "PoleSquareNoModel",
            createNameObj< Pole >,
        },
        {
            "SurfRay",
            createNameObj< SurfRay >,
        },
        {
            "SwingRope",
            createNameObj< SwingRope >,
        },
        {
            "SpaceCocoon",
            createNameObj< SpaceCocoon >,
        },
        {
            "Trapeze",
            createNameObj< Trapeze >,
        },
        {
            "TreeCube",
            createNameObj< Pole >,
        },
        {
            "WaterRoad",
            createNameObj< WaterRoad >,
        },
        {
            "WarpPod",
            createNameObj< WarpPod >,
        },
        {
            "CoinBox",
            createNameObj< CoinBox >,
        },
        {
            "PunchBox",
            createNameObj< PunchBox >,
        },
        {
            "SwitchBox",
            createNameObj< SwitchBox >,
        },
        {
            "NormalMapTestObj",
            createNameObj< NormalMapTestObj >,
        },
        {
            "Takobo",
            createNameObj< Takobo >,
        },
        {
            "Karikari",
            createNameObj< Karikari >,
        },
        {
            "KoteBug",
            createNameObj< KoteBug >,
        },
        {
            "Balloonfish",
            createNameObj< Balloonfish >,
        },
        {
            "BombHei",
            createNameObj< BombHei >,
        },
        {
            "Bomb",
            createNameObj< BombHei >,
        },
        {
            "Jiraira",
            createNameObj< Jiraira >,
        },
        {
            "BombHeiLauncher",
            createNameObj< BombHeiLauncher >,
        },
        {
            "BombLauncher",
            createNameObj< BombHeiLauncher >,
        },
        {
            "SpinDriver",
            createNameObj< SpinDriver >,
        },
        {
            "SuperSpinDriver",
            MR::createSuperSpinDriverYellow,
        },
        {
            "SuperSpinDriverGreen",
            MR::createSuperSpinDriverGreen,
        },
        {
            "SuperSpinDriverPink",
            MR::createSuperSpinDriverPink,
        },
        {
            "Kuribo",
            createNameObj< Kuribo >,
        },
        {
            "KuriboChief",
            createNameObj< KuriboChief >,
        },
        {
            "KuriboMini",
            createNameObj< KuriboMini >,
        },
        {
            "Kabokuri",
            createNameObj< Kabokuri >,
        },
        {
            "TakoHei",
            createNameObj< TakoHei >,
        },
        {
            "OtaRock",
            createNameObj< OtaRock >,
        },
        {
            "Meramera",
            createNameObj< Meramera >,
        },
        {
            "IceMeramera",
            createNameObj< Meramera >,
        },
        {
            "EyeBeamer",
            createNameObj< EyeBeamer >,
        },
        {
            "BigBirikyu",
            createNameObj< Birikyu >,
        },
        {
            "Birikyu",
            createNameObj< BirikyuWithFace >,
        },
        {
            "HomingKiller",
            createNameObj< HomingKillerLauncher >,
        },
        {
            "CocoSambo",
            createNameObj< CocoSambo >,
        },
        {
            "SamboHead",
            createNameObj< SamboHead >,
        },
        {
            "BegomanBaby",
            createNameObj< BegomanBaby >,
        },
        {
            "BegomanSpring",
            createNameObj< BegomanSpring >,
        },
        {
            "BegomanSpringHide",
            createNameObj< BegomanSpring >,
        },
        {
            "BegomanSpike",
            createNameObj< BegomanSpike >,
        },
        {
            "BegomanLauncher",
            createNameObj< BegomanLauncher >,
        },
        {
            "TogeBegomanLauncher",
            createNameObj< BegomanLauncher >,
        },
        {
            "BegomanBabyLauncher",
            createNameObj< BegomanLauncher >,
        },
        {
            "FireBubble",
            createNameObj< FireBubble >,
        },
        {
            "BallBeamer",
            createNameObj< BallBeamer >,
        },
        {
            "JumpBeamer",
            createNameObj< JumpBeamer >,
        },
        {
            "JumpGuarder",
            createNameObj< JumpGuarder >,
        },
        {
            "RingBeamer",
            createNameObj< RingBeamer >,
        },
        {
            "Pukupuku",
            createNameObj< Pukupuku >,
        },
        {
            "BombTeresa",
            createNameObj< BombTeresa >,
        },
        {
            "Teresa",
            createNameObj< Teresa >,
        },
        {
            "TeresaChief",
            createNameObj< Teresa >,
        },
        {
            "TeresaWater",
            createNameObj< TeresaWater >,
        },
        {
            "Torpedo",
            createNameObj< HomingKillerLauncher >,
        },
        {
            "Dossun",
            createNameObj< Dossun >,
        },
        {
            "Onimasu",
            createNameObj< OnimasuJump >,
        },
        {
            "OnimasuPivot",
            createNameObj< OnimasuPivot >,
        },
        {
            "Unizo",
            createNameObj< Unizo >,
        },
        {
            "UnizoLand",
            createNameObj< Unizo >,
        },
        {
            "UnizoShoal",
            createNameObj< Unizo >,
        },
        {
            "UnizoLauncher",
            createNameObj< UnizoLauncher >,
        },
        {
            "Kiraira",
            createNameObj< Kiraira >,
        },
        {
            "KirairaRail",
            createNameObj< Kiraira >,
        },
        {
            "NokonokoLand",
            createNameObj< NokonokoLand >,
        },
        {
            "Gesso",
            createNameObj< Gesso >,
        },
        {
            "Poihana",
            createNameObj< Poihana >,
        },
        {
            "Snakehead",
            createNameObj< Snakehead >,
        },
        {
            "SnakeheadSmall",
            createNameObj< Snakehead >,
        },
        {
            "Jellyfish",
            createNameObj< Jellyfish >,
        },
        {
            "JellyfishElectric",
            createNameObj< JellyfishElectric >,
        },
        {
            "Kanina",
            createNameObj< Kanina >,
        },
        {
            "KaninaRed",
            createNameObj< Kanina >,
        },
        {
            "Mogu",
            createNameObj< Mogu >,
        },
        {
            "Mogucchi",
            createNameObj< Mogucchi >,
        },
        {
            "MogucchiRefuseTerritory",
            createNameObj< MogucchiRefuseTerritory >,
        },
        {
            "KoopaJrShip",
            createNameObj< KoopaJrShip >,
        },
        {
            "Hanachan",
            createNameObj< Hanachan >,
        },
        {
            "JumpSpider",
            createNameObj< JumpSpider >,
        },
        {
            "StringSpider",
            createNameObj< StringSpider >,
        },
        {
            "PackunPetit",
            createNameObj< PackunPetit >,
        },
        {
            "StinkBugSmall",
            createNameObj< StinkBugSmall >,
        },
        {
            "StinkBugParent",
            createNameObj< StinkBugParent >,
        },
        {
            "BasaBasa",
            createNameObj< BasaBasa >,
        },
        {
            "BasaBasaIce",
            createNameObj< BasaBasa >,
        },
        {
            "Metbo",
            createNameObj< Metbo >,
        },
        {
            "BombBird",
            createNameObj< BombBird >,
        },
        {
            "MechanicKoopaMini",
            createNameObj< MechanicKoopaMini >,
        },
        {
            "Karon",
            createNameObj< Karon >,
        },
        {
            "HammerHeadPackun",
            createNameObj< HammerHeadPackun >,
        },
        {
            "DharmaSambo",
            createNameObj< DharmaSambo >,
        },
        {
            "SearchBeamer",
            createNameObj< SearchBeamer >,
        },
        {
            "Petari",
            createNameObj< Petari >,
        },
        {
            "MagnumKiller",
            createNameObj< HomingKillerLauncher >,
        },
        {
            "BossBegoman",
            createNameObj< BossBegoman >,
        },
        {
            "BossStinkBug",
            createNameObj< BossStinkBug >,
        },
        {
            "DinoPackun",
            MR::createDinoPackunVs1,
        },
        {
            "DinoPackunVs2",
            MR::createDinoPackunVs2,
        },
        {
            "Dodoryu",
            createNameObj< Dodoryu >,
        },
        {
            "KoopaVs1",
            KoopaFunction::createKoopaVs1,
        },
        {
            "KoopaVs2",
            KoopaFunction::createKoopaVs2,
        },
        {
            "KoopaVs3",
            KoopaFunction::createKoopaVs3,
        },
        {
            "OtaKing",
            createNameObj< OtaKing >,
        },
        {
            "Polta",
            createNameObj< Polta >,
        },
        {
            "SkeletalFishBoss",
            createNameObj< SkeletalFishBoss >,
        },
        {
            "SkeletalFishBaby",
            createNameObj< SkeletalFishBaby >,
        },
        {
            "SkeletalFishBossRail",
            createNameObj< SkeletalFishBossRail >,
        },
        {
            "TombSpider",
            createNameObj< TombSpider >,
        },
        {
            "BossKameck",
            MR::createBossKameck1,
        },
        {
            "BossKameck2",
            MR::createBossKameck2,
        },
        {
            "FireBallBeamKameck",
            MR::createFireBallBeamKameck,
        },
        {
            "TurtleBeamKameck",
            MR::createTurtleBeamKameck,
        },
        {
            "TripodBoss",
            MR::createTripodBoss,
        },
        {
            "Tripod2Boss",
            MR::createTripod2Boss,
        },
        {
            "TripodBossBaseJointPosition",
            createNameObj< TripodBossBaseJointPosition >,
        },
        {
            "TripodBossKillerGenerater",
            createNameObj< TripodBossKillerGenerater >,
        },
        {
            "TripodBossStepPoint",
            createNameObj< TripodBossStepPoint >,
        },
        {
            "TripodBossCore",
            createNameObj< TripodBossCore >,
        },
        {
            "TripodBossCoin",
            createNameObj< TripodBossCoin >,
        },
        {
            "TripodBossKinokoOneUp",
            createNameObj< TripodBossKinokoOneUp >,
        },
        {
            "TripodBossShell",
            createNameObj< TripodBossShell >,
        },
        {
            "TripodBossGuardWall",
            createNameObj< TripodBossGuardWall >,
        },
        {
            "TriPodBossBodyA",
            createNameObj< TripodBossFixParts >,
        },
        {
            "TripodBossBodyABloom",
            createNameObj< TripodBossFixParts >,
        },
        {
            "TriPodBossLeg1A",
            createNameObj< TripodBossFixParts >,
        },
        {
            "TriPodBossLeg1B",
            createNameObj< TripodBossFixParts >,
        },
        {
            "TriPodBossLeg1C",
            createNameObj< TripodBossFixParts >,
        },
        {
            "TriPodBossLeg2A",
            createNameObj< TripodBossFixParts >,
        },
        {
            "TriPodBossLeg2ABloom",
            createNameObj< TripodBossFixParts >,
        },
        {
            "TriPodBossLeg3A",
            createNameObj< TripodBossFixParts >,
        },
        {
            "TriPodBossLeg3B",
            createNameObj< TripodBossFixParts >,
        },
        {
            "TriPodBossLeg3C",
            createNameObj< TripodBossFixParts >,
        },
        {
            "TripodBossFoot",
            createNameObj< TripodBossFixParts >,
        },
        {
            "TripodBossFootBloom",
            createNameObj< TripodBossFixParts >,
        },
        {
            "TriPodBossLegBottom",
            createNameObj< TripodBossFixParts >,
        },
        {
            "TripodBossEyeLight",
            createNameObj< TripodBossFixParts >,
        },
        {
            "TripodBossEyeLightBloom",
            createNameObj< TripodBossFixParts >,
        },
        {
            "TriPodBossGearA",
            createNameObj< TripodBossRotateParts >,
        },
        {
            "TriPodBossGearB",
            createNameObj< TripodBossRotateParts >,
        },
        {
            "TriPodBossGearC",
            createNameObj< TripodBossRotateParts >,
        },
        {
            "TriPodBossGearSmallA",
            createNameObj< TripodBossRotateParts >,
        },
        {
            "TriPodBossGearSmallB",
            createNameObj< TripodBossRotateParts >,
        },
        {
            "TriPodBossGearSmallC",
            createNameObj< TripodBossRotateParts >,
        },
        {
            "TriPodBossGearBody",
            createNameObj< TripodBossRotateParts >,
        },
        {
            "TripodBossUpperKillerCannon",
            createNameObj< TripodBossKillerGeneraterCircle >,
        },
        {
            "TripodBossUnderKillerCannon",
            createNameObj< TripodBossKillerGeneraterCircle >,
        },
        {
            "TripodBossBottomKillerCannon",
            createNameObj< TripodBossKillerGeneraterCircle >,
        },
        {
            "SpiderAttachPoint",
            createNameObj< SlingShooter >,
        },
        {
            "SpiderCoin",
            createNameObj< SpiderCoin >,
        },
        {
            "SpiderMapBlock",
            createNameObj< SpiderMapBlock >,
        },
        {
            "ArrowSwitch",
            createNameObj< ArrowSwitch >,
        },
        {
            "ArrowSwitchMulti",
            createNameObj< ArrowSwitchMulti >,
        },
        {
            "ArrowSwitchTarget",
            createNameObj< ArrowSwitchTarget >,
        },
        {
            "AstroOverlookObj",
            createNameObj< AstroOverlookObj >,
        },
        {
            "BlueStarCupsulePlanet",
            createNameObj< BlueStarCupsulePlanet >,
        },
        {
            "BigFan",
            createNameObj< BigFan >,
        },
        {
            "BigFanNoLeg",
            createNameObj< BigFan >,
        },
        {
            "ClipAreaDropLaser",
            createNameObj< ClipAreaDropLaser >,
        },
        {
            "RevolvingWay",
            createNameObj< RevolvingWay >,
        },
        {
            "SoundEmitter",
            createNameObj< SoundEmitter >,
        },
        {
            "JumpHole",
            createNameObj< JumpHole >,
        },
        {
            "BallOpener",
            createNameObj< BallOpener >,
        },
        {
            "DragonHeadFlower",
            createNameObj< DragonHeadFlower >,
        },
        {
            "ConcreteBlock",
            createNameObj< SimpleBreakableObj >,
        },
        {
            "SphereRailDash",
            createNameObj< SphereRailDash >,
        },
        {
            "SnowplowSwitch",
            createNameObj< SnowplowSwitch >,
        },
        {
            "BallRail",
            createNameObj< BallRail >,
        },
        {
            "FallDownBridge",
            createNameObj< FallDownBridge >,
        },
        {
            "RockCreator",
            createNameObj< RockCreator >,
        },
        {
            "MeteoContainer",
            createNameObj< MeteoContainer >,
        },
        {
            "MagicBell",
            createNameObj< MagicBell >,
        },
        {
            "FlowerGroup",
            createNameObj< PlantGroup >,
        },
        {
            "FlowerBlueGroup",
            createNameObj< PlantGroup >,
        },
        {
            "CutBushGroup",
            createNameObj< PlantGroup >,
        },
        {
            "SmallStone",
            createNameObj< SmallStone >,
        },
        {
            "CircleShell",
            createNameObj< SmallStone >,
        },
        {
            "CircleStrawberry",
            createNameObj< SmallStone >,
        },
        {
            "Tsukidashikun",
            createNameObj< Tsukidashikun >,
        },
        {
            "TsukidashikunBig",
            createNameObj< RailBlock >,
        },
        {
            "TimerCoinBlock",
            createNameObj< ItemBlock >,
        },
        {
            "SplashCoinBlock",
            createNameObj< ItemBlock >,
        },
        {
            "TimerPieceBlock",
            createNameObj< ItemBlock >,
        },
        {
            "SplashPieceBlock",
            createNameObj< ItemBlock >,
        },
        {
            "ItemBlockSwitch",
            createNameObj< ItemBlock >,
        },
        {
            "ExterminationCheckerKuribo",
            MR::createExterminationPowerStar,
        },
        {
            "ExterminationKuriboKeySwitch",
            MR::createExterminationKeySwitch,
        },
        {
            "ExterminationSkeletalFishBaby",
            MR::createExterminationPowerStar,
        },
        {
            "ExterminationMeramera",
            MR::createExterminationPowerStar,
        },
        {
            "ShootingStar",
            createNameObj< ShootingStar >,
        },
        {
            "FlameGun",
            createNameObj< FlameGun >,
        },
        {
            "BenefitItemOneUp",
            createNameObj< BenefitItemOneUp >,
        },
        {
            "BenefitItemLifeUp",
            createNameObj< BenefitItemLifeUp >,
        },
        {
            "BenefitItemInvincible",
            createNameObj< BenefitItemInvincible >,
        },
        {
            "CoinSpot",
            createNameObj< CoinSpot >,
        },
        {
            "CollisionBlocker",
            createNameObj< CollisionBlocker >,
        },
        {
            "LotusLeaf",
            createNameObj< LotusLeaf >,
        },
        {
            "PunchingKinoko",
            createNameObj< PunchingKinoko >,
        },
        {
            "DesertMovingLand",
            createNameObj< DesertMovingLand >,
        },
        {
            "DesertMovingLandSmall",
            createNameObj< DesertMovingLand >,
        },
        {
            "NoteFairy",
            createNameObj< NoteFairy >,
        },
        {
            "EarthenPipe",
            createNameObj< EarthenPipe >,
        },
        {
            "EarthenPipeInWater",
            createNameObj< EarthenPipe >,
        },
        {
            "ElectricRail",
            createNameObj< ElectricRail >,
        },
        {
            "ElectricRailMoving",
            createNameObj< ElectricRailMoving >,
        },
        {
            "WanwanRolling",
            createNameObj< RockCreator >,
        },
        {
            "WanwanRollingMini",
            createNameObj< RockCreator >,
        },
        {
            "WanwanRollingGold",
            createNameObj< RockCreator >,
        },
        {
            "BreakableCage",
            createNameObj< BreakableCage >,
        },
        {
            "BreakableCageRotate",
            createNameObj< BreakableCage >,
        },
        {
            "BreakableCageL",
            createNameObj< BreakableCage >,
        },
        {
            "BreakableFixation",
            createNameObj< BreakableCage >,
        },
        {
            "BreakableTrash",
            createNameObj< BreakableCage >,
        },
        {
            "BigBubble",
            createNameObj< BigBubble >,
        },
        {
            "BigBubbleMoveLimitterCylinder",
            MR::createBigBubbleMoveLimitterCylinder,
        },
        {
            "BigBubbleMoveLimitterPlane",
            MR::createBigBubbleMoveLimitterPlane,
        },
        {
            "BigBubbleGenerator",
            MR::createBigBubbleGenerator,
        },
        {
            "BigObstructBubbleGenerator",
            MR::createBigObstructBubbleGenerator,
        },
        {
            "GCaptureTarget",
            createNameObj< GCaptureTarget >,
        },
        {
            "AsteroidA",
            createNameObj< SimpleEnvironmentObj >,
        },
        {
            "AsteroidB",
            createNameObj< SimpleEnvironmentObj >,
        },
        {
            "AsteroidC",
            createNameObj< SimpleEnvironmentObj >,
        },
        {
            "AsteroidD",
            createNameObj< SimpleEnvironmentObj >,
        },
        {
            "SpaceStickA",
            createNameObj< SimpleEnvironmentObj >,
        },
        {
            "SpaceStickB",
            createNameObj< SimpleEnvironmentObj >,
        },
        {
            "BlackHole",
            createNameObj< BlackHole >,
        },
        {
            "BlackHoleCube",
            createNameObj< BlackHole >,
        },
        {
            "KillerGunnerSingle",
            createNameObj< KillerGunnerSingle >,
        },
        {
            "KillerGunnerDouble",
            createNameObj< SimpleEnvironmentObj >,
        },
        {
            "KillerGunnerTriple",
            createNameObj< SimpleEnvironmentObj >,
        },
        {
            "PowerStar",
            createNameObj< PowerStar >,
        },
        {
            "GrandStar",
            createNameObj< PowerStar >,
        },
        {
            "PowerStarAppearPoint",
            createNameObj< PowerStarAppearPoint >,
        },
        {
            "CocoNut",
            createNameObj< CocoNut >,
        },
        {
            "CoconutTree",
            createNameObj< CoconutTree >,
        },
        {
            "CoconutTreeLeaf",
            createNameObj< CoconutTreeLeafGroup >,
        },
        {
            "CollapsePlane",
            createNameObj< CollapsePlane >,
        },
        {
            "WoodBox",
            createNameObj< WoodBox >,
        },
        {
            "ItemBubble",
            createNameObj< ItemBubble >,
        },
        {
            "SpinningBox",
            createNameObj< SpinningBox >,
        },
        {
            "ElectricBall",
            createNameObj< ElectricBall >,
        },
        {
            "SpaceMine",
            createNameObj< SpaceMine >,
        },
        {
            "MorphItemNeoHopper",
            createNameObj< MorphItemNeoHopper >,
        },
        {
            "MorphItemNeoBee",
            createNameObj< MorphItemNeoBee >,
        },
        {
            "MorphItemNeoTeresa",
            createNameObj< MorphItemNeoTeresa >,
        },
        {
            "MorphItemNeoIce",
            createNameObj< MorphItemNeoIce >,
        },
        {
            "MorphItemNeoFire",
            createNameObj< MorphItemNeoFire >,
        },
        {
            "MorphItemNeoFoo",
            createNameObj< MorphItemNeoFoo >,
        },
        {
            "AirBubble",
            createNameObj< AirBubble >,
        },
        {
            "AirBubbleGenerator",
            createNameObj< AirBubbleGenerator >,
        },
        {
            "Butterfly",
            createNameObj< Butterfly >,
        },
        {
            "Pompon2Plant",
            createNameObj< PomponPlant >,
        },
        {
            "Pompon4Plant",
            createNameObj< PomponPlant >,
        },
        {
            "LavaGeyser",
            createNameObj< LavaGeyser >,
        },
        {
            "LavaProminence",
            createNameObj< LavaProminence >,
        },
        {
            "LavaProminenceTriple",
            createNameObj< LavaProminenceTriple >,
        },
        {
            "LavaProminenceEnvironment",
            createNameObj< LavaProminenceTriple >,
        },
        {
            "LavaProminenceWithoutShadow",
            createNameObj< LavaProminence >,
        },
        {
            "LavaObstacleRockShell",
            createNameObj< HipDropMoveObj >,
        },
        {
            "LavaObstacleRockHomeA",
            createNameObj< HipDropDemoMoveObj >,
        },
        {
            "LavaObstacleRockHomeB",
            createNameObj< AnmModelSwitchMove >,
        },
        {
            "LavaHomeFloaterB",
            createNameObj< LavaFloater >,
        },
        {
            "LavaFloatingStepA",
            createNameObj< LavaFloater >,
        },
        {
            "LavaFloatingStepB",
            createNameObj< LavaFloater >,
        },
        {
            "LavaFloatingStepC",
            createNameObj< LavaFloater >,
        },
        {
            "LavaFloatingStepD",
            createNameObj< LavaFloater >,
        },
        {
            "LavaHomeSeesaw",
            createNameObj< LavaHomeSeesaw >,
        },
        {
            "LavaHomeVolcanoFlow",
            createNameObj< LavaHomeVolcanoFlow >,
        },
        {
            "LavaHomeVolcanoInnerFlow",
            createNameObj< RailMoveObj >,
        },
        {
            "LavaRotatePlanetStartStep",
            createNameObj< RailMoveObj >,
        },
        {
            "LavaBreakBridge",
            createNameObj< AnmModelGroundOnMove >,
        },
        {
            "LavaBreakRockA",
            createNameObj< AnmModelSwitchMoveEndKill >,
        },
        {
            "LavaBreakRockB",
            createNameObj< AnmModelGroundOnMove >,
        },
        {
            "LavaBreakRockC",
            createNameObj< AnmModelGroundOnMove >,
        },
        {
            "LavaBreakRockD",
            createNameObj< AnmModelGroundOnMove >,
        },
        {
            "LavaBreakColumn",
            createNameObj< LavaBreakColumn >,
        },
        {
            "LavaShellTower",
            createNameObj< LavaShellTower >,
        },
        {
            "LavaFallDownColumnA",
            createNameObj< AnmModelSwitchMove >,
        },
        {
            "LavaFallDownColumnB",
            createNameObj< AnmModelSwitchMove >,
        },
        {
            "LavaShellTowerStep",
            createNameObj< LavaGalaxyParts >,
        },
        {
            "LavaStepA",
            createNameObj< LavaGalaxyParts >,
        },
        {
            "LavaStepB",
            createNameObj< LavaGalaxyParts >,
        },
        {
            "LavaStepC",
            createNameObj< LavaGalaxyParts >,
        },
        {
            "LavaSteam",
            createNameObj< LavaSteam >,
        },
        {
            "LavaStrangeRock",
            createNameObj< LavaStrangeRock >,
        },
        {
            "LavaStrangeRockL",
            createNameObj< LavaStrangeRock >,
        },
        {
            "LavaStrangeRockTable",
            createNameObj< LavaStrangeRock >,
        },
        {
            "LavaSpaceStickA",
            createNameObj< SimpleEnvironmentObj >,
        },
        {
            "LavaSpaceStickB",
            createNameObj< SimpleEnvironmentObj >,
        },
        {
            "LavaBlackUFO",
            createNameObj< SimpleEnvironmentObj >,
        },
        {
            "CrystalCageS",
            createNameObj< CrystalCage >,
        },
        {
            "CrystalCageM",
            createNameObj< CrystalCage >,
        },
        {
            "CrystalCageL",
            createNameObj< CrystalCage >,
        },
        {
            "CrystalCageMoving",
            createNameObj< CrystalCageMoving >,
        },
        {
            "ShellfishCoin",
            createNameObj< Shellfish >,
        },
        {
            "ShellfishYellowChip",
            createNameObj< Shellfish >,
        },
        {
            "SeaBottomBigDoorA",
            createNameObj< RailDemoMoveObj >,
        },
        {
            "SeaBottomBigDoorB",
            createNameObj< RailDemoMoveObj >,
        },
        {
            "SeaBottomBigDoorC",
            createNameObj< RailDemoMoveObj >,
        },
        {
            "FloaterLandPartsFrame",
            createNameObj< SimpleMapObj >,
        },
        {
            "OceanFloaterTypeU",
            createNameObj< OceanFloaterLandParts >,
        },
        {
            "DashRing",
            createNameObj< DashRing >,
        },
        {
            "MeteorStrike",
            createNameObj< MeteorStrikeLauncher >,
        },
        {
            "MeteorStrikeEnvironment",
            createNameObj< MeteorStrikeLauncher >,
        },
        {
            "MeteorCannon",
            createNameObj< MeteorStrikeLauncher >,
        },
        {
            "BrightObj",
            createNameObj< BrightObj >,
        },
        {
            "BrightSun",
            createNameObj< BrightSun >,
        },
        {
            "PhantomTorch",
            createNameObj< PhantomTorch >,
        },
        {
            "PhantomBonfire",
            createNameObj< PhantomTorch >,
        },
        {
            "PhantomCandlestand",
            createNameObj< Candlestand >,
        },
        {
            "CandlestandIceVolcano",
            createNameObj< Candlestand >,
        },
        {
            "MarioLauncher",
            createNameObj< MarioLauncher >,
        },
        {
            "MarioLauncherAttractor",
            createNameObj< MarioLauncherAttractor >,
        },
        {
            "WaterBazooka",
            createNameObj< WaterBazooka >,
        },
        {
            "ElectricBazooka",
            createNameObj< WaterBazooka >,
        },
        {
            "WaterPressure",
            createNameObj< WaterPressure >,
        },
        {
            "FirePressure",
            createNameObj< FirePressure >,
        },
        {
            "IronCannonLauncher",
            createNameObj< IronCannonLauncher >,
        },
        {
            "IronCannonLauncherPoint",
            createNameObj< IronCannonLauncherPoint >,
        },
        {
            "FirePressureRadiate",
            createNameObj< FirePressureRadiate >,
        },
        {
            "TemplateStageGeometry",
            createNameObj< SimpleMapObj >,
        },
        {
            "SunkenShip",
            createNameObj< SimpleBreakableObj >,
        },
        {
            "BeeFlowerHover",
            createNameObj< BeeFlowerHover >,
        },
        {
            "WaterfallCaveCover",
            createNameObj< WaterfallCaveCover >,
        },
        {
            "WaterfallCaveNoBreakCover",
            createNameObj< SimpleMapObj >,
        },
        {
            "PalmIsland",
            createNameObj< PalmIsland >,
        },
        {
            "IceMerameraKing",
            createNameObj< IceMerameraKing >,
        },
        {
            "OceanPierFloaterA",
            createNameObj< OceanWaveFloater >,
        },
        {
            "HeavenlyBeachTopRock",
            createNameObj< AnmModelSwitchMoveEndKill >,
        },
        {
            "HeavenlyBeachUnderRock",
            createNameObj< AnmModelSwitchMove >,
        },
        {
            "OceanSmallTurtle",
            createNameObj< OceanSmallTurtle >,
        },
        {
            "OceanHexagonFloater",
            createNameObj< OceanWaveFloater >,
        },
        {
            "SpringWaterFloaterA",
            createNameObj< SpringWaterFloaterSpot >,
        },
        {
            "SpringWaterFloaterB",
            createNameObj< SpringWaterFloaterSpot >,
        },
        {
            "ShutterDoorB",
            createNameObj< RailMoveObj >,
        },
        {
            "ShutterDoorC",
            createNameObj< AnmModelSwitchMove >,
        },
        {
            "ShutterDoorD",
            createNameObj< AnmModelSwitchMove >,
        },
        {
            "ShutterDoorE",
            createNameObj< AnmModelSwitchMove >,
        },
        {
            "SeaBottomTriplePropellerStand",
            createNameObj< SimpleMapObj >,
        },
        {
            "SeaBottomTriplePropeller",
            createNameObj< SeaBottomTriplePropeller >,
        },
        {
            "OceanFloaterTowerRotateStepA",
            createNameObj< RotateMoveObj >,
        },
        {
            "OceanFloaterTowerRotateStepB",
            createNameObj< RotateMoveObj >,
        },
        {
            "OceanFloaterTowerRotateStepC",
            createNameObj< RotateMoveObj >,
        },
        {
            "OceanFloaterTowerRotateStepD",
            createNameObj< RotateMoveObj >,
        },
        {
            "LargeChain",
            createNameObj< LargeChain >,
        },
        {
            "IcicleRock",
            createNameObj< SimpleBreakableObj >,
        },
        {
            "ItemAppearStoneSpinDriver",
            createNameObj< ItemAppearStone >,
        },
        {
            "SnowFloor",
            createNameObj< SnowFloor >,
        },
        {
            "SnowFloorTile",
            createNameObj< SnowFloorTile >,
        },
        {
            "FlipPanel",
            createNameObj< FlipPanel >,
        },
        {
            "FlipPanelObserver",
            createNameObj< FlipPanelObserver >,
        },
        {
            "FlipPanelReverse",
            createNameObj< FlipPanel >,
        },
        {
            "FlipPanelFrame",
            createNameObj< SimpleMapObj >,
        },
        {
            "PhantomTowerMoveStepA",
            createNameObj< RailMoveObj >,
        },
        {
            "SideSpikeMoveStepA",
            createNameObj< SideSpikeMoveStep >,
        },
        {
            "SpaceMineRailA",
            createNameObj< SimpleMapObj >,
        },
        {
            "SpaceMineRail5m",
            createNameObj< SimpleMapObj >,
        },
        {
            "SandUpDownTowerTop",
            createNameObj< SimpleMapObjFarMax >,
        },
        {
            "SandUpDownTowerBottom",
            createNameObj< SimpleMapObjFarMax >,
        },
        {
            "SandUpDownKillerGunnerBase",
            createNameObj< SimpleMapObj >,
        },
        {
            "SandUpDownTriRock",
            createNameObj< SandUpDownTriRock >,
        },
        {
            "JumpStand",
            createNameObj< JumpStand >,
        },
        {
            "CaretakerGarbage",
            createNameObj< SimpleMapObj >,
        },
        {
            "GlassBottleTall",
            createNameObj< SimpleMapObj >,
        },
        {
            "HopperBeltConveyerRotatePartsA",
            createNameObj< RotateMoveObj >,
        },
        {
            "HopperBeltConveyerMovePartsA",
            createNameObj< RailMoveObj >,
        },
        {
            "ChooChooTrain",
            createNameObj< ChooChooTrain >,
        },
        {
            "SandRiverLightA",
            createNameObj< SwingLight >,
        },
        {
            "SnowMan",
            createNameObj< SnowMan >,
        },
        {
            "SnowBlockA",
            createNameObj< SimpleBreakableObj >,
        },
        {
            "ShockWaveGenerator",
            createNameObj< ShockWaveGenerator >,
        },
        {
            "DangerSignBoard",
            createNameObj< SimpleMapObjPush >,
        },
        {
            "Banekiti",
            createNameObj< Banekiti >,
        },
        {
            "MirrorReflectionTest",
            createNameObj< SimpleMirrorObj >,
        },
        {
            "MirrorReflectionPeachCastle",
            createNameObj< SimpleMirrorObj >,
        },
        {
            "MirrorReflectionTwinFallLake",
            createNameObj< SimpleMirrorObj >,
        },
        {
            "MirrorModelTest",
            createNameObj< SimpleMirrorReflectionObj >,
        },
        {
            "MirrorModelPeachCastle",
            createNameObj< SimpleMirrorReflectionObj >,
        },
        {
            "MirrorModelTwinFallLake",
            createNameObj< SimpleMirrorReflectionObj >,
        },
        {
            "PhantomFirewood",
            createNameObj< SimpleMapObj >,
        },
        {
            "ArrowBoard",
            createNameObj< SimpleMapObj >,
        },
        {
            "TeresaRoomGlaringLight",
            createNameObj< GlaringLight >,
        },
        {
            "SpaceDustWoodA",
            createNameObj< SimpleEnvironmentObj >,
        },
        {
            "SpaceDustWoodB",
            createNameObj< SimpleEnvironmentObj >,
        },
        {
            "TeresaRoomDoor",
            createNameObj< AnmModelSwitchMoveEventCamera >,
        },
        {
            "TrampleStar",
            createNameObj< TrampleStar >,
        },
        {
            "ReverseGravityTowerInside",
            createNameObj< SimpleMapObj >,
        },
        {
            "HipDropRock",
            createNameObj< HipDropRock >,
        },
        {
            "DropOfWaterCore",
            createNameObj< SimpleMapObj >,
        },
        {
            "WatchTowerRotateStep",
            createNameObj< WatchTowerRotateStep >,
        },
        {
            "ForestAppearStepA",
            createNameObj< SimpleMapObj >,
        },
        {
            "ForestObstacleWoodA",
            createNameObj< HipDropDemoMoveObj >,
        },
        {
            "AstroDomeAsteroid",
            createNameObj< AstroDomeAsteroid >,
        },
        {
            "AstroDomeBlueStar",
            createNameObj< AstroDomeBlueStar >,
        },
        {
            "AstroDomeComet",
            createNameObj< AstroDomeComet >,
        },
        {
            "AstroDomeDemoAstroGalaxy",
            createNameObj< AstroDomeDemoAstroGalaxy >,
        },
        {
            "SphereSelectorHandle",
            createNameObj< SphereSelectorHandle >,
        },
        {
            "ForestWoodCover",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaShipManholeCover",
            createNameObj< ManholeCover >,
        },
        {
            "StarDustStepA",
            createNameObj< SimpleMapObj >,
        },
        {
            "StarDustStepB",
            createNameObj< SimpleMapObj >,
        },
        {
            "TreasureSpot",
            createNameObj< TreasureSpot >,
        },
        {
            "CoinFlower",
            createNameObj< TreasureSpot >,
        },
        {
            "StarDustRollingStepA",
            createNameObj< RotateMoveObj >,
        },
        {
            "AsteroidMoveA",
            createNameObj< RailMoveObj >,
        },
        {
            "StarPieceCluster",
            createNameObj< SimpleMapObj >,
        },
        {
            "SpaceSeparatorA",
            createNameObj< SimpleMapObj >,
        },
        {
            "SpaceSeparatorB",
            createNameObj< SimpleMapObj >,
        },
        {
            "ForestNarrowStepA",
            createNameObj< SimpleMapObj >,
        },
        {
            "ForestHomeGate",
            createNameObj< SimpleMapObj >,
        },
        {
            "RGTowerPressGroundA",
            createNameObj< RailMoveObjPress >,
        },
        {
            "WeatherVane",
            createNameObj< SimpleMapObj >,
        },
        {
            "RainCloud",
            createNameObj< RainCloud >,
        },
        {
            "PowerStarKeeperA",
            createNameObj< RotateMoveObj >,
        },
        {
            "PowerStarKeeperB",
            createNameObj< RotateMoveObj >,
        },
        {
            "PowerStarKeeperC",
            createNameObj< RotateMoveObj >,
        },
        {
            "ForestPoihanaFenceA",
            createNameObj< SimpleMapObj >,
        },
        {
            "ForestPoihanaFenceB",
            createNameObj< SimpleMapObj >,
        },
        {
            "TeresaMansionBridgeA",
            createNameObj< SimpleMapObj >,
        },
        {
            "TeresaMansionBridgeB",
            createNameObj< SimpleMapObj >,
        },
        {
            "DarknessRoomDoorA",
            createNameObj< DarknessRoomDoor >,
        },
        {
            "TeresaMansionDoorA",
            createNameObj< TypicalDoor >,
        },
        {
            "TeresaMansionDoorB",
            createNameObj< TypicalDoor >,
        },
        {
            "TeresaMansionEntranceDoor",
            createNameObj< TypicalDoorOpen >,
        },
        {
            "WaterBazookaTowerMoveStepA",
            createNameObj< RotateMoveObj >,
        },
        {
            "WaterBazookaTowerMoveStepB",
            createNameObj< RailMoveObj >,
        },
        {
            "WaterBazookaTowerMoveStepC",
            createNameObj< RailMoveObj >,
        },
        {
            "QuestionBoxMoveStepA",
            createNameObj< QuestionBoxGalleryObj >,
        },
        {
            "QuestionBoxMoveStepB",
            createNameObj< QuestionBoxGalleryObj >,
        },
        {
            "QuestionBoxMoveStepC",
            createNameObj< QuestionBoxGalleryObj >,
        },
        {
            "ForestHomeBridge",
            createNameObj< SimpleMapObj >,
        },
        {
            "ForestBarricadeRockA",
            createNameObj< SimpleMapObj >,
        },
        {
            "ReverseGravityRoomPressGroundA",
            createNameObj< RailMoveObjPress >,
        },
        {
            "ReverseGravityRoomPressGroundB",
            createNameObj< RailMoveObjPress >,
        },
        {
            "ReverseGravityRoomPressGroundC",
            createNameObj< RailMoveObjPress >,
        },
        {
            "ReverseGravityRoomPressGroundD",
            createNameObj< RailMoveObjPress >,
        },
        {
            "ReverseGravityRoomPressGroundE",
            createNameObj< RailMoveObjPress >,
        },
        {
            "RollingOvalPlanetParts",
            createNameObj< RotateMoveObj >,
        },
        {
            "TeresaMansionLightA",
            createNameObj< SwingLight >,
        },
        {
            "TeresaMansionLightB",
            createNameObj< SwingLight >,
        },
        {
            "TeresaMansionCandlestand",
            createNameObj< Candlestand >,
        },
        {
            "TeresaMansionHoleCover",
            createNameObj< SimpleBreakableObj >,
        },
        {
            "KoopaStatue",
            createNameObj< SimpleBreakableObj >,
        },
        {
            "TeresaPicture",
            createNameObj< SimpleTextureSwitchChangeObj >,
        },
        {
            "KinokoOneUpPicture",
            createNameObj< SimpleTextureSwitchChangeObj >,
        },
        {
            "FireBar",
            createNameObj< FireBar >,
        },
        {
            "BattleShipMovePartsA",
            createNameObj< RotateMoveObj >,
        },
        {
            "BattleShipMovePartsB",
            createNameObj< RotateMoveObj >,
        },
        {
            "BattleShipElevator",
            createNameObj< BattleShipElevator >,
        },
        {
            "BattleShipElevatorCover",
            createNameObj< SimpleMapObj >,
        },
        {
            "TeresaRaceSpaceStickA",
            createNameObj< SimpleMapObj >,
        },
        {
            "TeresaRaceSpaceStickB",
            createNameObj< SimpleMapObj >,
        },
        {
            "TeresaRaceSpaceStickC",
            createNameObj< SimpleMapObj >,
        },
        {
            "TeresaRacePartsA",
            createNameObj< RotateMoveObj >,
        },
        {
            "TeresaRacePartsB",
            createNameObj< AnmModelBindMove >,
        },
        {
            "NeedlePlant",
            createNameObj< NeedlePlant >,
        },
        {
            "NeedlePlantFly",
            createNameObj< NeedlePlant >,
        },
        {
            "BeeWallClimbPartsA",
            createNameObj< RailMoveObj >,
        },
        {
            "PeachCastleTownAfterAttack",
            createNameObj< SimpleMapObj >,
        },
        {
            "PeachCastleTownBeforeAttack",
            createNameObj< SimpleMapObj >,
        },
        {
            "PeachCastleTownGate",
            createNameObj< SimpleMapObj >,
        },
        {
            "PhantomDecoratePartsA",
            createNameObj< SimpleEnvironmentObj >,
        },
        {
            "PhantomDecoratePartsB",
            createNameObj< SimpleEnvironmentObj >,
        },
        {
            "PhantomDecoratePartsHole",
            createNameObj< SimpleEnvironmentObj >,
        },
        {
            "CocoonStepA",
            createNameObj< SimpleMapObj >,
        },
        {
            "CocoonStepB",
            createNameObj< SimpleMapObj >,
        },
        {
            "SpaceCannonLauncher",
            createNameObj< SimpleMapObj >,
        },
        {
            "TrapBaseA",
            createNameObj< SimpleMapObj >,
        },
        {
            "SweetsDecoratePartsSpoon",
            createNameObj< RotateMoveObj >,
        },
        {
            "SweetsDecoratePartsFork",
            createNameObj< RotateMoveObj >,
        },
        {
            "SeesawMoveNutA",
            createNameObj< SeesawMoveNut >,
        },
        {
            "SeesawMoveNutB",
            createNameObj< SeesawMoveNut >,
        },
        {
            "SeesawMoveNutC",
            createNameObj< SeesawMoveNut >,
        },
        {
            "TwisterSan",
            createNameObj< Sandstorm >,
        },
        {
            "TwisterSanSlim",
            createNameObj< Sandstorm >,
        },
        {
            "SunakazeKun",
            createNameObj< Sandstorm >,
        },
        {
            "SandStreamMoveStepsA",
            createNameObj< RotateMoveObj >,
        },
        {
            "SandStreamMoveStepsB",
            createNameObj< RotateMoveObj >,
        },
        {
            "WaterLeakPipe",
            createNameObj< WaterLeakPipe >,
        },
        {
            "BroadBeanMoveStepA",
            createNameObj< RailMoveObj >,
        },
        {
            "BroadBeanMoveStepB",
            createNameObj< RailMoveObj >,
        },
        {
            "ColorPencil",
            createNameObj< SimpleMapObj >,
        },
        {
            "TeresaRacePartsBallA",
            createNameObj< SimpleMapObj >,
        },
        {
            "SandCapsulePressGround",
            createNameObj< SandCapsulePressGround >,
        },
        {
            "RayGunPlanetPartsScrew",
            createNameObj< RailRotateMoveObj >,
        },
        {
            "RayGunPlanetPartsGear",
            createNameObj< RotateMoveObj >,
        },
        {
            "BreakDownFixStepA",
            createNameObj< SimpleMapObj >,
        },
        {
            "SandStreamHighTowerMoveStepA",
            createNameObj< RailMoveObj >,
        },
        {
            "DriftWood",
            createNameObj< DriftWood >,
        },
        {
            "ToyFactoryDecoratePartsGearA",
            createNameObj< RotateMoveObj >,
        },
        {
            "MiniMechaKoopaPartsMoveStepA",
            createNameObj< RailMoveObj >,
        },
        {
            "MiniMechaKoopaPartsGear",
            createNameObj< RotateMoveObj >,
        },
        {
            "MiniMechaKoopaPartsCage",
            createNameObj< RotateMoveObj >,
        },
        {
            "AsteroidBlockRotateStepA",
            createNameObj< RotateMoveObj >,
        },
        {
            "SandUpDownTowerBreakableWallA",
            createNameObj< SimpleBreakableObj >,
        },
        {
            "SandUpDownTowerBreakableWallB",
            createNameObj< SimpleBreakableObj >,
        },
        {
            "HoleDeathSandMoveStepA",
            createNameObj< RailMoveObj >,
        },
        {
            "SandUpDownTowerMoveStepA",
            createNameObj< RailMoveObj >,
        },
        {
            "DeathSandLandPartsA",
            createNameObj< SimpleMapObj >,
        },
        {
            "DeathSandLandPartsB",
            createNameObj< SimpleMapObj >,
        },
        {
            "DeathSandLandPlatformStepA",
            createNameObj< SimpleMapObj >,
        },
        {
            "DeathSandEnvironmentSapotenA",
            createNameObj< SimpleEnvironmentObj >,
        },
        {
            "DeathSandEnvironmentSapotenB",
            createNameObj< SimpleEnvironmentObj >,
        },
        {
            "DeathSandEnvironmentRock",
            createNameObj< SimpleEnvironmentObj >,
        },
        {
            "DeathSandEnvironmentPyramid",
            createNameObj< SimpleEnvironmentObj >,
        },
        {
            "UFOSandObstacleA",
            createNameObj< SimpleMapObj >,
        },
        {
            "UFOSandObstacleB",
            createNameObj< SimpleMapObj >,
        },
        {
            "UFOSandObstacleC",
            createNameObj< SimpleMapObj >,
        },
        {
            "SweetDecoratePartsOrange",
            createNameObj< SimpleEnvironmentObj >,
        },
        {
            "CapsuleCage",
            createNameObj< CapsuleCage >,
        },
        {
            "KameckShipLv1",
            createNameObj< SimpleMapObj >,
        },
        {
            "LavaBallRisingPlanetLava",
            createNameObj< LavaBallRisingPlanetLava >,
        },
        {
            "StrongBlock",
            createNameObj< SimpleMapObj >,
        },
        {
            "ChoConveyorChocoA",
            createNameObj< SimpleMapObj >,
        },
        {
            "ChoConveyorMoveChocoA",
            createNameObj< RailMoveObj >,
        },
        {
            "HoneyQueenLeafA",
            createNameObj< SimpleSeesawObj >,
        },
        {
            "ForestHomePartsTree",
            createNameObj< SimpleMapObj >,
        },
        {
            "ForestHomePartsTreeTower",
            createNameObj< SimpleMapObj >,
        },
        {
            "RosettaChair",
            createNameObj< RosettaChair >,
        },
        {
            "GravityLightA",
            createNameObj< GravityLight >,
        },
        {
            "GravityLightRoad",
            createNameObj< GravityLightRoad >,
        },
        {
            "PoltaBattlePlanetPartsA",
            createNameObj< SimpleMapObj >,
        },
        {
            "HeavenlyBeachTimerStepA",
            createNameObj< SimpleTimerObj >,
        },
        {
            "HeavenlyBeachTimerStepB",
            createNameObj< SimpleTimerObj >,
        },
        {
            "ReverseKingdomTreeA",
            createNameObj< SimpleMapObj >,
        },
        {
            "FloaterOtakingFloaterA",
            createNameObj< LavaFloater >,
        },
        {
            "WindMillPropeller",
            createNameObj< RotateMoveObj >,
        },
        {
            "WindMillPropellerMini",
            createNameObj< RotateMoveObj >,
        },
        {
            "LavaRotateStepsRotatePartsA",
            createNameObj< RotateMoveObj >,
        },
        {
            "LavaRotateStepsRotatePartsB",
            createNameObj< RotateMoveObj >,
        },
        {
            "LavaRotateStepsRotatePartsC",
            createNameObj< RotateMoveObj >,
        },
        {
            "LavaRotateStepsRotatePartsD",
            createNameObj< RotateMoveObj >,
        },
        {
            "QuickSand2DMovePartsA",
            createNameObj< RotateMoveObj >,
        },
        {
            "BiriBiriBegomanSpikePistonA",
            createNameObj< RailMoveObj >,
        },
        {
            "HugeBattleShipPlanetEntrance",
            createNameObj< SimpleMapObj >,
        },
        {
            "MysteryGravityRoomBridgeA",
            createNameObj< SimpleMapObj >,
        },
        {
            "DeadLeaves",
            createNameObj< DeadLeaves >,
        },
        {
            "PrologueDirector",
            createNameObj< PrologueDirector >,
        },
        {
            "DeathPromenadeMovePartsSpuareA",
            createNameObj< RailMoveObj >,
        },
        {
            "DeathPromenadeRotateCircleL",
            createNameObj< RotateMoveObj >,
        },
        {
            "DeathPromenadeRotateCircleS",
            createNameObj< RotateMoveObj >,
        },
        {
            "FlexibleSandPlanetPartsA",
            createNameObj< FlexibleSphere >,
        },
        {
            "DarkHopperRotateStepA",
            createNameObj< SimpleClipPartsObj >,
        },
        {
            "DarkHopperPlanetPartsA",
            createNameObj< SimpleMapObj >,
        },
        {
            "DarkHopperPlanetPartsC",
            createNameObj< SimpleMapObj >,
        },
        {
            "DarkHopperPlanetPartsD",
            createNameObj< SimpleMapObj >,
        },
        {
            "ClockworkHandle",
            createNameObj< SimpleBreakableObj >,
        },
        {
            "MiniMechaKoopaPartsFan",
            createNameObj< SimpleMapObj >,
        },
        {
            "RockRoadCircleA",
            createNameObj< SimpleMapObj >,
        },
        {
            "AssemblyBlockPartsA",
            createNameObj< AssemblyBlock >,
        },
        {
            "AssemblyBlockPartsB",
            createNameObj< AssemblyBlock >,
        },
        {
            "AssemblyBlockPartsC",
            createNameObj< AssemblyBlock >,
        },
        {
            "AssemblyBlockPartsD",
            createNameObj< AssemblyBlock >,
        },
        {
            "AssemblyBlockPartsE",
            createNameObj< AssemblyBlock >,
        },
        {
            "AssemblyBlockPartsIceB",
            createNameObj< AssemblyBlock >,
        },
        {
            "AssemblyBlockPartsIceC",
            createNameObj< AssemblyBlock >,
        },
        {
            "AssemblyBlockPartsIceD",
            createNameObj< AssemblyBlock >,
        },
        {
            "AssemblyBlockPartsIceE",
            createNameObj< AssemblyBlock >,
        },
        {
            "AssemblyBlockPartsTimerA",
            createNameObj< AssemblyBlock >,
        },
        {
            "HellBallGuidePartsA",
            createNameObj< SimpleMapObj >,
        },
        {
            "HellBallRotatePartsA",
            createNameObj< RotateMoveObj >,
        },
        {
            "HellBallRotatePartsB",
            createNameObj< RotateMoveObj >,
        },
        {
            "HellBallRotatePartsC",
            createNameObj< RotateMoveObj >,
        },
        {
            "HellBallRotatePartsD",
            createNameObj< RotateMoveObj >,
        },
        {
            "HellBallRotatePartsE",
            createNameObj< RotateMoveObj >,
        },
        {
            "HellBallRotatePartsF",
            createNameObj< RotateMoveObj >,
        },
        {
            "HellBallRotatePartsG",
            createNameObj< RotateMoveObj >,
        },
        {
            "IceSlipRoad",
            createNameObj< SimpleMapObj >,
        },
        {
            "CandyLiftA",
            createNameObj< RotateMoveObj >,
        },
        {
            "CandyLiftB",
            createNameObj< RotateMoveObj >,
        },
        {
            "SurfingRaceTutorialParts",
            createNameObj< SimpleMapObj >,
        },
        {
            "SurfingRaceMainGate",
            createNameObj< SimpleMapObj >,
        },
        {
            "SurfingRaceSubGate",
            createNameObj< SimpleMapObj >,
        },
        {
            "SurfingRaceStep",
            createNameObj< SimpleMapObj >,
        },
        {
            "SurfingRaceSignBoard",
            createNameObj< SimpleMapObj >,
        },
        {
            "SurfingRaceVictoryStand",
            createNameObj< SimpleMapObj >,
        },
        {
            "HeavensDoorFlowerA",
            createNameObj< SimpleMapObjNoSilhouetted >,
        },
        {
            "HeavensDoorHouseDoor",
            createNameObj< SimpleMapObj >,
        },
        {
            "HeavensDoorAppearStepAAfter",
            createNameObj< SimpleMapObj >,
        },
        {
            "HeavensDoorAppearStepA",
            createNameObj< HeavensDoorDemoObj >,
        },
        {
            "HeavensDoorMiddleRotatePartsA",
            createNameObj< RotateMoveObj >,
        },
        {
            "HeavensDoorMiddleRotatePartsB",
            createNameObj< RotateMoveObj >,
        },
        {
            "HeavensDoorInsideCage",
            createNameObj< HeavensDoorDemoObj >,
        },
        {
            "HeavensDoorInsidePlanetPartsA",
            createNameObj< HeavensDoorDemoObj >,
        },
        {
            "HeavensDoorInsideRotatePartsA",
            createNameObj< RotateMoveObj >,
        },
        {
            "HeavensDoorInsideRotatePartsB",
            createNameObj< RotateMoveObj >,
        },
        {
            "HeavensDoorInsideRotatePartsC",
            createNameObj< RotateMoveObj >,
        },
        {
            "MechaKoopaPartsHead",
            createNameObj< MechaKoopaPartsHead >,
        },
        {
            "MechaKoopaPartsCollar",
            createNameObj< RotateMoveObj >,
        },
        {
            "MechaKoopaPartsBreast",
            createNameObj< RailMoveObjBreakAtEnd >,
        },
        {
            "MechaKoopaPartsArmRight",
            createNameObj< MechaKoopaPartsArm >,
        },
        {
            "MechaKoopaPartsArmLeft",
            createNameObj< MechaKoopaPartsArm >,
        },
        {
            "MechaKoopaPartsBody",
            createNameObj< SimpleMapObj >,
        },
        {
            "MechaKoopaPartsCaterpillar",
            createNameObj< AnmModelSwitchMove >,
        },
        {
            "MechaKoopaPartsRollerA",
            createNameObj< SimpleMapObj >,
        },
        {
            "MechaKoopaPartsMoveStepA",
            createNameObj< RailMoveObjSwitchShadow >,
        },
        {
            "MechaKoopaPartsWreckA",
            createNameObj< SimpleMapObj >,
        },
        {
            "CannonFortressBreakStepA",
            createNameObj< CannonFortressBreakStep >,
        },
        {
            "CannonFortressRotateBreakStepA",
            createNameObj< CannonFortressBreakStep >,
        },
        {
            "CannonFortressRotateBreakStepB",
            createNameObj< CannonFortressBreakStep >,
        },
        {
            "CannonFortressRotateBreakStepC",
            createNameObj< CannonFortressBreakStep >,
        },
        {
            "CannonFortressMoveWall",
            createNameObj< RailMoveObjPress >,
        },
        {
            "SpaceShipStep",
            createNameObj< SpaceShipStep >,
        },
        {
            "QuarterRollGravityRoomArrow",
            createNameObj< QuarterRollGravityRoomArrow >,
        },
        {
            "NutShipFleetBridge",
            createNameObj< PhantomShipBridge >,
        },
        {
            "IceVolcanoMoveStepA",
            createNameObj< RailMoveObj >,
        },
        {
            "IceVolcanoBalanceStepA",
            createNameObj< SimpleSeesawObj >,
        },
        {
            "IceVolcanoBalanceStepB",
            createNameObj< SimpleSeesawObj >,
        },
        {
            "IceVolcanoFloaterA",
            createNameObj< LavaFloater >,
        },
        {
            "IceVolcanoFloaterB",
            createNameObj< LavaFloater >,
        },
        {
            "IceVolcanoAppearStepA",
            createNameObj< AnmModelSwitchMove >,
        },
        {
            "IceVolcanoAppearStepB",
            createNameObj< AnmModelSwitchMove >,
        },
        {
            "IceVolcanoClimbingWall",
            createNameObj< AnmModelSwitchMove >,
        },
        {
            "IceMountainFloaterA",
            createNameObj< SimpleFloaterObj >,
        },
        {
            "IceMountainSeesawNoSlipA",
            createNameObj< SimpleSeesawObj >,
        },
        {
            "IceMountainSeesawSlipA",
            createNameObj< SimpleSeesawObj >,
        },
        {
            "IceMountainSeesawSlipB",
            createNameObj< SimpleSeesawObj >,
        },
        {
            "IceMountainSeesawSlipC",
            createNameObj< SimpleSeesawObj >,
        },
        {
            "IceMountainSeesawSlipD",
            createNameObj< SimpleSeesawObj >,
        },
        {
            "IceRingBumpyPartsA",
            createNameObj< SimpleMapObj >,
        },
        {
            "IceWaterUpDownParts",
            createNameObj< IceVolcanoUpDownPlane >,
        },
        {
            "LavaUpDownParts",
            createNameObj< IceVolcanoUpDownPlane >,
        },
        {
            "IceUpDownShutterDoorA",
            createNameObj< AnmModelSwitchMove >,
        },
        {
            "IceLavaIslandSnowStepA",
            createNameObj< SimpleMapObj >,
        },
        {
            "IceLavaIslandIceMovableStepA",
            createNameObj< RailMoveObj >,
        },
        {
            "IceLavaIslandLavaMovableStepA",
            createNameObj< RailMoveObj >,
        },
        {
            "HoleBeltConveyerPartsA",
            createNameObj< RailMoveObj >,
        },
        {
            "HoleBeltConveyerPartsB",
            createNameObj< RailMoveObj >,
        },
        {
            "HoleBeltConveyerPartsC",
            createNameObj< RailMoveObj >,
        },
        {
            "HoleBeltConveyerPartsD",
            createNameObj< RailMoveObj >,
        },
        {
            "HoleBeltConveyerPartsE",
            createNameObj< RailMoveObj >,
        },
        {
            "HoleBeltConveyerPartsF",
            createNameObj< RailMoveObj >,
        },
        {
            "HoleBeltConveyerPartsG",
            createNameObj< RotateMoveObj >,
        },
        {
            "HoleBeltConveyerPartsH",
            createNameObj< RailMoveObj >,
        },
        {
            "ChallengeBallAccelCylinderA",
            createNameObj< RotateMoveObj >,
        },
        {
            "ChallengeBallGearA",
            createNameObj< RotateMoveObj >,
        },
        {
            "ChallengeBallMoveGroundA",
            createNameObj< RailMoveObj >,
        },
        {
            "ChallengeBallMoveGroundB",
            createNameObj< RailMoveObj >,
        },
        {
            "ChallengeBallRotateBridgeA",
            createNameObj< RotateMoveObj >,
        },
        {
            "ChallengeBallVanishingRoadA",
            createNameObj< SimpleMapObj >,
        },
        {
            "TrialBubbleTimerMoveWallA",
            createNameObj< TimerMoveWall >,
        },
        {
            "TrialBubbleRotateWallA",
            createNameObj< RotateMoveObj >,
        },
        {
            "TrialBubbleRevolvingPartsA",
            createNameObj< RotateMoveObj >,
        },
        {
            "TrialBubbleMoveWallA",
            createNameObj< RailMoveObj >,
        },
        {
            "CubeBubbleExHomeStep",
            createNameObj< SimpleMapObj >,
        },
        {
            "CubeBubbleExStartStep",
            createNameObj< SimpleMapObj >,
        },
        {
            "CubeBubbleExRotateWallS",
            createNameObj< RotateMoveObj >,
        },
        {
            "CubeBubbleExRotateWallL",
            createNameObj< RotateMoveObj >,
        },
        {
            "CubeBubbleExPartsA",
            createNameObj< SimpleMapObj >,
        },
        {
            "UFONormalB",
            createNameObj< UFOBreakable >,
        },
        {
            "UFONormalD",
            createNameObj< UFOBreakable >,
        },
        {
            "UFOStrongA",
            createNameObj< UFOSolid >,
        },
        {
            "UFOBattleStageC",
            createNameObj< UFOSolid >,
        },
        {
            "UFOBattleStageD",
            createNameObj< UFOSolid >,
        },
        {
            "UFOBattleStageE",
            createNameObj< UFOSolid >,
        },
        {
            "UFOKinoko",
            createNameObj< UFOKinoko >,
        },
        {
            "UFOKinokoLanding",
            createNameObj< SimpleMapObj >,
        },
        {
            "UFOBlueStarCupsule",
            createNameObj< BlueStarCupsulePlanet >,
        },
        {
            "KoopaShipA",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaShipB",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaShipC",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaShipD",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaShipE",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaShipSeesawStep",
            createNameObj< SimpleSeesawObj >,
        },
        {
            "KoopaJrSmallShipA",
            createNameObj< RailMoveObj >,
        },
        {
            "KoopaJrSmallShipAGuidePoint",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaJrKillerShipA",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaJrNormalShipA",
            createNameObj< SimpleMapObj >,
        },
        {
            "WaterRoadCaveStepA",
            createNameObj< RailMoveObj >,
        },
        {
            "WaterRoadCaveStepB",
            createNameObj< SimpleMapObj >,
        },
        {
            "WaterRoadCaveRotateGround",
            createNameObj< RotateMoveObj >,
        },
        {
            "SubmarineVolcanoInside",
            createNameObj< SimpleMapObj >,
        },
        {
            "SubmarineVolcanoBigColumn",
            createNameObj< SubmarineVolcanoBigColumn >,
        },
        {
            "SubmarineVolcanoSmallColumn",
            createNameObj< SubmarineVolcanoBigColumn >,
        },
        {
            "OnimasuPlanetPartsGoal",
            createNameObj< SimpleMapObj >,
        },
        {
            "OnimasuPlanetObstaclePartsA",
            createNameObj< SimpleMapObj >,
        },
        {
            "OnimasuPlanetRailMovePartsA",
            createNameObj< RailMoveObj >,
        },
        {
            "OnimasuPlanetRotatePartsA",
            createNameObj< RotateMoveObj >,
        },
        {
            "OnimasuPlanetRotatePartsB",
            createNameObj< RotateMoveObj >,
        },
        {
            "PhantomShipPropellerSmall",
            createNameObj< AnmModelSwitchMove >,
        },
        {
            "PhantomShipPropellerBig",
            createNameObj< AnmModelSwitchMove >,
        },
        {
            "PhantomShipHandle",
            createNameObj< PhantomShipHandle >,
        },
        {
            "PhantomShipBridge",
            createNameObj< PhantomShipBridge >,
        },
        {
            "TakoBarrelA",
            createNameObj< SimpleBreakableObj >,
        },
        {
            "TakoBarrelB",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaRestarterVs3",
            createNameObj< KoopaRestarterVs3 >,
        },
        {
            "KoopaPowerUpSwitch",
            createNameObj< KoopaPowerUpSwitch >,
        },
        {
            "KoopaSwitchKeeper",
            createNameObj< KoopaSwitchKeeper >,
        },
        {
            "KoopaViewSwitchKeeper",
            createNameObj< KoopaViewSwitchKeeper >,
        },
        {
            "KoopaBattleMapStair",
            createNameObj< KoopaBattleMapStair >,
        },
        {
            "KoopaBattleMapPlate",
            createNameObj< KoopaBattleMapPlate >,
        },
        {
            "KoopaBattleMapCoinPlate",
            createNameObj< KoopaBattleMapCoinPlate >,
        },
        {
            "KoopaBattleMapStairBig",
            createNameObj< KoopaBattleMapStair >,
        },
        {
            "KoopaBattleMapStairTurn",
            createNameObj< KoopaBattleMapStair >,
        },
        {
            "KoopaBattleMapStairturnAppear",
            createNameObj< TimeAppearObj >,
        },
        {
            "KoopaVS1PartsSpiralRoad",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaVS1PartsMoveStepA",
            createNameObj< RailMoveObj >,
        },
        {
            "KoopaVS1PartsMoveStepB",
            createNameObj< RailMoveObj >,
        },
        {
            "KoopaVS1PartsRotateMoveStepA",
            createNameObj< RailRotateMoveObj >,
        },
        {
            "KoopaVS1PartsRotateMoveStepB",
            createNameObj< RailRotateMoveObj >,
        },
        {
            "KoopaVS1PartsReverseGRoad",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaVS1PartsStairRoad",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaVS1PartsBattleStage",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaVS2PartsReverseGRoadA",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaVS2PartsReverseGRoadB",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaVS2PartsStartRestStep",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaVS2PartsRestStepA",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaVS2PartsRestStepB",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaVS2PartsRestStepC",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaVS2PartsRestClipC",
            createNameObj< SimpleClipPartsObj >,
        },
        {
            "KoopaVS2PartsRestStepD",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaVS2PartsRestStepE",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaVS2PartsRestStepF",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaVS2PartsRestStepG",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaVS2PartsDarkMatterA",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaVS2PartsDarkMatterB",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaVS2PartsDarkMatterC",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaVS2PartsDarkMatterD",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaVS2PartsDarkMatterE",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaVS2PartsStartMoveStepA",
            createNameObj< RotateMoveObj >,
        },
        {
            "KoopaVS2PartsStartMoveStepB",
            createNameObj< RotateMoveObj >,
        },
        {
            "KoopaVS2PartsStartMoveStepC",
            createNameObj< RailMoveObj >,
        },
        {
            "KoopaVS2PartsRollingStep",
            createNameObj< RotateMoveObj >,
        },
        {
            "KoopaVS2Parts2DMoveStepBarA",
            createNameObj< RailMoveObj >,
        },
        {
            "KoopaVS2Parts2DMoveStepBarB",
            createNameObj< RailMoveObj >,
        },
        {
            "KoopaVS2Parts2DMoveStepSBarB",
            createNameObj< RailMoveObj >,
        },
        {
            "KoopaVS2Parts2DMoveStepConvex",
            createNameObj< RailMoveObjPress >,
        },
        {
            "KoopaVS2Parts2DMoveStepLShape",
            createNameObj< RailMoveObj >,
        },
        {
            "KoopaVS2Parts2DMoveStepCross",
            createNameObj< RailMoveObj >,
        },
        {
            "KoopaVS2PartsJoinedMoveStep",
            createNameObj< RailMoveObj >,
        },
        {
            "KoopaVS2PartsJoinedMoveClip",
            createNameObj< SimpleClipPartsObj >,
        },
        {
            "KoopaVS2PartsSquareMoveStepA",
            createNameObj< RailMoveObj >,
        },
        {
            "KoopaVS2PartsSquareMoveClipA",
            createNameObj< SimpleClipPartsObj >,
        },
        {
            "KoopaVS2PartsSquareMoveStepB",
            createNameObj< RailMoveObj >,
        },
        {
            "KoopaVS2PartsSquareMoveClipB",
            createNameObj< SimpleClipPartsObj >,
        },
        {
            "KoopaVS2PartsClipAreaDisplayA",
            createNameObj< RailMoveIndirectObj >,
        },
        {
            "KoopaVS2PartsStairBig",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaVS2Parts2DRailGuideA",
            createNameObj< SimpleMapObj >,
        },
        {
            "KoopaVS3RotateStepA",
            createNameObj< RotateMoveObj >,
        },
        {
            "KoopaVS3RotateStepB",
            createNameObj< RotateMoveObj >,
        },
        {
            "KoopaVS3RotateStepD",
            createNameObj< RotateMoveObj >,
        },
        {
            "KoopaVS3RotateStepsA",
            createNameObj< RotateMoveObj >,
        },
        {
            "KoopaVS3Parts2DWallA",
            createNameObj< SimpleMapObj >,
        },
        {
            "TreasureBoxCrackedEmpty",
            createNameObj< TreasureBoxCracked >,
        },
        {
            "TreasureBoxCrackedCoin",
            createNameObj< TreasureBoxCracked >,
        },
        {
            "TreasureBoxCrackedYellowChip",
            createNameObj< TreasureBoxCracked >,
        },
        {
            "TreasureBoxCrackedBlueChip",
            createNameObj< TreasureBoxCracked >,
        },
        {
            "TreasureBoxCrackedKinokoOneUp",
            createNameObj< TreasureBoxCracked >,
        },
        {
            "TreasureBoxCrackedKinokoLifeUp",
            createNameObj< TreasureBoxCracked >,
        },
        {
            "TreasureBoxCrackedAirBubble",
            createNameObj< TreasureBoxCracked >,
        },
        {
            "TreasureBoxCrackedPowerStar",
            createNameObj< TreasureBoxCracked >,
        },
        {
            "TreasureBoxEmpty",
            createNameObj< TreasureBoxCracked >,
        },
        {
            "TreasureBoxCoin",
            createNameObj< TreasureBoxCracked >,
        },
        {
            "TreasureBoxYellowChip",
            createNameObj< TreasureBoxCracked >,
        },
        {
            "TreasureBoxBlueChip",
            createNameObj< TreasureBoxCracked >,
        },
        {
            "TreasureBoxKinokoOneUp",
            createNameObj< TreasureBoxCracked >,
        },
        {
            "TreasureBoxKinokoLifeUp",
            createNameObj< TreasureBoxCracked >,
        },
        {
            "TreasureBoxGoldEmpty",
            createNameObj< TreasureBoxCracked >,
        },
        {
            "OceanRingRuinsMove",
            createNameObj< RailMoveObj >,
        },
        {
            "OceanRingRuinsGearSmall",
            createNameObj< RotateMoveObj >,
        },
        {
            "OceanRingRuinsGearBig",
            createNameObj< RotateMoveObj >,
        },
        {
            "OceanRingRuinsColumn",
            createNameObj< SimpleMapObj >,
        },
        {
            "OceanRingRuinsBase",
            createNameObj< SimpleMapObj >,
        },
        {
            "KameckShip",
            createNameObj< SimpleMapObj >,
        },
        {
            "BeachParasol",
            createNameObj< SimpleMapObj >,
        },
        {
            "BeachChair",
            createNameObj< SimpleMapObj >,
        },
        {
            "GhostShipCaveMoveGroundA",
            createNameObj< RailMoveObj >,
        },
        {
            "GhostShipCaveMoveGroundB",
            createNameObj< RailMoveObj >,
        },
        {
            "PhantomShipBoxFloaterA",
            createNameObj< PhantomShipBoxFloater >,
        },
        {
            "PhantomShipBoxFloaterB",
            createNameObj< PhantomShipBoxFloater >,
        },
        {
            "PhantomShipBoxFloaterC",
            createNameObj< PhantomShipBoxFloater >,
        },
        {
            "PhantomCaveStepA",
            createNameObj< SimpleMapObj >,
        },
        {
            "GhostShipCaveClosedRockA",
            createNameObj< SimpleMapObj >,
        },
        {
            "GhostShipBrokenHead",
            createNameObj< SimpleMapObj >,
        },
        {
            "CannonUnderConstructionA",
            createNameObj< SimpleMapObj >,
        },
        {
            "CannonUnderConstructionB",
            createNameObj< SimpleMapObj >,
        },
        {
            "GhostShipCavePipeCollision",
            createNameObj< InvisiblePolygonObj >,
        },
        {
            "AstroMapBoard",
            createNameObj< AstroMapBoard >,
        },
        {
            "AstroCore",
            createNameObj< AstroCore >,
        },
        {
            "AstroCountDownPlate",
            createNameObj< AstroCountDownPlate >,
        },
        {
            "AstroDome",
            createNameObj< AstroDome >,
        },
        {
            "AstroDomeEntrance",
            createNameObj< AstroMapObj >,
        },
        {
            "AstroStarPlate",
            createNameObj< AstroMapObj >,
        },
        {
            "AstroBaseA",
            createNameObj< AstroMapObj >,
        },
        {
            "AstroBaseB",
            createNameObj< AstroMapObj >,
        },
        {
            "AstroBaseC",
            createNameObj< AstroMapObj >,
        },
        {
            "AstroBaseKitchen",
            createNameObj< AstroMapObj >,
        },
        {
            "AstroBaseCenterA",
            createNameObj< AstroMapObj >,
        },
        {
            "AstroBaseCenterB",
            createNameObj< AstroMapObj >,
        },
        {
            "AstroBaseCenterTop",
            createNameObj< AstroMapObj >,
        },
        {
            "AstroRotateStepA",
            createNameObj< AstroMapObj >,
        },
        {
            "AstroRotateStepB",
            createNameObj< AstroMapObj >,
        },
        {
            "AstroDecoratePartsA",
            createNameObj< AstroMapObj >,
        },
        {
            "AstroDecoratePartsGearA",
            createNameObj< AstroMapObj >,
        },
        {
            "AstroChildRoom",
            createNameObj< AstroMapObj >,
        },
        {
            "AstroParking",
            createNameObj< AstroMapObj >,
        },
        {
            "AstroLibrary",
            createNameObj< AstroMapObj >,
        },
        {
            "AstroRoomLibrary",
            createNameObj< SimpleMapObj >,
        },
        {
            "AstroTorchLightRed",
            createNameObj< AstroEffectObj >,
        },
        {
            "AstroTorchLightBlue",
            createNameObj< AstroEffectObj >,
        },
        {
            "EffectTicoS",
            createNameObj< AstroEffectObj >,
        },
        {
            "EffectTicoL",
            createNameObj< AstroEffectObj >,
        },
        {
            "UFOKinokoUnderConstruction",
            createNameObj< UFOKinokoUnderConstruction >,
        },
        {
            "UFOKinokoLandingAstro",
            createNameObj< SimpleMapObj >,
        },
        {
            "AstroDomeSky",
            createNameObj< AstroDomeSky >,
        },
        {
            "WhiteRoom",
            createNameObj< SimpleMapObj >,
        },
        {
            "VROrbit",
            createNameObj< ProjectionMapSky >,
        },
        {
            "VRDarkSpace",
            createNameObj< Sky >,
        },
        {
            "VRSandwichSun",
            createNameObj< ProjectionMapSky >,
        },
        {
            "SummerSky",
            createNameObj< Sky >,
        },
        {
            "GalaxySky",
            createNameObj< Sky >,
        },
        {
            "MilkyWaySky",
            createNameObj< Sky >,
        },
        {
            "HalfGalaxySky",
            createNameObj< ProjectionMapSky >,
        },
        {
            "GreenPlanetOrbitSky",
            createNameObj< ProjectionMapSky >,
        },
        {
            "PhantomSky",
            createNameObj< Sky >,
        },
        {
            "KoopaVS1Sky",
            createNameObj< ProjectionMapSky >,
        },
        {
            "KoopaVS2Sky",
            createNameObj< Sky >,
        },
        {
            "FamicomMarioSky",
            createNameObj< Sky >,
        },
        {
            "DesertSky",
            createNameObj< Sky >,
        },
        {
            "ChildRoomSky",
            createNameObj< Sky >,
        },
        {
            "AuroraSky",
            createNameObj< Sky >,
        },
        {
            "CloudSky",
            createNameObj< ProjectionMapSky >,
        },
        {
            "RockPlanetOrbitSky",
            createNameObj< ProjectionMapSky >,
        },
        {
            "StarrySky",
            createNameObj< Sky >,
        },
        {
            "AstroDomeSkyA",
            createNameObj< Sky >,
        },
        {
            "HomeAir",
            createNameObj< Air >,
        },
        {
            "SphereAir",
            createNameObj< PriorDrawAir >,
        },
        {
            "SunsetAir",
            createNameObj< ProjectionMapAir >,
        },
        {
            "FineAir",
            createNameObj< ProjectionMapAir >,
        },
        {
            "DimensionAir",
            createNameObj< AirFar100m >,
        },
        {
            "DarknessRoomAir",
            createNameObj< Air >,
        },
        {
            "TwilightAir",
            createNameObj< Air >,
        },
        {
            "ZoneHalo",
            createNameObj< Halo >,
        },
        {
            "PowerStarHalo",
            createNameObj< PowerStarHalo >,
        },
        {
            "Coin",
            MR::createDirectSetCoin,
        },
        {
            "PurpleCoin",
            MR::createDirectSetPurpleCoin,
        },
        {
            "RailCoin",
            MR::createRailCoin,
        },
        {
            "CircleCoinGroup",
            MR::createCircleCoinGroup,
        },
        {
            "PurpleRailCoin",
            MR::createRailPurpleCoin,
        },
        {
            "CirclePurpleCoinGroup",
            MR::createCirclePurpleCoinGroup,
        },
        {
            "PurpleCoinStarter",
            createNameObj< PurpleCoinStarter >,
        },
        {
            "CoinReplica",
            createNameObj< CoinReplica >,
        },
        {
            "QuestionCoin",
            createNameObj< QuestionCoin >,
        },
        {
            "BlueChip",
            createNameObj< BlueChip >,
        },
        {
            "YellowChip",
            createNameObj< YellowChip >,
        },
        {
            "BlueChipGroup",
            createNameObj< BlueChipGroup >,
        },
        {
            "YellowChipGroup",
            createNameObj< YellowChipGroup >,
        },
        {
            "StarPiece",
            createNameObj< StarPiece >,
        },
        {
            "StarPieceFlow",
            createNameObj< StarPieceGroup >,
        },
        {
            "StarPieceGroup",
            createNameObj< StarPieceGroup >,
        },
        {
            "StarPieceFollowGroup",
            createNameObj< StarPieceFollowGroup >,
        },
        {
            "StarPieceMother",
            createNameObj< StarPieceMother >,
        },
        {
            "StarPieceSpot",
            createNameObj< StarPieceSpot >,
        },
        {
            "OceanBowl",
            createNameObj< OceanBowl >,
        },
        {
            "OceanRing",
            createNameObj< OceanRing >,
        },
        {
            "OceanRingAndFlag",
            createNameObj< OceanRing >,
        },
        {
            "OceanSphere",
            createNameObj< OceanSphere >,
        },
        {
            "WhirlPool",
            createNameObj< WhirlPool >,
        },
        {
            "WhirlPoolAccelerator",
            createNameObj< WhirlPoolAccelerator >,
        },
        {
            "FishGroupA",
            createNameObj< FishGroup >,
        },
        {
            "FishGroupB",
            createNameObj< FishGroup >,
        },
        {
            "FishGroupC",
            createNameObj< FishGroup >,
        },
        {
            "FishGroupD",
            createNameObj< FishGroup >,
        },
        {
            "FishGroupE",
            createNameObj< FishGroup >,
        },
        {
            "FishGroupF",
            createNameObj< FishGroup >,
        },
        {
            "Flag",
            createNameObj< Flag >,
        },
        {
            "FlagKoopaA",
            createNameObj< Flag >,
        },
        {
            "FlagKoopaB",
            createNameObj< Flag >,
        },
        {
            "FlagRaceA",
            createNameObj< Flag >,
        },
        {
            "FlagKoopaCastle",
            createNameObj< Flag >,
        },
        {
            "FlagPeachCastleA",
            createNameObj< Flag >,
        },
        {
            "FlagPeachCastleB",
            createNameObj< Flag >,
        },
        {
            "FlagPeachCastleC",
            createNameObj< Flag >,
        },
        {
            "FlagSurfing",
            createNameObj< Flag >,
        },
        {
            "FlagTamakoro",
            createNameObj< Flag >,
        },
        {
            "SeaGullGroup",
            createNameObj< SeaGullGroup >,
        },
        {
            "WaterPlant",
            createNameObj< WaterPlant >,
        },
        {
            "HitWallTimerSwitch",
            createNameObj< HitWallTimerSwitch >,
        },
        {
            "PTimerSwitch",
            createNameObj< PTimerSwitch >,
        },
        {
            "CrystalSwitch",
            createNameObj< CrystalSwitch >,
        },
        {
            "HipDropSwitch",
            createNameObj< HipDropSwitch >,
        },
        {
            "HipDropTimerSwitch",
            createNameObj< HipDropTimerSwitch >,
        },
        {
            "DesertLandUpSwitch",
            createNameObj< DesertLandMoveSwitch >,
        },
        {
            "DesertLandDownSwitch",
            createNameObj< DesertLandMoveSwitch >,
        },
        {
            "ScrewSwitch",
            createNameObj< ScrewSwitch >,
        },
        {
            "ScrewSwitchReverse",
            createNameObj< ScrewSwitchReverse >,
        },
        {
            "PicketSwitch",
            createNameObj< PicketSwitch >,
        },
        {
            "PicketSwitchBig",
            createNameObj< PicketSwitch >,
        },
        {
            "ValveSwitch",
            createNameObj< ValveSwitch >,
        },
        {
            "KeySwitch",
            createNameObj< KeySwitch >,
        },
        {
            "SpinLeverSwitch",
            createNameObj< SpinLeverSwitch >,
        },
        {
            "EffectObjSample",
            createNameObj< EffectObjR1000F50 >,
        },
        {
            "ShootingStarArea",
            createNameObj< RandomEffectObj >,
        },
        {
            "IcePlanetLight",
            createNameObj< EffectObjR100F50SyncClipping >,
        },
        {
            "IceLayerBreak",
            createNameObj< EffectObjR500F50 >,
        },
        {
            "LavaSparksS",
            createNameObj< EffectObj20x20x10SyncClipping >,
        },
        {
            "LavaSparksL",
            createNameObj< EffectObj50x50x10SyncClipping >,
        },
        {
            "SpaceDustS",
            createNameObj< EffectObj20x20x10SyncClipping >,
        },
        {
            "SpaceDustL",
            createNameObj< EffectObj50x50x10SyncClipping >,
        },
        {
            "BirdLouseS",
            createNameObj< EffectObj20x20x10SyncClipping >,
        },
        {
            "BirdLouseL",
            createNameObj< EffectObj50x50x10SyncClipping >,
        },
        {
            "SnowS",
            createNameObj< EffectObj10x10x10SyncClipping >,
        },
        {
            "FallingSmallRock",
            createNameObj< FallingSmallRock >,
        },
        {
            "LavaHomeVolcanoFallingRock",
            createNameObj< EffectObjR1000F50 >,
        },
        {
            "Fountain",
            createNameObj< Fountain >,
        },
        {
            "FountainBig",
            createNameObj< FountainBig >,
        },
        {
            "WaterfallS",
            createNameObj< EffectObjR1000F50 >,
        },
        {
            "WaterfallL",
            createNameObj< EffectObjR1000F50 >,
        },
        {
            "DrainPipeBubbleA",
            createNameObj< EffectObjR500F50 >,
        },
        {
            "DrainPipeBubbleB",
            createNameObj< EffectObjR500F50 >,
        },
        {
            "WaterDropTop",
            createNameObj< EffectObjR1000F50 >,
        },
        {
            "WaterDropBottom",
            createNameObj< EffectObjR1000F50 >,
        },
        {
            "WaterDropMiddle",
            createNameObj< EffectObjR1000F50 >,
        },
        {
            "SubmarineSteam",
            createNameObj< SubmarineSteam >,
        },
        {
            "SubmarineVolcano",
            createNameObj< SubmarineSteam >,
        },
        {
            "WaterLayerBreak",
            createNameObj< EffectObjR500F50 >,
        },
        {
            "StarPieceClusterBreak",
            createNameObj< EffectObjR500F50 >,
        },
        {
            "FireworksA",
            createNameObj< RandomEffectObj >,
        },
        {
            "BattleShipExplosionRock",
            createNameObj< EffectObjR500F50 >,
        },
        {
            "BattleShipExplosionMetal",
            createNameObj< EffectObjR500F50 >,
        },
        {
            "ForestWaterfallS",
            createNameObj< EffectObjR1000F50 >,
        },
        {
            "ForestWaterfallL",
            createNameObj< EffectObjR1000F50 >,
        },
        {
            "SandBreezeS",
            createNameObj< EffectObj20x20x10SyncClipping >,
        },
        {
            "SandBreezeL",
            createNameObj< EffectObj50x50x10SyncClipping >,
        },
        {
            "SpringJetWater",
            createNameObj< SpringJetWater >,
        },
        {
            "SandUpDownKGBaseEffect",
            createNameObj< SandUpDownEffectObj >,
        },
        {
            "SandUpDownTowerTopEffect",
            createNameObj< SandUpDownEffectObj >,
        },
        {
            "UFOSandObstacleAEffect",
            createNameObj< SandUpDownEffectObj >,
        },
        {
            "UFOSandObstacleBEffect",
            createNameObj< SandUpDownEffectObj >,
        },
        {
            "UFOSandObstacleCEffect",
            createNameObj< SandUpDownEffectObj >,
        },
        {
            "GravityDust",
            createNameObj< EffectObjGravityDust >,
        },
        {
            "Steam",
            createNameObj< SimpleEffectObj >,
        },
        {
            "FallRedLeaf",
            createNameObj< EffectObj10x10x10SyncClipping >,
        },
        {
            "FallGreenLeaf",
            createNameObj< EffectObj10x10x10SyncClipping >,
        },
        {
            "IcicleRockLight",
            createNameObj< EffectObjR100F50SyncClipping >,
        },
        {
            "TwinFallLakeWaterFall",
            createNameObj< EffectObjR1000F50 >,
        },
        {
            "EffectTeresa",
            createNameObj< EffectObj50x50x10SyncClipping >,
        },
        {
            "UFOKinokoLandingBlackSmoke",
            createNameObj< EffectObjR500F50 >,
        },
        {
            "InvisibleWall10x10",
            createNameObj< InvisiblePolygonObj >,
        },
        {
            "InvisibleWall10x20",
            createNameObj< InvisiblePolygonObj >,
        },
        {
            "InvisibleWallJump10x10",
            createNameObj< InvisiblePolygonObj >,
        },
        {
            "InvisibleWallJump10x20",
            createNameObj< InvisiblePolygonObj >,
        },
        {
            "InvisibleWallGCapture10x10",
            createNameObj< InvisiblePolygonObjGCapture >,
        },
        {
            "InvisibleWallGCapture10x20",
            createNameObj< InvisiblePolygonObjGCapture >,
        },
        {
            "PolygonCodeRecoveryPlate",
            createNameObj< InvisiblePolygonObj >,
        },
        {
            "PolygonCodeRecoveryBowl",
            createNameObj< InvisiblePolygonObj >,
        },
        {
            "InvisibleWallBattleShipA",
            createNameObj< TransparentWall >,
        },
        {
            "InvisibleWaterfall",
            createNameObj< TransparentWall >,
        },
        {
            "InvisibleWaterfallTwinFallLake",
            createNameObj< TransparentWall >,
        },
        {
            "StatusFloor",
            createNameObj< TransparentWall >,
        },
        {
            "GlobalCubeGravity",
            MR::createGlobalCubeGravityObj,
        },
        {
            "GlobalConeGravity",
            MR::createGlobalConeGravityObj,
        },
        {
            "GlobalDiskGravity",
            MR::createGlobalDiskGravityObj,
        },
        {
            "GlobalDiskTorusGravity",
            MR::createGlobalDiskTorusGravityObj,
        },
        {
            "GlobalPlaneGravity",
            MR::createGlobalPlaneGravityObj,
        },
        {
            "GlobalPlaneGravityInBox",
            MR::createGlobalPlaneInBoxGravityObj,
        },
        {
            "GlobalPlaneGravityInCylinder",
            MR::createGlobalPlaneInCylinderGravityObj,
        },
        {
            "GlobalPointGravity",
            MR::createGlobalPointGravityObj,
        },
        {
            "GlobalSegmentGravity",
            MR::createGlobalSegmentGravityObj,
        },
        {
            "GlobalWireGravity",
            MR::createGlobalWireGravityObj,
        },
        {
            "MiniEggStarGalaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniHoneyBeeKingdomGalaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniFlipPanelExGalaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniSurfingLv1Galaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniTriLegLv1Galaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniStarDustGalaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniTamakoroExLv1Galaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniBattleShipGalaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniBreakDownPlanetGalaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniKoopaBattleVs1Galaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniHeavenlyBeachGalaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniCubeBubbleExLv1Galaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniPhantomGalaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniOceanFloaterLandGalaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniKoopaJrShipLv1Galaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniCosmosGardenGalaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniIceVolcanoGalaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniHoneyBeeExGalaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniSandClockGalaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniKoopaBattleVs2Galaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniReverseKingdomGalaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniOceanRingGalaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniFactoryGalaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniSkullSharkGalaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniFloaterOtaKingGalaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniOceanPhantomCaveGalaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniCannonFleetGalaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniDarkRoomGalaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniHellProminenceGalaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "MiniKoopaBattleVs3Galaxy",
            createNameObj< MiniatureGalaxy >,
        },
        {
            "SurprisedGalaxy",
            createNameObj< SurprisedGalaxy >,
        },
        {
            "SurpBeltConveyerExGalaxy",
            createNameObj< SurprisedGalaxy >,
        },
        {
            "SurpCocoonExGalaxy",
            createNameObj< SurprisedGalaxy >,
        },
        {
            "SurpTearDropGalaxy",
            createNameObj< SurprisedGalaxy >,
        },
        {
            "SurpTeresaMario2DGalaxy",
            createNameObj< SurprisedGalaxy >,
        },
        {
            "SurpSnowCapsuleGalaxy",
            createNameObj< SurprisedGalaxy >,
        },
        {
            "SurpTransformationExGalaxy",
            createNameObj< SurprisedGalaxy >,
        },
        {
            "SurpFishTunnelGalaxy",
            createNameObj< SurprisedGalaxy >,
        },
        {
            "SurpTamakoroExLv2Galaxy",
            createNameObj< SurprisedGalaxy >,
        },
        {
            "SurpSurfingLv2Galaxy",
            createNameObj< SurprisedGalaxy >,
        },
        {
            "SurpCubeBubbleExLv2Galaxy",
            createNameObj< SurprisedGalaxy >,
        },
        {
            "SurpPeachCastleFinalGalaxy",
            createNameObj< SurprisedGalaxy >,
        },
        {
            "ClipFieldSwitch",
            createNameObj< ClipFieldSwitch >,
        },
        {
            "FallOutFieldDraw",
            MR::createFallOutFieldDraw,
        },
        {
            "ClipFieldFillDraw",
            MR::createClipFieldFillDraw,
        },
        {
            "SpotLightVolumeDraw",
            MR::createVolumeDrawSpotLight,
        },
        {
            "GroupSwitchWatcher",
            createNameObj< GroupSwitchWatcher >,
        },
        {
            "RaceRail",
            createNameObj< RaceRail >,
        },
        {
            "TimerSwitch",
            createNameObj< TimerSwitch >,
        },
        {
            "SwitchSynchronizerReverse",
            createNameObj< SwitchSynchronizer >,
        },
        {
            "MovieStarter",
            createNameObj< MovieStarter >,
        },
        {
            "DemoGroup",
            createNameObj< DemoExecutor >,
        },
        {
            "DemoSubGroup",
            createNameObj< DemoCastSubGroup >,
        },
        {
            "AstroDomeDemoStarter",
            createNameObj< AstroDomeDemoStarter >,
        },
        {
            "AstroDomeGalaxySelector",
            createNameObj< AstroDomeGalaxySelector >,
        },
        {
            "StarReturnDemoStarter",
            createNameObj< StarReturnDemoStarter >,
        },
        {
            "GrandStarReturnDemoStarter",
            createNameObj< GrandStarReturnDemoStarter >,
        },
        {
            "ScenarioStarter",
            createNameObj< ScenarioStarter >,
        },
        {
            "FileSelector",
            createNameObj< FileSelector >,
        },
    };

    const char* const cPlayerArchiveLoaderObjTable[] = {
        "Hopper",          "BenefitItemInvincible", "MorphItemNeoBee", "MorphItemNeoFire",
        "MorphItemNeoFoo", "MorphItemNeoHopper",    "MorphItemNeoIce", "MorphItemNeoTeresa",
    };

    // cCreateTable by name, ASCII case folded as isEqualStringCase compares: open addressing, a slot holds its entry's
    // number + 1 (0 empty). A name listed twice keeps its first entry, the one a scan of the table found.
    const u32 cCreateIndexRoom = 4096;
    static_assert(ARRAY_SIZEU(cCreateTable) * 2 <= cCreateIndexRoom, "the index stays at most half full");
    u16 sCreateIndex[cCreateIndexRoom];
    bool sCreateIndexBuilt = false;

    u32 hashNameCase(const char* pName) {
        u32 hash = 2166136261u;
        for (const char* p = pName; *p != '\0'; p++) {
            u32 c = static_cast< u8 >(*p);
            if (c >= 'A' && c <= 'Z') {
                c += 'a' - 'A';
            }
            hash = (hash ^ c) * 16777619u;
        }
        return hash;
    }

    void buildCreateIndex() {
        for (u32 i = 0; i < ARRAY_SIZEU(cCreateTable); i++) {
            const char* const pName = cCreateTable[i].mName;
            if (pName == nullptr) {
                continue;
            }

            u32 slot = hashNameCase(pName) & (cCreateIndexRoom - 1);
            while (sCreateIndex[slot] != 0 && !MR::isEqualStringCase(cCreateTable[sCreateIndex[slot] - 1].mName, pName)) {
                slot = (slot + 1) & (cCreateIndexRoom - 1);
            }
            if (sCreateIndex[slot] == 0) {
                sCreateIndex[slot] = static_cast< u16 >(i + 1);
            }
        }
        sCreateIndexBuilt = true;
    }
};  // namespace

namespace NameObjFactory {
    CreatorFuncPtr getCreator(const char* pName) {
        // First: a plugin may also take the place of one of the game's own.
        if (const CreatorFuncPtr pPlugin = NebulaMods::findCreator(pName)) {
            return pPlugin;
        }

        pName = NebulaMods::objectBecomes(NEBULA_OBJECTS_PLACED, NebulaMods::objectLike(pName));
        if (pName == nullptr) {
            return nullptr;
        }
        // A change into a mod's own object (luma.json "objects", a plugin's class).
        if (const CreatorFuncPtr pPlugin = NebulaMods::findCreator(pName)) {
            return pPlugin;
        }

        if (PlanetMapCreatorFunction::isRegisteredObj(pName)) {
            return PlanetMapCreatorFunction::getPlanetMapCreator(pName);
        }

        const Name2CreateFunc* pName2CreateFunc = getName2CreateFunc(pName);

        if (pName2CreateFunc == nullptr) {
            return nullptr;
        }

        return pName2CreateFunc->mCreateFunc;
    }

    bool isPlayerArchiveLoaderObj(const char* pArchive) {
        for (u32 i = 0; i < ARRAY_SIZE(::cPlayerArchiveLoaderObjTable); i++) {
            if (MR::isEqualStringCase(::cPlayerArchiveLoaderObjTable[i], pArchive)) {
                return true;
            }
        }

        return false;
    }

    u32 getCreateTableNum() {
        return ARRAY_SIZEU(cCreateTable);
    }

    const char* getCreateTableName(u32 index) {
        return index < ARRAY_SIZEU(cCreateTable) ? cCreateTable[index].mName : nullptr;
    }

    const Name2CreateFunc* getName2CreateFunc(const char* pName) {
        if (!::sCreateIndexBuilt) {
            ::buildCreateIndex();
        }

        for (u32 slot = ::hashNameCase(pName) & (::cCreateIndexRoom - 1); ::sCreateIndex[slot] != 0; slot = (slot + 1) & (::cCreateIndexRoom - 1)) {
            const Name2CreateFunc* const pName2CreateFunc = &::cCreateTable[::sCreateIndex[slot] - 1];
            if (MR::isEqualStringCase(pName2CreateFunc->mName, pName)) {
                return pName2CreateFunc;
            }
        }

        return nullptr;
    }
};  // namespace NameObjFactory
