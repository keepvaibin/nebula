// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The ray tracer's tables of the pak: a stage's numbers and the particle effects as lights. Hashes and numbers only, no
// game data. What a ray reads of a model's material is in its file (nebula_model.h NebulaModelHit, the cooker's).

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace galaxy::render {

// A stage's ray tracing numbers: the pak's NebulaData/RtStages.tsv, cooked from
// crates/nebula-import/data/rt_stages.tsv (a mod's pack may carry rows for its own stages). Tab-separated, a header
// row naming the columns (a column's words may be joined by '_'; columns not named here are ignored):
//   stage  sun  sun gain  sun size  ambient gain  sky gain  exposure low  exposure high  air  water clear  shadow sets
// stage: the game's stage name (EggStarGalaxy). sun: auto (the sun the stage shows, else its key light held in the
//   world, else none where the key is off), object (the sun it shows only), key (its key light only), none, or three
//   numbers: the world direction toward the sun. shadow sets: keep (the zones' shadow light sets stay as the artists
//   made them while the tracer shadows; empty: they give way to their lit partners). air: the density of the air
//   that scatters light toward the eye, per unit: 0.000001 a day sky's (Koschmieder: a 39 km range), more in
//   haze and dust (at most 0.01); empty or no row: none (space). Empty fields keep the defaults;
//   a row exists only where a picture showed a default wrong, and a `note` column says what.
struct RtStage {
    enum class Sun : std::uint8_t { Auto = 0, Object = 1, Key = 2, None = 3, Fixed = 4 };
    Sun sun = Sun::Auto;
    float direction[3] = {0.0f, 1.0f, 0.0f};  // Fixed: toward the sun in the stage's world, unit length
    float sun_gain = 1.0f;                    // multiplies the key light
    float sun_size = 0.026f;                  // the tangent of the sun disc's half angle (rt_walk.cpp kSunTan)
    float ambient_gain = 1.10f;               // multiplies the stage's ambient (kAmbientGain)
    float sky_gain = 1.0f;                    // multiplies the sky dome as light
    float exposure_low = 40.0f;               // the stage's exposure's limits, lux
    float exposure_high = 150.0f;
    float air = 0.0f;                         // the air's density, per unit; 0: none (space, no row)
    float water_clear = 1770.0f;              // green's 1/e way under water, units (pure water at 1 unit a cm)
    bool shadow_sets_kept = false;
};
inline constexpr RtStage kRtStageDefaults{};

class RtStageTable {
public:
    // The rows read.
    std::size_t read(std::string_view text);
    [[nodiscard]] const RtStage* find(std::uint32_t hash) const noexcept;

private:
    std::unordered_map<std::uint32_t, RtStage> rows_;
};

// A stage's name as the frame list carries it (NebulaFrameList::stage_hash): FNV-1a, 32 bits, 0 becoming 1.
[[nodiscard]] std::uint32_t rt_stage_hash(std::string_view name) noexcept;

// The particle effects as lights: the pak's NebulaData/RtEffects.tsv, cooked from crates/nebula-import/data/
// rt_effects.tsv (a mod's pack may carry rows for its own effects). Tab-separated, a header
// row naming the columns:
//   effect  luminance  behaviour  (others ignored)
// effect: the effect's name (ParticleNames). luminance: candela a square metre of what its particles show; 0 not a
// light. behaviour: steady, flicker, pulse, flash, streak, own (by its own animation). The compiled-in table
// (rt_effect_lights.inc) answers for effects without a row.
struct RtEffectRow {
    float luminance = 0.0f;
    std::uint8_t behaviour = 0u;  // 0 steady, 1 flicker, 2 pulse, 3 flash, 4 streak, 5 own
};

class RtEffectTable {
public:
    std::size_t read(std::string_view text);
    [[nodiscard]] const RtEffectRow* find(std::uint32_t hash) const noexcept;

private:
    std::unordered_map<std::uint32_t, RtEffectRow> rows_;
};

// An effect's name as its draws carry it (NebulaFrameItem::source_hash, the game's MotionSignal): FNV-1a over
// "Effect/" and the name in lower case, 0 becoming 1.
[[nodiscard]] std::uint32_t rt_effect_hash(std::string_view name) noexcept;

// Where the pak holds the tables.
inline constexpr const char* kRtStagesPath = "NebulaData/RtStages.tsv";
inline constexpr const char* kRtEffectsPath = "NebulaData/RtEffects.tsv";

// Once, before the renderer starts (the host, from the pak): the tables read from then on, from any thread.
void set_rt_tables(std::string_view stages, std::string_view effects);
// A stage's row; null where it has none (kRtStageDefaults hold).
[[nodiscard]] const RtStage* rt_stage(std::uint32_t hash) noexcept;
// An effect's row of the pak's table; null where it has none.
[[nodiscard]] const RtEffectRow* rt_effect(std::uint32_t hash) noexcept;

}  // namespace galaxy::render
