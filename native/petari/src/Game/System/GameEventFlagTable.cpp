#include <nebula_game_api.h>
#include "Game/System/GameEventFlagTable.hpp"
#include "Game/System/GalaxyInfoTable.hpp"
#include "Game/System/NebulaArena.hpp"
#include "Game/Util/HashUtil.hpp"
#include "Game/Util/Table.hpp"
#include "Game/Util/SingletonHolder.hpp"
#include "Game/Util/StringUtil.hpp"

namespace {
    GameEventFlag* sAddedFlags = nullptr;
    // Rows a mod sets, by name, over the game's own.
    GameEventFlag* sSetFlags = nullptr;
    s32 sSetFlagCount = 0;
    s32 sAddedFlagCount = 0;
    GameEventFlag* sMergedFlags = nullptr;

    static const GameEventFlag sGameEventFlagStatic[] = {
        {"AppearFlipPanelExGalaxy", GameEventFlag::Type_GalaxyOpenStar, 1, {0}, {0}, 0, {"FlipPanelExGalaxy"}, {0}},
        {"AppearSurfingLv1Galaxy", GameEventFlag::Type_GalaxyOpenStar, 1, {0}, {0}, 0, {"SurfingLv1Galaxy"}, {0}},
        {"AppearTamakoroExLv1Galaxy", GameEventFlag::Type_GalaxyOpenStar, 1, {0}, {0}, 0, {"TamakoroExLv1Galaxy"}, {0}},
        {"AppearBreakDownPlanetGalaxy", GameEventFlag::Type_GalaxyOpenStar, 1, {0}, {0}, 0, {"BreakDownPlanetGalaxy"}, {0}},
        {"AppearCubeBubbleExLv1Galaxy", GameEventFlag::Type_GalaxyOpenStar, 1, {0}, {0}, 0, {"CubeBubbleExLv1Galaxy"}, {0}},
        {"StarNumOceanFloaterLandGalaxy", GameEventFlag::Type_GalaxyOpenStar, 1, {0}, {0}, 0, {"OceanFloaterLandGalaxy"}, {0}},
        {"SpecialStarOpenOceanFloaterLandGalaxy", GameEventFlag::Type_SpecialStar, 1, {1}, {0}, 0, {"HeavenlyBeachGalaxy"}, {0}},
        {"AppearOceanFloaterLandGalaxy", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"StarNumOceanFloaterLandGalaxy"},
         {"SpecialStarOpenOceanFloaterLandGalaxy"}},
        {"StarNumHoneyBeeExGalaxy", GameEventFlag::Type_GalaxyOpenStar, 1, {0}, {0}, 0, {"HoneyBeeExGalaxy"}, {0}},
        {"SpecialStarOpenHoneyBeeExGalaxy", GameEventFlag::Type_SpecialStar, 1, {1}, {0}, 0, {"HoneyBeeKingdomGalaxy"}, {0}},
        {"AppearHoneyBeeExGalaxy", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"StarNumHoneyBeeExGalaxy"}, {"SpecialStarOpenHoneyBeeExGalaxy"}},
        {"StarNumSkullSharkGalaxy", GameEventFlag::Type_GalaxyOpenStar, 1, {0}, {0}, 0, {"SkullSharkGalaxy"}, {0}},
        {"SpecialStarOpenSkullSharkGalaxy", GameEventFlag::Type_SpecialStar, 1, {1}, {0}, 0, {"TearDropGalaxy"}, {0}},
        {"AppearSkullSharkGalaxy", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"StarNumSkullSharkGalaxy"}, {"SpecialStarOpenSkullSharkGalaxy"}},
        {"StarNumDarkRoomGalaxy", GameEventFlag::Type_GalaxyOpenStar, 1, {0}, {0}, 0, {"DarkRoomGalaxy"}, {0}},
        {"SpecialStarOpenDarkRoomGalaxy", GameEventFlag::Type_SpecialStar, 1, {1}, {0}, 0, {"FactoryGalaxy"}, {0}},
        {"AppearDarkRoomGalaxy", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"StarNumDarkRoomGalaxy"}, {"SpecialStarOpenDarkRoomGalaxy"}},
        {"ハチマリオ初変身", GameEventFlag::Type_None, 0, {0}, {0}, 0, {0}, {0}},
        {"テレサマリオ初変身", GameEventFlag::Type_None, 0, {0}, {0}, 0, {0}, {0}},
        {"ホッパーマリオ初変身", GameEventFlag::Type_None, 0, {0}, {0}, 0, {0}, {0}},
        {"ファイアマリオ初変身", GameEventFlag::Type_None, 0, {0}, {0}, 0, {0}, {0}},
        {"アイスマリオ初変身", GameEventFlag::Type_None, 0, {0}, {0}, 0, {0}, {0}},
        {"フライングマリオ初変身", GameEventFlag::Type_None, 0, {0}, {0}, 0, {0}, {0}},
        {"無敵マリオ初変身", GameEventFlag::Type_None, 0, {0}, {0}, 0, {0}, {0}},
        {"ライフアップキノコ解説", GameEventFlag::Type_None, 0, {0}, {0}, 0, {0}, {0}},
        {"１ＵＰキノコ解説", GameEventFlag::Type_None, 0, {0}, {0}, 0, {0}, {0}},
        {"サーフィンチュートリアル", GameEventFlag::Type_None, 0, {0}, {0}, 0, {0}, {0}},
        {"タマコロチュートリアル", GameEventFlag::Type_None, 0, {0}, {0}, 0, {0}, {0}},
        {"PeachCastleGardenGalaxy", GameEventFlag::Type_Galaxy, 1, {0}, {0}, 0, {0}, {0}},
        {"EpilogueDemoStage", GameEventFlag::Type_Galaxy, 1, {0}, {0}, 0, {0}, {0}},
        {"HeavensDoorGalaxy", GameEventFlag::Type_Galaxy, 1, {0}, {0}, 0, {0}, {0}},
        {"EggStarGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"HoneyBeeKingdomGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"FlipPanelExGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"SurfingLv1Galaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"BeltConveyerExGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"TriLegLv1Galaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"StarDustGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"TamakoroExLv1Galaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"BattleShipGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"BreakDownPlanetGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"CocoonExGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"KoopaBattleVs1Galaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"HeavenlyBeachGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"CubeBubbleExLv1Galaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"PhantomGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"OceanFloaterLandGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"TearDropGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"TeresaMario2DGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"KoopaJrShipLv1Galaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"CosmosGardenGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"IceVolcanoGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"HoneyBeeExGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"SandClockGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"SnowCapsuleGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"KoopaBattleVs2Galaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"ReverseKingdomGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"OceanRingGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"SkullSharkGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"FactoryGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"TransformationExGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"FloaterOtaKingGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"OceanPhantomCaveGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"CannonFleetGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"DarkRoomGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"HellProminenceGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"FishTunnelGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"KoopaBattleVs3Galaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"TamakoroExLv2Galaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"SurfingLv2Galaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"CubeBubbleExLv2Galaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"PeachCastleFinalGalaxy", GameEventFlag::Type_Galaxy, 0, {0}, {0}, 0, {0}, {0}},
        {"NormalCometEggStarGalaxy", GameEventFlag::Type_Comet, 1, {1}, {0}, 0, {"EggStarGalaxy"}, {"EventCometStarter"}},
        {"NormalCometHoneyBeeKingdomGalaxy", GameEventFlag::Type_Comet, 1, {1}, {0}, 0, {"HoneyBeeKingdomGalaxy"}, {"EventCometStarter"}},
        {"NormalCometStarDustGalaxy", GameEventFlag::Type_Comet, 1, {2}, {0}, 0, {"StarDustGalaxy"}, {"EventCometStarter"}},
        {"NormalCometBattleShipGalaxy", GameEventFlag::Type_Comet, 1, {3}, {0}, 0, {"BattleShipGalaxy"}, {"EventCometStarter"}},
        {"NormalCometHeavenlyBeachGalaxy", GameEventFlag::Type_Comet, 1, {3}, {0}, 0, {"HeavenlyBeachGalaxy"}, {"EventCometStarter"}},
        {"NormalCometPhantomGalaxy", GameEventFlag::Type_Comet, 1, {3}, {0}, 0, {"PhantomGalaxy"}, {"EventCometStarter"}},
        {"NormalCometCosmosGardenGalaxy", GameEventFlag::Type_Comet, 1, {3}, {0}, 0, {"CosmosGardenGalaxy"}, {"EventCometStarter"}},
        {"NormalCometIceVolcanoGalaxy", GameEventFlag::Type_Comet, 1, {1}, {0}, 0, {"IceVolcanoGalaxy"}, {"EventCometStarter"}},
        {"NormalCometSandClockGalaxy", GameEventFlag::Type_Comet, 1, {3}, {0}, 0, {"SandClockGalaxy"}, {"EventCometStarter"}},
        {"NormalCometReverseKingdomGalaxy", GameEventFlag::Type_Comet, 1, {3}, {0}, 0, {"ReverseKingdomGalaxy"}, {"EventCometStarter"}},
        {"NormalCometOceanRingGalaxy", GameEventFlag::Type_Comet, 1, {1}, {0}, 0, {"OceanRingGalaxy"}, {"EventCometStarter"}},
        {"NormalCometFactoryGalaxy", GameEventFlag::Type_Comet, 1, {6}, {0}, 0, {"FactoryGalaxy"}, {"EventCometStarter"}},
        {"NormalCometOceanPhantomCaveGalaxy", GameEventFlag::Type_Comet, 1, {2}, {0}, 0, {"OceanPhantomCaveGalaxy"}, {"EventCometStarter"}},
        {"NormalCometCannonFleetGalaxy", GameEventFlag::Type_Comet, 1, {3}, {0}, 0, {"CannonFleetGalaxy"}, {"EventCometStarter"}},
        {"NormalCometHellProminenceGalaxy", GameEventFlag::Type_Comet, 1, {2}, {0}, 0, {"HellProminenceGalaxy"}, {"EventCometStarter"}},
        {"SpecialStarCoin100CometRelease", GameEventFlag::Type_SpecialStar, 1, {5}, {0}, 0, {"EggStarGalaxy"}, {0}},
        {"EventCoin100CometStarter", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"EventCometStarter"}, {"SpecialStarCoin100CometRelease"}},
        {"Coin100CometEggStarGalaxy", GameEventFlag::Type_Comet, 1, {1}, {0}, 0, {"EggStarGalaxy"}, {"ViewNormalEnding"}},
        {"Coin100CometHoneyBeeKingdomGalaxy", GameEventFlag::Type_Comet, 1, {1}, {0}, 0, {"HoneyBeeKingdomGalaxy"}, {"EventCoin100CometStarter"}},
        {"Coin100CometStarDustGalaxy", GameEventFlag::Type_Comet, 1, {1}, {0}, 0, {"StarDustGalaxy"}, {"EventCoin100CometStarter"}},
        {"Coin100CometBattleShipGalaxy", GameEventFlag::Type_Comet, 1, {1}, {0}, 0, {"BattleShipGalaxy"}, {"EventCoin100CometStarter"}},
        {"Coin100CometHeavenlyBeachGalaxy", GameEventFlag::Type_Comet, 1, {1}, {0}, 0, {"HeavenlyBeachGalaxy"}, {"EventCoin100CometStarter"}},
        {"Coin100CometPhantomGalaxy", GameEventFlag::Type_Comet, 1, {2}, {0}, 0, {"PhantomGalaxy"}, {"EventCoin100CometStarter"}},
        {"Coin100CometCosmosGardenGalaxy", GameEventFlag::Type_Comet, 1, {1}, {0}, 0, {"CosmosGardenGalaxy"}, {"EventCoin100CometStarter"}},
        {"Coin100CometIceVolcanoGalaxy", GameEventFlag::Type_Comet, 1, {1}, {0}, 0, {"IceVolcanoGalaxy"}, {"EventCoin100CometStarter"}},
        {"Coin100CometSandClockGalaxy", GameEventFlag::Type_Comet, 1, {2}, {0}, 0, {"SandClockGalaxy"}, {"EventCoin100CometStarter"}},
        {"Coin100CometReverseKingdomGalaxy", GameEventFlag::Type_Comet, 1, {3}, {0}, 0, {"ReverseKingdomGalaxy"}, {"EventCoin100CometStarter"}},
        {"Coin100CometOceanRingGalaxy", GameEventFlag::Type_Comet, 1, {3}, {0}, 0, {"OceanRingGalaxy"}, {"EventCoin100CometStarter"}},
        {"Coin100CometFactoryGalaxy", GameEventFlag::Type_Comet, 1, {2}, {0}, 0, {"FactoryGalaxy"}, {"EventCoin100CometStarter"}},
        {"Coin100CometOceanPhantomCaveGalaxy", GameEventFlag::Type_Comet, 1, {1}, {0}, 0, {"OceanPhantomCaveGalaxy"}, {"EventCoin100CometStarter"}},
        {"Coin100CometCannonFleetGalaxy", GameEventFlag::Type_Comet, 1, {2}, {0}, 0, {"CannonFleetGalaxy"}, {"EventCoin100CometStarter"}},
        {"Coin100CometHellProminenceGalaxy", GameEventFlag::Type_Comet, 1, {3}, {0}, 0, {"HellProminenceGalaxy"}, {"EventCoin100CometStarter"}},
        {"SpecialStarGrand1", GameEventFlag::Type_SpecialStar, 1, {1}, {0}, 0, {"HeavensDoorGalaxy"}, {0}},
        {"SpecialStarGrand2", GameEventFlag::Type_SpecialStar, 1, {1}, {0}, 0, {"TriLegLv1Galaxy"}, {0}},
        {"SpecialStarGrand3", GameEventFlag::Type_SpecialStar, 1, {1}, {0}, 0, {"KoopaBattleVs1Galaxy"}, {0}},
        {"SpecialStarGrand4", GameEventFlag::Type_SpecialStar, 1, {1}, {0}, 0, {"KoopaJrShipLv1Galaxy"}, {0}},
        {"SpecialStarGrand5", GameEventFlag::Type_SpecialStar, 1, {1}, {0}, 0, {"KoopaBattleVs2Galaxy"}, {0}},
        {"SpecialStarGrand6", GameEventFlag::Type_SpecialStar, 1, {1}, {0}, 0, {"FloaterOtaKingGalaxy"}, {0}},
        {"SpecialStarGrand7", GameEventFlag::Type_SpecialStar, 1, {1}, {0}, 0, {"KoopaBattleVs3Galaxy"}, {0}},
        {"SpecialStarGreen1", GameEventFlag::Type_SpecialStar, 1, {7}, {0}, 0, {"BattleShipGalaxy"}, {0}},
        {"SpecialStarGreen2", GameEventFlag::Type_SpecialStar, 1, {2}, {0}, 0, {"OceanFloaterLandGalaxy"}, {0}},
        {"SpecialStarGreen3", GameEventFlag::Type_SpecialStar, 1, {7}, {0}, 0, {"SandClockGalaxy"}, {0}},
        {"SpecialStarGreen1/2", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"SpecialStarGreen1"}, {"SpecialStarGreen2"}},
        {"SpecialStarGreenAll", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"SpecialStarGreen1/2"}, {"SpecialStarGreen3"}},
        {"SpecialStarRed1", GameEventFlag::Type_SpecialStar, 1, {2}, {0}, 0, {"HeavensDoorGalaxy"}, {0}},
        {"AppearHeavensDoorGalaxy", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"SpecialStarRed1"}, {0}},
        {"EventCometStarter", GameEventFlag::Type_StarNum, 1, {13}, {0}, 0, {0}, {0}},
        {"EventKinopioExplorerRescued", GameEventFlag::Type_SpecialStar, 1, {1}, {0}, 0, {"HoneyBeeKingdomGalaxy"}, {0}},
        {"EventKinopioExplorerOrganize", GameEventFlag::Type_StarNum, 1, {7}, {0}, 0, {0}, {0}},
        {"ExOpen1CocoonExGalaxy", GameEventFlag::Type_SpecialStar, 1, {3}, {0}, 0, {"StarDustGalaxy"}, {0}},
        {"ExOpen1TearDropGalaxy", GameEventFlag::Type_SpecialStar, 1, {1}, {0}, 0, {"HeavenlyBeachGalaxy"}, {0}},
        {"ExOpen1FishTunnelGalaxy", GameEventFlag::Type_SpecialStar, 1, {1}, {0}, 0, {"SandClockGalaxy"}, {0}},
        {"ExOpenCommonBase", GameEventFlag::Type_SpecialStar, 1, {1}, {0}, 0, {"PhantomGalaxy"}, {0}},
        {"ExOpenSubTransformationExGalaxy", GameEventFlag::Type_SpecialStar, 1, {2}, {0}, 0, {"OceanRingGalaxy"}, {0}},
        {"ExOpenSubTeresaMario2DGalaxy", GameEventFlag::Type_SpecialStar, 1, {2}, {0}, 0, {"HeavensDoorGalaxy"}, {0}},
        {"ExOpen1TransformationExGalaxy", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"ExOpenCommonBase"}, {"ExOpenSubTransformationExGalaxy"}},
        {"ExOpen1TeresaMario2DGalaxy", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"ExOpenCommonBase"}, {"ExOpenSubTeresaMario2DGalaxy"}},
        {"ExOpen1SnowCapsuleGalaxy", GameEventFlag::Type_SpecialStar, 1, {1}, {0}, 0, {"HellProminenceGalaxy"}, {0}},
        {"ExOpen1BeltConveyerExGalaxy", GameEventFlag::Type_StarNum, 1, {7}, {0}, 0, {0}, {0}},
        {"AppearBeltConveyerExGalaxy", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"SpecialStarGrand1"}, {"ExOpen1BeltConveyerExGalaxy"}},
        {"AppearCocoonExGalaxy", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"SpecialStarGrand2"}, {"ExOpen1CocoonExGalaxy"}},
        {"AppearTearDropGalaxy", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"SpecialStarGrand3"}, {"ExOpen1TearDropGalaxy"}},
        {"AppearFishTunnelGalaxy", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"SpecialStarGrand4"}, {"ExOpen1FishTunnelGalaxy"}},
        {"AppearTransformationExGalaxy", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"SpecialStarGrand5"}, {"ExOpen1TransformationExGalaxy"}},
        {"AppearTeresaMario2DGalaxy", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"SpecialStarGrand6"}, {"ExOpen1TeresaMario2DGalaxy"}},
        {"AppearSnowCapsuleGalaxy", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"SpecialStarGrand6"}, {"ExOpen1SnowCapsuleGalaxy"}},
        {"StarPieceBeltConveyerExGalaxy", GameEventFlag::Type_StarPiece, 1, {0}, {40}, 0, {"BeltConveyerExGalaxy"}, {0}},
        {"StarPieceCocoonExGalaxy", GameEventFlag::Type_StarPiece, 1, {1}, {40}, 0, {"CocoonExGalaxy"}, {0}},
        {"StarPieceTearDropGalaxy", GameEventFlag::Type_StarPiece, 1, {2}, {60}, 0, {"TearDropGalaxy"}, {0}},
        {"StarPieceFishTunnelGalaxy", GameEventFlag::Type_StarPiece, 1, {6}, {80}, 0, {"FishTunnelGalaxy"}, {0}},
        {"StarPieceTransformationExGalaxy", GameEventFlag::Type_StarPiece, 1, {5}, {100}, 0, {"TransformationExGalaxy"}, {0}},
        {"StarPieceTeresaMario2DGalaxy", GameEventFlag::Type_StarPiece, 1, {3}, {120}, 0, {"TeresaMario2DGalaxy"}, {0}},
        {"StarPieceSnowCapsuleGalaxy", GameEventFlag::Type_StarPiece, 1, {4}, {160}, 0, {"SnowCapsuleGalaxy"}, {0}},
        {"AppearTamakoroExLv2Galaxy", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"SpecialStarGreenAll"}, {0}},
        {"AppearSurfingLv2Galaxy", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"SpecialStarGreenAll"}, {0}},
        {"AppearCubeBubbleExLv2Galaxy", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"SpecialStarGreenAll"}, {0}},
        {"AppearPeachCastleFinalGalaxy", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"ViewCompleteMarioAndLuigi"}, {0}},
        {"LibraryOpenRequirement", GameEventFlag::Type_EventValueIsZero, 1, {0}, {0}, 0, {"LibraryOpenNewStarCount"}, {"SpecialStarGrand3"}},
        {"StarNumPictureBookB", GameEventFlag::Type_StarNum, 1, {24}, {0}, 0, {0}, {0}},
        {"StarNumPictureBookC", GameEventFlag::Type_StarNum, 1, {28}, {0}, 0, {0}, {0}},
        {"StarNumPictureBookD", GameEventFlag::Type_StarNum, 1, {32}, {0}, 0, {0}, {0}},
        {"StarNumPictureBookE", GameEventFlag::Type_StarNum, 1, {40}, {0}, 0, {0}, {0}},
        {"StarNumPictureBookF", GameEventFlag::Type_StarNum, 1, {49}, {0}, 0, {0}, {0}},
        {"StarNumPictureBookG", GameEventFlag::Type_StarNum, 1, {58}, {0}, 0, {0}, {0}},
        {"PictureBookA", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"LibraryOpenRequirement"}, {0}},
        {"PictureBookB", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"StarNumPictureBookB"}, {"LibraryOpenRequirement"}},
        {"PictureBookC", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"StarNumPictureBookC"}, {"LibraryOpenRequirement"}},
        {"PictureBookD", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"StarNumPictureBookD"}, {"LibraryOpenRequirement"}},
        {"PictureBookE", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"StarNumPictureBookE"}, {"LibraryOpenRequirement"}},
        {"PictureBookF", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"StarNumPictureBookF"}, {"LibraryOpenRequirement"}},
        {"PictureBookG", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"StarNumPictureBookG"}, {"LibraryOpenRequirement"}},
        {"PictureBookH", GameEventFlag::Type_SpecialStar, 1, {1}, {0}, 0, {"KoopaBattleVs3Galaxy"}, {0}},
        {"PictureBookI", GameEventFlag::Type_SpecialStar, 1, {1}, {0}, 0, {"KoopaBattleVs3Galaxy"}, {0}},
        {"SpecialStarLuigiRescued", GameEventFlag::Type_SpecialStar, 1, {1}, {0}, 0, {"PhantomGalaxy"}, {0}},
        {"LuigiTalkAfterRescued", GameEventFlag::Type_EventFlag, 0, {0}, {0}, 0, {"SpecialStarLuigiRescued"}, {0}},
        {"SpecialStarFindingLuigi1", GameEventFlag::Type_SpecialStar, 1, {6}, {0}, 0, {"EggStarGalaxy"}, {0}},
        {"SpecialStarFindingLuigi2", GameEventFlag::Type_SpecialStar, 1, {7}, {0}, 0, {"BattleShipGalaxy"}, {0}},
        {"SpecialStarFindingLuigi3", GameEventFlag::Type_SpecialStar, 1, {6}, {0}, 0, {"HoneyBeeKingdomGalaxy"}, {0}},
        {"RosettaTalkAboutTico", GameEventFlag::Type_EventFlag, 0, {0}, {0}, 0, {"SpecialStarGrand6"}, {0}},
        {"RosettaTalkAfterNormalEnding", GameEventFlag::Type_EventFlag, 0, {0}, {0}, 0, {"ViewNormalEnding"}, {0}},
        {"ViewNormalEnding", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"PictureBookH"}, {0}},
        {"PowerStarComplete", GameEventFlag::Type_StarNum, 1, {120}, {0}, 0, {0}, {0}},
        {"ViewCompleteEnding", GameEventFlag::Type_EventFlag, 0, {0}, {0}, 0, {"PowerStarComplete"}, {0}},
        {"ViewCompleteMarioAndLuigi", GameEventFlag::Type_CompleteMarioAndLuigi, 1, {0}, {0}, 0, {0}, {0}},
        {"SpecialStarFinalChallenge", GameEventFlag::Type_SpecialStar, 1, {1}, {0}, 0, {"PeachCastleFinalGalaxy"}, {0}},
        {"StarPieceCounterStop", GameEventFlag::Type_None, 0, {0}, {0}, 0, {0}, {0}},
        {"PlayHeavensDoorGalaxy1", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {0}, {0}},
        {"PlayHeavensDoorGalaxy2", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"SpecialStarGrand6"}, {0}},
        {"[同期]ピーチ城浮上後", GameEventFlag::Type_StoryEventSync, 1, {0}, {0}, 0, {"PlayHeavensDoorGalaxy1"}, {"ピーチ城浮上後"} /* ピーチ城浮上後 */},
        {"PlayEggStarGalaxy1", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"SpecialStarGrand1"}, {0}},
        {"[同期]バトラー情報Ａ", GameEventFlag::Type_StoryEventSync, 1, {0}, {0}, 0, {"PlayEggStarGalaxy1"}, {"バトラー情報Ａ"} /* バトラー情報Ａ */},
        {"SpecialStarEggStarGalaxy1", GameEventFlag::Type_SpecialStar, 1, {1}, {0}, 0, {"EggStarGalaxy"}, {0}},
        {"PlayEggStarGalaxy2", GameEventFlag::Type_EventFlag, 1, {0}, {0}, 0, {"SpecialStarEggStarGalaxy1"}, {0}},
        {"[同期]スターピースレクチャー", GameEventFlag::Type_StoryEventSync, 1, {0}, {0}, 0, {"PlayEggStarGalaxy2"}, {"スターピースレクチャー"} /* スターピースレクチャー */},
        {"PowerStarNumForButlerKoopaDemo", GameEventFlag::Type_GalaxyOpenStar, 1, {0}, {0}, 0, {"KoopaBattleVs1Galaxy"}, {0}},
        {"[同期]クッパスタープラント発見", GameEventFlag::Type_StoryEventSync, 1, {0}, {0}, 0, {"PowerStarNumForButlerKoopaDemo"},
         {"クッパスタープラント発見"} /* クッパスタープラント発見 */},
    };
};  // namespace

