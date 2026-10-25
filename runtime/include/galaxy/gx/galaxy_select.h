// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The ADVANCED page's galaxy select: its stage list, the page's requests (taken by the game each scene frame,
// svc_test_scene_frame) and the names the game gives it in its own words.

#include <cstddef>
#include <cstdint>
#include <string>

namespace galaxy::gx {

struct GalaxyStage {
    const char* id;       // the stage's name in the game's data
    std::uint32_t stars;  // scenarios that can be entered (1..stars)
};

// By dome (Terrace, Fountain, Kitchen, Bedroom, Engine Room, Garden), then the rest. `stars` leaves out the hidden
// scenarios (ScenarioData IsHidden, numbered after the others): the game sizes its per-scenario data without them
// (ScenarioData::getScenarioNum), so entering one directly indexes past those arrays.
inline constexpr GalaxyStage kGalaxyStages[] = {
    // The Comet Observatory; the game's name lookup does not know it, so it is shown as kObservatoryName.
    {"AstroGalaxy", 1u},
    {"EggStarGalaxy", 5u}, {"HoneyBeeKingdomGalaxy", 5u}, {"SurfingLv1Galaxy", 1u},
    {"FlipPanelExGalaxy", 1u}, {"TriLegLv1Galaxy", 1u},
    {"StarDustGalaxy", 5u}, {"BattleShipGalaxy", 5u}, {"TamakoroExLv1Galaxy", 1u},
    {"BreakDownPlanetGalaxy", 1u}, {"KoopaBattleVs1Galaxy", 1u},
    {"HeavenlyBeachGalaxy", 5u}, {"PhantomGalaxy", 5u}, {"CubeBubbleExLv1Galaxy", 1u},
    {"OceanFloaterLandGalaxy", 1u}, {"KoopaJrShipLv1Galaxy", 1u},
    {"CosmosGardenGalaxy", 5u}, {"IceVolcanoGalaxy", 5u}, {"SandClockGalaxy", 5u},
    {"HoneyBeeExGalaxy", 1u}, {"KoopaBattleVs2Galaxy", 1u},
    {"ReverseKingdomGalaxy", 5u}, {"OceanRingGalaxy", 5u}, {"FactoryGalaxy", 5u},
    {"SkullSharkGalaxy", 1u}, {"FloaterOtaKingGalaxy", 1u},
    {"OceanPhantomCaveGalaxy", 5u}, {"CannonFleetGalaxy", 5u}, {"HellProminenceGalaxy", 5u},
    {"DarkRoomGalaxy", 1u}, {"KoopaBattleVs3Galaxy", 1u},
    {"HeavensDoorGalaxy", 2u}, {"BeltConveyerExGalaxy", 1u}, {"CocoonExGalaxy", 1u},
    {"TearDropGalaxy", 1u}, {"FishTunnelGalaxy", 1u}, {"TransformationExGalaxy", 1u},
    {"SnowCapsuleGalaxy", 1u}, {"TeresaMario2DGalaxy", 1u}, {"CubeBubbleExLv2Galaxy", 1u},
    {"TamakoroExLv2Galaxy", 1u}, {"SurfingLv2Galaxy", 1u},
};
inline constexpr std::size_t kGalaxyStageCount = sizeof(kGalaxyStages) / sizeof(kGalaxyStages[0]);
inline constexpr const char* kObservatoryId = "AstroGalaxy";
inline constexpr const char* kObservatoryName = "COMET OBSERVATORY";

void request_galaxy_travel(std::size_t stage, std::uint32_t star) noexcept;
void request_grand_stars() noexcept;
// The game, once per scene frame: a travel the page asked for (true, once), and the Grand Stars.
[[nodiscard]] bool take_galaxy_travel(std::size_t& stage, std::uint32_t& star) noexcept;
[[nodiscard]] bool take_grand_stars() noexcept;
// The page asks for the names while it has none; the game is asked once (true), then gives them.
void want_galaxy_select_names() noexcept;
[[nodiscard]] bool take_galaxy_select_names_wanted() noexcept;
// A name from the game (star 0: the galaxy's).
void set_galaxy_select_name(std::size_t stage, std::uint32_t star, std::string name);
// The stage and star names; empty when not known.
[[nodiscard]] std::string galaxy_select_name(std::size_t stage);
[[nodiscard]] std::string galaxy_select_star_name(std::size_t stage, std::uint32_t star);

}  // namespace galaxy::gx
