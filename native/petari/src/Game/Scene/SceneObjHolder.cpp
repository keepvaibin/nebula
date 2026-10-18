#include "Game/Scene/SceneObjHolder.hpp"
#include "Game/AreaObj/AreaObjContainer.hpp"
#include "Game/Boss/BossAccessor.hpp"
#include "Game/Boss/SkeletalFishBabyRailHolder.hpp"
#include "Game/Boss/SkeletalFishBossRailHolder.hpp"
#include "Game/Boss/TripodBossAccesser.hpp"
#include "Game/Camera/CameraContext.hpp"
#include "Game/Camera/CameraDirector.hpp"
#include "Game/Demo/DemoDirector.hpp"
#include "Game/Demo/PrologueDirector.hpp"
#include "Game/Effect/EffectSystem.hpp"
#include "Game/Enemy/BegomanBase.hpp"
#include "Game/Enemy/KabokuriFireHolder.hpp"
#include "Game/Enemy/KameckBeamHolder.hpp"
#include "Game/Enemy/KarikariDirector.hpp"
#include "Game/Enemy/TakoHeiInkHolder.hpp"
#include "Game/GameAudio/AudBgmConductor.hpp"
#include "Game/GameAudio/AudCameraWatcher.hpp"
#include "Game/GameAudio/AudEffectDirector.hpp"
#include "Game/Gravity/PlanetGravityManager.hpp"
#include "Game/LiveActor/AllLiveActorGroup.hpp"
#include "Game/LiveActor/ClippingDirector.hpp"
#include "Game/LiveActor/LiveActorGroupArray.hpp"
#include "Game/LiveActor/MessageSensorHolder.hpp"
#include "Game/LiveActor/MirrorCamera.hpp"
#include "Game/LiveActor/SensorHitChecker.hpp"
#include "Game/LiveActor/ShadowController.hpp"
#include "Game/LiveActor/ShadowSurfaceDrawer.hpp"
#include "Game/LiveActor/ShadowVolumeDrawer.hpp"
#include "Game/LiveActor/VolumeModelDrawer.hpp"
#include "Game/Map/Air.hpp"
#include "Game/Map/CollisionDirector.hpp"
#include "Game/Map/LightDirector.hpp"
#include "Game/Map/NamePosHolder.hpp"
#include "Game/Map/OceanHomeMapCtrl.hpp"
#include "Game/Map/PlanetMapCreator.hpp"
#include "Game/Map/QuakeEffectGenerator.hpp"
#include "Game/Map/RaceManager.hpp"
#include "Game/Map/SleepControllerHolder.hpp"
#include "Game/Map/SphereSelector.hpp"
#include "Game/Map/StageSwitch.hpp"
#include "Game/Map/SunshadeMapHolder.hpp"
#include "Game/Map/SwitchWatcherHolder.hpp"
#include "Game/Map/WaterAreaHolder.hpp"
#include "Game/Map/WaterPlant.hpp"
#include "Game/MapObj/AirBubbleHolder.hpp"
#include "Game/MapObj/ArrowSwitchMultiHolder.hpp"
#include "Game/MapObj/BigBubbleHolder.hpp"
#include "Game/MapObj/BigFanHolder.hpp"
#include "Game/MapObj/ChipHolder.hpp"
#include "Game/MapObj/ClipAreaDropHolder.hpp"
#include "Game/MapObj/ClipAreaHolder.hpp"
#include "Game/MapObj/ClipFieldFillDraw.hpp"
#include "Game/MapObj/CoinHolder.hpp"
#include "Game/MapObj/CoinRotater.hpp"
#include "Game/MapObj/EarthenPipe.hpp"
#include "Game/MapObj/ElectricRailHolder.hpp"
#include "Game/MapObj/FallOutFieldDraw.hpp"
#include "Game/MapObj/FirePressureBulletHolder.hpp"
#include "Game/MapObj/GCapture.hpp"
#include "Game/MapObj/MapPartsRailGuideHolder.hpp"
#include "Game/MapObj/MiniatureGalaxyHolder.hpp"
#include "Game/MapObj/Note.hpp"
#include "Game/MapObj/PurpleCoinHolder.hpp"
#include "Game/MapObj/SpiderThread.hpp"
#include "Game/MapObj/SpinDriverPathDrawer.hpp"
#include "Game/MapObj/StarPieceDirector.hpp"
#include "Game/MapObj/WarpPod.hpp"
#include "Game/MapObj/WaterPressureBulletHolder.hpp"
#include "Game/NPC/EventDirector.hpp"
#include "Game/NPC/MiiFaceIconHolder.hpp"
#include "Game/NPC/MiiFacePartsHolder.hpp"
#include "Game/NPC/NPCDirector.hpp"
#include "Game/NPC/TalkDirector.hpp"
#include "Game/NameObj/MovementOnOffGroupHolder.hpp"
#include "Game/NameObj/NameObjExecuteHolder.hpp"
#include "Game/NameObj/NameObjGroup.hpp"
#include "Game/Player/GroupChecker.hpp"
#include "Game/Player/MarioHolder.hpp"
#include "Game/Player/PlayerEvent.hpp"
#include "Game/Ride/FluffWind.hpp"
#include "Game/Ride/PlantLeaf.hpp"
#include "Game/Ride/PlantStalk.hpp"
#include "Game/Ride/SwingRope.hpp"
#include "Game/Ride/Trapeze.hpp"
#include "Game/Scene/PlacementStateChecker.hpp"
#include "Game/Scene/SceneDataInitializer.hpp"
#include "Game/Scene/SceneNameObjMovementController.hpp"
#include "Game/Scene/ScenePlayingResult.hpp"
#include "Game/Scene/StageDataHolder.hpp"
#include "Game/Scene/StopSceneController.hpp"
#include "Game/Screen/BloomEffect.hpp"
#include "Game/Screen/BloomEffectSimple.hpp"
#include "Game/Screen/CaptureScreenDirector.hpp"
#include "Game/Screen/CenterScreenBlur.hpp"
#include "Game/Screen/CinemaFrame.hpp"
#include "Game/Screen/CometRetryButton.hpp"
#include "Game/Screen/DepthOfFieldBlur.hpp"
#include "Game/Screen/GalaxyMapController.hpp"
#include "Game/Screen/GalaxyNamePlateDrawer.hpp"
#include "Game/Screen/GameSceneLayoutHolder.hpp"
#include "Game/Screen/HeatHazeEffect.hpp"
#include "Game/Screen/ImageEffectSystemHolder.hpp"
#include "Game/Screen/InformationObserver.hpp"
#include "Game/Screen/LensFlare.hpp"
#include "Game/Screen/MoviePlayerSimple.hpp"
#include "Game/Screen/MoviePlayingSequence.hpp"
#include "Game/Screen/OdhConverter.hpp"
#include "Game/Screen/PlayerActionGuidance.hpp"
#include "Game/Screen/SceneWipeHolder.hpp"
#include "Game/Screen/ScreenAlphaCapture.hpp"
#include "Game/Screen/ScreenBlurEffect.hpp"
#include "Game/Screen/StaffRoll.hpp"
#include "Game/System/GameSystem.hpp"
#include "Game/System/GameSystemSceneController.hpp"
#include "Game/Util/BaseMatrixFollowTargetHolder.hpp"
#include "Game/Util/FurCtrl.hpp"
#include "Game/Util/SceneUtil.hpp"
#include "Game/Util/ShareUtil.hpp"
#include "Game/Util/SingletonHolder.hpp"