bool GameEventFlagIter::isEnd() const {
    if (!GameEventFlagIter::isValid()) {
        return true;
    }

    return GameEventFlagTable::getFlag(mIter) == nullptr;
}

void GameEventFlagIter::goNext() {
    if (GameEventFlagIter::isValid()) {
        mIter++;
    }
}

const GameEventFlag* GameEventFlagIter::getFlag() const {
    return GameEventFlagTable::getFlag(mIter);
}

bool GameEventFlagIter::isValid() const {
    if (mIter >= 0) {
        return true;
    } else {
        return false;
    }
}

GameEventFlagTableInstance::GameEventFlagTableInstance() : mNextSpecial(nullptr) {
    const s32 size = GameEventFlagTable::getTableSize();
    mNames.reset(static_cast< u32 >(size));
    mSaveHashes.reset(static_cast< u32 >(size));
    mSpecialGalaxies.reset(static_cast< u32 >(size));
    mNextSpecial = new u32[size > 0 ? size : 1];
    // pLastSpecial[first]: the latest special star flag so far of the galaxy whose first one is first.
    u32* pLastSpecial = new u32[size > 0 ? size : 1];

    for (s32 i = 0; i < size; i++) {
        const GameEventFlag* pFlag = GameEventFlagTable::getFlag(i);
        mNames.add(pFlag->mName, static_cast< u32 >(i));
        if ((pFlag->mSaveFlag & 0x1) == 0) {
            mSaveHashes.add(pFlag->mName, GameEventFlagTable::getSaveHash(pFlag->mName), static_cast< u32 >(i));
        }

        mNextSpecial[i] = MR::NameIndex::cNoValue;
        const GameEventFlagAccessor accessor(pFlag);
        if (accessor.isTypeSpecialStar() && accessor.getGalaxyName() != nullptr) {
            if (mSpecialGalaxies.add(accessor.getGalaxyName(), static_cast< u32 >(i))) {
                pLastSpecial[i] = static_cast< u32 >(i);
            } else {
                const u32 first = mSpecialGalaxies.find(accessor.getGalaxyName());
                mNextSpecial[pLastSpecial[first]] = static_cast< u32 >(i);
                pLastSpecial[first] = static_cast< u32 >(i);
            }
        }
    }
    delete[] pLastSpecial;
}

