// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The GAME page's galaxy select (GALAXY_GALAXY_SELECT=1, set in preview packages for testing):
// any galaxy and star, entered with the game's own stage change
// (MR::requestChangeStageInGameMoving) at the simulation thread's next scene. The names shown are
// the game's own, read from its message data through MR::getGalaxyNameOnCurrentLanguage and
// MR::getScenarioNameOnCurrentLanguage the first time the page asks for them.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::gx {

struct GalaxyStage {
    const char* id;       // the stage's name in the game's data
    std::uint32_t stars;  // scenarios that can be entered (1..stars)
};

// By dome (Terrace, Fountain, Kitchen, Bedroom, Engine Room, Garden), then the rest.
// stars leaves out the hidden scenarios (ScenarioData IsHidden, numbered after the others): the game sizes
// its per-scenario data by the count without them (ScenarioData::getScenarioNum), so entering one directly
// indexes past those arrays (IceVolcanoGalaxy 6 crashed in SpinDriverPathStorageScenario::setup).
inline constexpr GalaxyStage kGalaxyStages[] = {
    // The Comet Observatory (its first scenario: the stage change the galaxy tours enter it with). Not a galaxy
    // to the game's name lookup: shown as kObservatoryName.
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

// The stars of a stage in kGalaxyStages; 0 for other stages.
[[nodiscard]] std::uint32_t galaxy_stage_stars(std::string_view id) noexcept;

[[nodiscard]] bool galaxy_select_enabled() noexcept;

// Menu side.
void request_galaxy_travel(std::size_t stage, std::uint32_t star) noexcept;
void want_galaxy_select_names() noexcept;
// The game's names in upper case ("" until read).
[[nodiscard]] std::string galaxy_select_name(std::size_t stage);
[[nodiscard]] std::string galaxy_select_star_name(std::size_t stage, std::uint32_t star);

// Simulation thread, at a scene's start.
[[nodiscard]] bool take_galaxy_travel(std::size_t& stage, std::uint32_t& star) noexcept;
[[nodiscard]] bool galaxy_select_names_wanted() noexcept;
void set_galaxy_select_names(std::vector<std::string> galaxies, std::vector<std::vector<std::string>> stars);

// UTF-16 code units of a game message as the menu font shows them: upper case letters, digits and
// its punctuation; others are dropped, accents lose their marks.
[[nodiscard]] std::string galaxy_select_text(const std::vector<std::uint16_t>& units);

}  // namespace galaxy::gx