SceneObjHolder::SceneObjHolder() {
    for (int i = 0; i < SceneObj_NumMax; i++) {
        mObj[i] = nullptr;
    }
}

NameObj* SceneObjHolder::create(int id) {
    NameObj* pObj = mObj[id];

    if (pObj != nullptr) {
        return pObj;
    }

    pObj = newEachObj(id);
    pObj->initWithoutIter();

    mObj[id] = pObj;

    return pObj;
}

NameObj* SceneObjHolder::getObj(int id) const {
    return mObj[id];
}

bool SceneObjHolder::isExist(int id) const {
    return mObj[id] != nullptr;
}

NameObj* SceneObjHolder::newEachObj(int id) {
    switch (id) {
    case SceneObj_SensorHitChecker:
        return new SensorHitChecker("\203\132\203\223\203\124\201\133\223\226\202\275\202\350" /* センサー当たり */);
    case SceneObj_CollisionDirector:
        return new CollisionDirector();
    case SceneObj_ClippingDirector:
        return new ClippingDirector();
    case SceneObj_DemoDirector:
        return new DemoDirector("\203\146\203\202\216\167\212\366" /* デモ指揮 */);
    case SceneObj_EventDirector:
        return new EventDirector();
    case SceneObj_EffectSystem:
        return new EffectSystem("\203\107\203\164\203\106\203\116\203\147\203\126\203\130\203\145\203\200" /* エフェクトシステム */, true);
    case SceneObj_LightDirector:
        return new LightDirector();
    case SceneObj_SceneDataInitializer:
        return new SceneDataInitializer();
    case SceneObj_StageDataHolder:
        return new StageDataHolder(MR::getCurrentStageName(), 0, true);
    case SceneObj_MessageSensorHolder:
        return new MessageSensorHolder("\203\126\203\130\203\145\203\200\224\304\227\160\203\132\203\223\203\124\201\133" /* システム汎用センサー */);
    case SceneObj_StageSwitchContainer:
        return new StageSwitchContainer();
    case SceneObj_SwitchWatcherHolder:
        return new SwitchWatcherHolder();
    case SceneObj_SleepControllerHolder:
        return new SleepControllerHolder();
    case SceneObj_AreaObjContainer:
        return new AreaObjContainer("\203\107\203\212\203\101\203\111\203\165\203\127\203\106\203\116\203\147\203\122\203\223\203\145\203\151\212\307\227\235" /* エリアオブジェクトコンテナ管理 */);
    case SceneObj_LiveActorGroupArray:
        return new LiveActorGroupArray("\203\111\203\165\203\127\203\106\203\116\203\147\203\117\203\213\201\133\203\166" /* オブジェクトグループ */);
    case SceneObj_MovementOnOffGroupHolder:
        return new MovementOnOffGroupHolder("Movement\203\117\203\213\201\133\203\166\212\307\227\235" /* Movementグループ管理 */);
    case SceneObj_CaptureScreenActor:
        return new CaptureScreenActor(MR::DrawType_CaptureScreenIndirect, "Indirect");
    case SceneObj_AudCameraWatcher:
        return new AudCameraWatcher();
    case SceneObj_AudEffectDirector:
        return new AudEffectDirector();
    case SceneObj_AudBgmConductor:
        return new AudBgmConductor();
    case SceneObj_MarioHolder:
        return new MarioHolder();
    case SceneObj_MirrorCamera:
        return new MirrorCamera("\213\276\227\160\203\112\203\201\203\211" /* 鏡用カメラ */);
    case SceneObj_CameraContext:
        return new CameraContext();
    case SceneObj_NameObjGroup:
        return new NameObjGroup("IgnorePauseNameObj", 16);
    case SceneObj_TalkDirector:
        return new TalkDirector("\211\357\230\142\203\146\203\102\203\214\203\116\203\136\201\133" /* 会話ディレクター */);
    case SceneObj_EventSequencer:
        return new EventSequencer();
    case SceneObj_StopSceneController:
        return new StopSceneController();
    case SceneObj_SceneNameObjMovementController:
        return new SceneNameObjMovementController();
    case SceneObj_ImageEffectSystemHolder:
        return new ImageEffectSystemHolder();
    case SceneObj_BloomEffect:
        return new BloomEffect("\203\165\203\213\201\133\203\200" /* ブルーム */);
    case SceneObj_BloomEffectSimple:
        return new BloomEffectSimple();
    case SceneObj_ScreenBlurEffect:
        return new ScreenBlurEffect("\211\346\226\312\203\165\203\211\201\133" /* 画面ブラー */);
    case SceneObj_DepthOfFieldBlur:
        return new DepthOfFieldBlur("\224\355\216\312\212\105\220\133\223\170\203\165\203\211\201\133" /* 被写界深度ブラー */);
    case SceneObj_SceneWipeHolder:
        return new SceneWipeHolder();
    case SceneObj_PlayerActionGuidance:
        return new PlayerActionGuidance();
    case SceneObj_ScenePlayingResult:
        return new ScenePlayingResult();
    case SceneObj_LensFlareDirector:
        return new LensFlareDirector();
    case SceneObj_FurDrawManager:
        return new FurDrawManager(64);
    case SceneObj_PlacementStateChecker:
        return new PlacementStateChecker("\203\111\203\165\203\127\203\106\203\116\203\147\224\172\222\165\217\363\221\324\202\314\212\304\216\213" /* オブジェクト配置状態の監視 */);
    case SceneObj_NamePosHolder:
        return new NamePosHolder();
    case SceneObj_NPCDirector:
        return new NPCDirector();
    case SceneObj_ResourceShare:
        return new ResourceShare();
    case SceneObj_MoviePlayerSimple:
        return new MoviePlayerSimple();
    case SceneObj_InformationObserver:
        return new InformationObserver();
    case SceneObj_CenterScreenBlur:
        return new CenterScreenBlur();
    case SceneObj_OdhConverter:
        return new OdhConverter();
    case SceneObj_CometRetryButton:
        return new CometRetryButton("\203\122\203\201\203\142\203\147\203\212\203\147\203\211\203\103\203\173\203\136\203\223" /* コメットリトライボタン */);
    case SceneObj_AllLiveActorGroup:
        return new AllLiveActorGroup();
    case SceneObj_CameraDirector:
        return new CameraDirector("\203\112\203\201\203\211\212\307\227\235" /* カメラ管理 */);
    case SceneObj_PlanetGravityManager:
        return new PlanetGravityManager("\217\144\227\315" /* 重力 */);
    case SceneObj_BaseMatrixFollowTargetHolder:
        return new BaseMatrixFollowTargetHolder("\215\163\227\361\222\307\220\217\220\346\203\212\203\130\203\147" /* 行列追随先リスト */, 256, 256);
    case SceneObj_GameSceneLayoutHolder:
        return new GameSceneLayoutHolder();
    case SceneObj_TripodBossAccesser:
        return new TripodBossAccesser("\216\117\213\162\203\173\203\130\203\101\203\116\203\132\203\124" /* 三脚ボスアクセサ */);
    case SceneObj_KameckBeamHolder:
        return new KameckBeamHolder();
    case SceneObj_KameckFireBallHolder:
        return new KameckFireBallHolder();
    case SceneObj_KameckBeamTurtleHolder:
        return new KameckBeamTurtleHolder();
    case SceneObj_KabokuriFireHolder:
        return new KabokuriFireHolder();
    case SceneObj_TakoHeiInkHolder:
        return new TakoHeiInkHolder();
    case SceneObj_SwingRopeGroup:
        return new SwingRopeGroup("\203\130\203\103\203\223\203\117\203\215\201\133\203\166\225\140\211\346" /* スイングロープ描画 */);
    case SceneObj_CoinHolder:
        return new CoinHolder("\203\122\203\103\203\223\212\307\227\235" /* コイン管理 */);
    case SceneObj_PurpleCoinHolder:
        return new PurpleCoinHolder();
    case SceneObj_CoinRotater:
        return new CoinRotater("\203\122\203\103\203\223\211\361\223\135\212\307\227\235" /* コイン回転管理 */);
    case SceneObj_AirBubbleHolder:
        return new AirBubbleHolder("\213\363\213\103\203\101\203\217\212\307\227\235" /* 空気アワ管理 */);
    case SceneObj_StarPieceDirector:
        return new StarPieceDirector("\203\130\203\136\201\133\203\163\201\133\203\130\216\167\212\366" /* スターピース指揮 */);
    case SceneObj_BegomanAttackPermitter:
        return new BegomanAttackPermitter("\203\170\201\133\203\123\203\175\203\223\215\125\214\202\213\226\211\302\216\322" /* ベーゴマン攻撃許可者 */);
    case SceneObj_BigFanHolder:
        return new BigFanHolder();
    case SceneObj_KarikariDirector:
        return new KarikariDirector("\203\112\203\212\203\112\203\212\203\146\203\102\203\214\203\116\203\136\201\133" /* カリカリディレクター */);
    case SceneObj_ShadowControllerHolder:
        return new ShadowControllerHolder();
    case SceneObj_ShadowVolumeDrawInit:
        return new ShadowVolumeDrawInit();
    case SceneObj_ShadowSurfaceDrawInit:
        return new ShadowSurfaceDrawInit("\220\205\226\312\211\145\225\140\211\346\217\211\212\372\211\273" /* 水面影描画初期化 */);
    case SceneObj_PlantStalkDrawInit:
        return new PlantStalkDrawInit("\220\101\225\250\202\314\214\163\225\140\211\346\217\211\212\372\211\273" /* 植物の茎描画初期化 */);
    case SceneObj_PlantLeafDrawInit:
        return new PlantLeafDrawInit("\225\140\211\346\217\211\212\372\211\273[\220\101\225\250\202\314\227\164]" /* 描画初期化[植物の葉] */);
    case SceneObj_TrapezeRopeDrawInit:
        return new TrapezeRopeDrawInit("\213\363\222\206\203\165\203\211\203\223\203\122\203\215\201\133\203\166\225\140\211\346" /* 空中ブランコロープ描画 */);
    case SceneObj_VolumeModelDrawInit:
        return new VolumeModelDrawInit();
    case SceneObj_SpinDriverPathDrawInit:
        return new SpinDriverPathDrawInit();
    case SceneObj_NoteGroup:
        return new NoteGroup();
    case SceneObj_ClipAreaHolder:
        return new ClipAreaHolder("\203\116\203\212\203\142\203\166\203\107\203\212\203\101\203\172\203\213\203\137\201\133" /* クリップエリアホルダー */);
    case SceneObj_ArrowSwitchMultiHolder:
        return new ArrowSwitchMultiHolder();
    case SceneObj_ClipAreaDropHolder:
        return new ClipAreaDropHolder();
    case SceneObj_FallOutFieldDraw:
        return new FallOutFieldDraw("\203\116\203\212\203\142\203\166\203\107\203\212\203\101\225\140\211\346[\224\262\202\253]" /* クリップエリア描画[抜き] */);
    case SceneObj_ClipFieldFillDraw:
        return new ClipFieldFillDraw("\203\116\203\212\203\142\203\166\203\107\203\212\203\101\225\140\211\346[\223\150\202\350\202\302\202\324\202\265]" /* クリップエリア描画[塗りつぶし] */);
    case SceneObj_ScreenAlphaCapture:
        return new ScreenAlphaCapture("\203\101\203\213\203\164\203\100\203\145\203\116\203\130\203\140\203\203\216\346\202\350\215\236\202\335" /* アルファテクスチャ取り込み */);
    case SceneObj_MapPartsRailGuideHolder:
        return new MapPartsRailGuideHolder();
    case SceneObj_GCapture:
        return new GCapture("G\203\114\203\203\203\166\203\140\203\203\201\133" /* Gキャプチャー */);
    case SceneObj_NameObjExecuteHolder:
        return new NameObjExecuteHolder(4096);
    case SceneObj_ElectricRailHolder:
        return new ElectricRailHolder("\223\144\214\202\203\214\201\133\203\213\225\333\216\235" /* 電撃レール保持 */);
    case SceneObj_SpiderThread:
        return new SpiderThread("\203\116\203\202\202\314\221\203" /* クモの巣 */);
    case SceneObj_QuakeEffectGenerator:
        return new QuakeEffectGenerator();
    case SceneObj_HeatHazeDirector:
        return new HeatHazeDirector("\227\172\211\212\220\247\214\344" /* 陽炎制御 */);
    case SceneObj_BlueChipHolder:
        return new ChipHolder("\203\165\203\213\201\133\203\140\203\142\203\166\203\172\203\213\203\137\201\133" /* ブルーチップホルダー */, 0);
    case SceneObj_YellowChipHolder:
        return new ChipHolder("\203\103\203\107\203\215\201\133\201\133\203\140\203\142\203\166\203\172\203\213\203\137\201\133" /* イエローーチップホルダー */, 1);
    case SceneObj_BigBubbleHolder:
        return new BigBubbleHolder("\203\111\203\111\203\101\203\217\203\172\203\213\203\137\201\133" /* オオアワホルダー */);
    case SceneObj_EarthenPipeMediator:
        return new EarthenPipeMediator();
    case SceneObj_WaterAreaHolder:
        return new WaterAreaHolder();
    case SceneObj_WaterPlantDrawInit:
        return new WaterPlantDrawInit();
    case SceneObj_OceanHomeMapCtrl:
        return new OceanHomeMapCtrl();
    case SceneObj_RaceManager:
        return new RaceManager();
    case SceneObj_GroupCheckManager:
        return new GroupCheckManager("\221\256\220\253\203\117\203\213\201\133\203\166\203\175\203\154\201\133\203\127\203\203\201\133" /* 属性グループマネージャー */);
    case SceneObj_SkeletalFishBabyRailHolder:
        return new SkeletalFishBabyRailHolder("\203\130\203\112\203\213\203\126\203\203\201\133\203\116\203\170\203\162\201\133\203\214\201\133\203\213\212\307\227\235" /* スカルシャークベビーレール管理 */);
    case SceneObj_SkeletalFishBossRailHolder:
        return new SkeletalFishBossRailHolder("\203\130\203\112\203\213\203\126\203\203\201\133\203\116\203\173\203\130\203\214\201\133\203\213\212\307\227\235" /* スカルシャークボスレール管理 */);
    case SceneObj_WaterPressureBulletHolder:
        return new WaterPressureBulletHolder("\203\105\203\110\201\133\203\136\201\133\203\166\203\214\203\142\203\126\203\203\201\133\213\312\203\172\203\213\203\137\201\174" /* ウォータープレッシャー玉ホルダ− */);
    case SceneObj_FirePressureBulletHolder:
        return new FirePressureBulletHolder("\203\164\203\100\203\103\203\101\203\166\203\214\203\142\203\126\203\203\201\133\213\312\203\172\203\213\203\137\201\174" /* ファイアプレッシャー玉ホルダ− */);
    case SceneObj_SunshadeMapHolder:
        return new SunshadeMapHolder();
    case SceneObj_MiiFacePartsHolder:
        return new MiiFacePartsHolder(128);
    case SceneObj_MiiFaceIconHolder:
        return new MiiFaceIconHolder(16, "Mii\203\101\203\103\203\122\203\223\225\333\216\235\212\307\227\235" /* Miiアイコン保持管理 */);
    case SceneObj_FluffWindHolder:
        return new FluffWindHolder();
    case SceneObj_SphereSelector:
        return new SphereSelector();
    case SceneObj_GalaxyNamePlateDrawer:
        return new GalaxyNamePlateDrawer();
    case SceneObj_CinemaFrame:
        return new CinemaFrame(true);
    case SceneObj_BossAccessor:
        return new BossAccessor();
    case SceneObj_MiniatureGalaxyHolder:
        return new MiniatureGalaxyHolder();
    case SceneObj_PlanetMapCreator:
        return new PlanetMapCreator("\230\146\220\257\203\116\203\212\203\107\203\103\203\136" /* 惑星クリエイタ */);
    case SceneObj_WarpPodMgr:
        return new WarpPodMgr("\203\217\201\133\203\166\203\174\203\142\203\150\212\307\227\235\213\307" /* ワープポッド管理局 */);
    case SceneObj_PriorDrawAirHolder:
        return new PriorDrawAirHolder();
    case SceneObj_GalaxyMapController:
        return new GalaxyMapController();
    case SceneObj_MoviePlayingSequenceHolder:
        return new MoviePlayingSequenceHolder("\203\200\201\133\203\162\201\133\212\307\227\235\225\333\216\235" /* ムービー管理保持 */);
    case SceneObj_PrologueHolder:
        return new PrologueHolder("\203\166\203\215\203\215\201\133\203\117\225\333\216\235" /* プロローグ保持 */);
    case SceneObj_StaffRoll:
        return new StaffRoll("\203\130\203\136\203\142\203\164\203\215\201\133\203\213" /* スタッフロール */);
    default:
        return nullptr;
    }
}

namespace MR {
    NameObj* createSceneObj(int id) {
        return getSceneObjHolder()->create(id);
    }

    SceneObjHolder* getSceneObjHolder() {
        return SingletonHolder< GameSystem >::get()->mSceneController->getSceneObjHolder();
    }

    bool isExistSceneObj(int id) {
        GameSystemSceneController* pSceneController = SingletonHolder< GameSystem >::get()->mSceneController;

        if (pSceneController == nullptr) {
            return false;
        }

        if (!pSceneController->isExistSceneObjHolder()) {
            return false;
        }

        return MR::getSceneObjHolder()->isExist(id);
    }
};  // namespace MR