const GameEventFlag* GameEventFlagTableInstance::findSpecialStar(const char* pGalaxyName, s32 starId, const char* pNamePart) const {
    for (u32 i = mSpecialGalaxies.find(pGalaxyName); i != MR::NameIndex::cNoValue; i = mNextSpecial[i]) {
        const GameEventFlag* pFlag = GameEventFlagTable::getFlag(static_cast< int >(i));
        const GameEventFlagAccessor accessor(pFlag);
        if (accessor.getStarId() == starId && (pNamePart == nullptr || MR::isEqualSubString(accessor.getName(), pNamePart))) {
            return pFlag;
        }
    }
    return nullptr;
}

const GameEventFlag* GameEventFlagTableInstance::findFlag(const char* flagName) const {
    const u32 index = mNames.find(flagName);
    return index == MR::NameIndex::cNoValue ? nullptr : GameEventFlagTable::getFlag(static_cast< int >(index));
}

s32 GameEventFlagTableInstance::findSaveIndex(u16 saveHash) const {
    const u32 index = mSaveHashes.findHash(saveHash);
    return index == MR::NameIndex::cNoValue ? -1 : static_cast< s32 >(index);
}

namespace GameEventFlagTable {
    GameEventFlagIter getBeginIter() {
        return GameEventFlagIter();
    }

    GameEventFlagAccessor makeAccessor(const char* pFlagName) {
        return findFlag(pFlagName);
    }

    const GameEventFlag* flags() {
        if (::sAddedFlagCount == 0 && ::sSetFlagCount == 0) {
            return ::sGameEventFlagStatic;
        }
        if (::sMergedFlags == nullptr) {
            const s32 size = ARRAY_SIZE(::sGameEventFlagStatic);
            // Built on first use, which can be inside a scene: kept in the arena of the process.
            ::sMergedFlags = new (NebulaArena::root(), 0) GameEventFlag[size + ::sAddedFlagCount];
            for (s32 i = 0; i < size; i++) {
                ::sMergedFlags[i] = ::sGameEventFlagStatic[i];
                for (s32 f = ::sSetFlagCount - 1; f >= 0; f--) {
                    if (MR::isEqualString(::sSetFlags[f].mName, ::sMergedFlags[i].mName)) {
                        ::sMergedFlags[i] = ::sSetFlags[f];
                        break;
                    }
                }
                // A hungry Luma a mod puts in this one's place.
                if (::sMergedFlags[i].mType == GameEventFlag::Type_StarPiece) {
                    for (s32 a = 0; a < ::sAddedFlagCount; a++) {
                        if (::sAddedFlags[a].mType == GameEventFlag::Type_StarPiece &&
                            ::sAddedFlags[a].mStarPieceIndex == ::sMergedFlags[i].mStarPieceIndex) {
                            ::sMergedFlags[i].mType = GameEventFlag::Type_None;
                        }
                    }
                }
            }
            for (s32 a = 0; a < ::sAddedFlagCount; a++) {
                ::sMergedFlags[size + a] = ::sAddedFlags[a];
            }
        }
        return ::sMergedFlags;
    }

    s32 getTableSize() {
        return ARRAY_SIZE(::sGameEventFlagStatic) + ::sAddedFlagCount;
    }

    static bool nameIsFree(const char* pName) {
        const u32 hash = MR::getHashCode(pName);
        for (u32 i = 0; i < ARRAY_SIZE(::sGameEventFlagStatic); i++) {
            if (MR::getHashCode(::sGameEventFlagStatic[i].mName) == hash) {
                return false;
            }
        }
        for (s32 i = 0; i < ::sAddedFlagCount; i++) {
            if (MR::getHashCode(::sAddedFlags[i].mName) == hash) {
                return false;
            }
        }
        return true;
    }

    static void addFlag(const GameEventFlag& rFlag) {
        GameEventFlag* pFlags = new (NebulaArena::root(), 0) GameEventFlag[::sAddedFlagCount + 1];
        for (s32 i = 0; i < ::sAddedFlagCount; i++) {
            pFlags[i] = ::sAddedFlags[i];
        }
        pFlags[::sAddedFlagCount] = rFlag;
        delete[] ::sAddedFlags;
        ::sAddedFlags = pFlags;
        ::sAddedFlagCount++;
        // The merged rows are built again on the next use (they would be one row short of the table size).
        ::sMergedFlags = nullptr;
    }

    bool addGalaxy(const char* pGalaxyName) {
        if (!nameIsFree(pGalaxyName)) {
            return false;
        }
        GameEventFlag flag = {};
        flag.mName = pGalaxyName;
        flag.mType = GameEventFlag::Type_Galaxy;
        addFlag(flag);
        return true;
    }

    void setFlag(const char* pName, u8 kind, u16 a, u16 b, const char* pFirst, const char* pSecond) {
        GameEventFlag flag = {};
        flag.mName = pName;
        flag.mType = kind;
        flag.mSaveFlag = 1;
        flag.mCondition1 = a;
        flag.mCondition2 = b;
        flag.mCondition3 = pFirst;
        flag.mCondition4 = pSecond;
        // A name the game doesn't have is a new row.
        if (nameIsFree(pName)) {
            addFlag(flag);
            return;
        }
        GameEventFlag* pFlags = new (NebulaArena::root(), 0) GameEventFlag[::sSetFlagCount + 1];
        for (s32 i = 0; i < ::sSetFlagCount; i++) {
            pFlags[i] = ::sSetFlags[i];
        }
        pFlags[::sSetFlagCount] = flag;
        delete[] ::sSetFlags;
        ::sSetFlags = pFlags;
        ::sSetFlagCount++;
        ::sMergedFlags = nullptr;
    }

    bool addHungryLuma(const char* pGalaxyName, s32 slot, s32 starBits) {
        const size_t length = strlen(pGalaxyName) + sizeof("StarPiece");
        char* pName = new (NebulaArena::root(), 0) char[length];
        nebula_snprintf(pName, length, "StarPiece%s", pGalaxyName);
        if (!nameIsFree(pName)) {
            return false;
        }
        GameEventFlag flag = {};
        flag.mName = pName;
        flag.mType = GameEventFlag::Type_StarPiece;
        flag.mSaveFlag = 1;
        flag.mStarPieceIndex = static_cast< u8 >(slot);
        flag.mExactStarPieceNum = static_cast< u32 >(starBits);
        flag.mGalaxyName = pGalaxyName;
        addFlag(flag);
        return true;
    }

    const GameEventFlag* getFlag(int index) {
        if (index < 0 || index >= getTableSize()) {
            return nullptr;
        } else {
            return &flags()[index];
        }
    }

    const GameEventFlag* findFlag(const char* pFlagName) {
        return SingletonHolder< GameEventFlagTableInstance >::get()->findFlag(pFlagName);
    }

    const GameEventFlag* findSpecialPowerStar(const char* pGalaxyName, s32 starId, const char* pNamePart) {
        const GameEventFlagTableInstance* pInstance = SingletonHolder< GameEventFlagTableInstance >::get();
        if (pInstance != nullptr) {
            return pInstance->findSpecialStar(pGalaxyName, starId, pNamePart);
        }

        // Before the game data exists: the table itself.
        for (GameEventFlagIter iter = getBeginIter(); !iter.isEnd(); iter.goNext()) {
            GameEventFlagAccessor accessor = GameEventFlagAccessor(iter.getFlag());

            if (!accessor.isTypeSpecialStar() || accessor.getGalaxyName() == nullptr) {
                continue;
            }

            if (accessor.getStarId() != starId) {
                continue;
            }

            if (!MR::isEqualString(pGalaxyName, accessor.getGalaxyName())) {
                continue;
            }

            if (pNamePart != nullptr && !MR::isEqualSubString(accessor.getName(), pNamePart)) {
                continue;
            }

            return iter.getFlag();
        }

        return nullptr;
    }

    const char* getEventFlagNameSpecialPowerStar(const char* pGalaxyName, s32 starId) {
        const GameEventFlag* pFlag = findSpecialPowerStar(pGalaxyName, starId, nullptr);
        return pFlag != nullptr ? GameEventFlagAccessor(pFlag).getName() : nullptr;
    }

    bool isPowerStarType(const char* pGalaxyName, s32 starId, const char* pStarType) {
        const char* pFlagName = getEventFlagNameSpecialPowerStar(pGalaxyName, starId);

        if (pFlagName == nullptr) {
            return false;
        }

        return MR::isEqualString(pFlagName, pStarType);
    }

    s32 calcExclamationGalaxyNum() {
        s32 num = -1;

        for (GameEventFlagIter iter = getBeginIter(); !iter.isEnd(); iter.goNext()) {
            GameEventFlagAccessor accessor = GameEventFlagAccessor(iter.getFlag());

            if (!accessor.isTypeStarPiece()) {
                continue;
            }

            if (num >= accessor.getStarPieceIndex()) {
                continue;
            }

            num = accessor.getStarPieceIndex();
        }

        return num + 1;
    }

    const char* getExclamationGalaxyNameFromIndex(int index) {
        for (GameEventFlagIter iter = getBeginIter(); !iter.isEnd(); iter.goNext()) {
            GameEventFlagAccessor accessor = GameEventFlagAccessor(iter.getFlag());

            if (!accessor.isTypeStarPiece()) {
                continue;
            }

            if (accessor.getStarPieceIndex() != index) {
                continue;
            }

            return accessor.getGalaxyNameWithStarPiece();
        }

        return nullptr;
    }

    bool isExist(const char* pName) {
        return findFlag(pName) != nullptr;
    }

    int getIndex(const GameEventFlag* pFlag) {
        return pFlag - &flags()[0];
    }

    bool isDependedAnother(const char* pFlagName1, const char* pFlagName2) {
        const GameEventFlag* pFlag1 = findFlag(pFlagName1);

        // A flag the table lacks depends on nothing.
        if (pFlag1 == nullptr) {
            return false;
        }

        if (pFlag1->mType == GameEventFlag::Type_EventFlag) {
            if (pFlag1->mRequirement1 != nullptr && MR::isEqualString(pFlag1->mRequirement1, pFlagName2)) {
                return true;
            }

            if (pFlag1->mRequirement2 != nullptr && MR::isEqualString(pFlag1->mRequirement2, pFlagName2)) {
                return true;
            }
        } else if (pFlag1->mType == GameEventFlag::Type_Galaxy) {
            MR::InlineVector< const char*, 3 > galaxyDependedFlags;
            getGalaxyDependedFlags(&galaxyDependedFlags, pFlagName1);

            for (const char* const* pFlag = galaxyDependedFlags.begin(); pFlag != galaxyDependedFlags.end(); pFlag++) {
                if (MR::isEqualString(*pFlag, pFlagName2)) {
                    return true;
                }
            }
        }

        return false;
    }

    u16 getSaveHash(const char* pName) {
        // Flags with Japanese names: the names are UTF-8, the save hashes are of the Shift-JIS bytes.
        struct SavedName {
            const char* mName;
            u16 mHash;
        };
        static constexpr SavedName cJapanese[] = {
            {"ハチマリオ初変身", 0x7ABA},
            {"テレサマリオ初変身", 0x0C8E},
            {"ホッパーマリオ初変身", 0x184F},
            {"ファイアマリオ初変身", 0x0E5E},
            {"アイスマリオ初変身", 0x62E5},
            {"フライングマリオ初変身", 0x5965},
            {"無敵マリオ初変身", 0x4C23},
            {"ライフアップキノコ解説", 0x1880},
            {"１ＵＰキノコ解説", 0x3D66},
            {"サーフィンチュートリアル", 0x2CD5},
            {"タマコロチュートリアル", 0x4E78},
            {"[同期]ピーチ城浮上後", 0x628F},
            {"[同期]バトラー情報Ａ", 0x6DFF},
            {"[同期]スターピースレクチャー", 0x118A},
            {"[同期]クッパスタープラント発見", 0x6EBA},
        };
        for (const char* p = pName; *p != '\0'; p++) {
            if (static_cast< u8 >(*p) >= 0x80) {
                for (const SavedName& rName : cJapanese) {
                    if (MR::isEqualString(rName.mName, pName)) {
                        return rName.mHash;
                    }
                }
                break;
            }
        }
        return MR::getHashCode(pName) & 0x7FFF;
    }

    int getIndexFromHashCode(u16 hashCode) {
        return SingletonHolder< GameEventFlagTableInstance >::get()->findSaveIndex(hashCode);
    }

    s32 calcSpecialPowerStarNum(const char* pPowerStarType) {
        s32 num = 0;

        for (GameEventFlagIter iter = getBeginIter(); !iter.isEnd(); iter.goNext()) {
            GameEventFlagAccessor accessor = GameEventFlagAccessor(iter.getFlag());

            if (!accessor.isTypeSpecialStar()) {
                continue;
            }

            if (!MR::isEqualSubString(accessor.getName(), pPowerStarType)) {
                continue;
            }

            num++;
        }

        return num;
    }

    s32 getStarPieceNumToOpenExclamationGalaxy(const char* pGalaxyName) {
        MR::FormattedName flagName("StarPiece%s", pGalaxyName);

        GameEventFlagAccessor accessor = makeAccessor(flagName.c_str());

        return accessor.getNeedStarPieceNum();
    }

    s32 calcGreenPowerStarNum() {
        return calcSpecialPowerStarNum("SpecialStarGreen");
    }

    void getGalaxyDependedFlags(MR::InlineVector< const char*, 3 >* pFlags, const char* pName) {
        Table info;
        info.attach(NebulaDataTables::getGalaxyInfoTable());

        TableIter iter = info.findElement("name", pName, 0);

        // OpenCondition0, 1, ... up to the first column the table lacks.
        for (s32 idx = 0;; idx++) {
            const MR::FormattedName key("OpenCondition%d", idx);
            if (info.searchItemInfo(key.c_str()) < 0) {
                break;
            }

            const char* openCondition = "";
            iter.getValue(key.c_str(), &openCondition);
            if (!MR::isEqualString(openCondition, "")) {
                pFlags->push_back(openCondition);
            }
        }
    }
};  // namespace GameEventFlagTable
