// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The ray tracer's material and light table. Rows are keyed by texture-pack hash (XXH64 of the base level, as
// community texture packs name textures) and give a surface (class, roughness, metalness) and, for light sources, an
// emission: a point or spot light at the object (Lumas, star bits, a headlamp) or light from the surface itself
// (lava, magma). The table holds hashes and numbers only, no game data.
//
// Files: rt-materials.tsv then rt-materials-lights.tsv
// beside the executable. Tab-separated, with a header row naming the columns:
//   hash  class  roughness  metallic  r  g  b  intensity  range  pulse  cone  shadow  glow  stage  region  object
//   material  (others ignored)
// A later row for the same texture, in the same or a later file, replaces only the fields it fills, so a lights file
// can add emissions over the surfaces file.
//
// hash: a texture-pack name (tex1_<w>x<h>[_m]_<16 hex>_<format>), the 16 hex digits alone, or `untextured`.
// class: '+'-joined tokens.
//   Surfaces: diffuse, glossy, metal, glass, crystal, ice, water, foliage, fabric, skin, unlit, sky, particle (never
//   traced geometry or water).
//   Lights: emitter (a point light; a spot light with a cone), emissive-area (an area light), tint (the emission takes
//   the colour the draw shows, for lights whose colour is in the game's colour registers rather than the shared
//   texture), strip (a long emitter such as a power cable: one light per 120-unit stretch).
//   helper: a lookup texture (environment, indirect or mask map) that never decides a draw's material.
//   skip: never a light.
//   hide: a light effect the game fakes (a beam, a cone, a flare) that the traced light replaces; not drawn while the
//   ray-traced lighting is on.
//   unlit-only: only the draws the game does not light emit (a lamp's glass beside its lit housing in one texture).
//   painted: its brightness changes are the material's own (a face, lettering, a picture), so it is never de-lit
//   (rt_delight.hlsl).
//   delight: its texture carries light painted into it (bevel highlights, groove shadows, a metal band's sheen) that the
//   traced light replaces, so it is de-lit (rt_delight.hlsl). Only these: a texture's own detail (stone, sand, wood
//   grain, painted tiles) is its colour, and taking it out flattens the texture and turns it into false relief.
//   traced: a clear surface the engine traces (rt_engine.hlsl EngineClearCS) whose game draw paints a fake
//   reflection; the draw is hidden under the engine, its geometry still traced.
//   seen: a light source's surface that shows its radiance while the game's own point light there is its light.
//   own (first, in `material` rows): the row alone, not on top of its texture's row.
// roughness: GGX perceptual, 0-1. metallic: 0-1. r g b: 0-1. intensity: relative to a Luma (1).
// range: eye units (0: from the intensity). pulse: Hz (0: steady). cone: a spot light's half angle in degrees around
// the draw's mean normal (0: all around). shadow: 0 or 1. glow: the light source's own surface brightness in the
// intensity's units (empty: the intensity); an area light's surface brightens by it, a point light's halo takes that
// share of its colour.

#include <cstddef>
#include <cstdint>
#include <istream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace galaxy::render {

// A texture-pack material's surface class.
enum class RtSurfaceClass : std::uint8_t {
    Diffuse = 0,    // lit by the game's lights
    Glossy = 1,     // a lit channel with the specular function
    Metal = 2,      // an environment map
    Unlit = 3,      // baked or self-lit colour
    Water = 4,      // opaque surface with indirect texturing
    Crystal = 7,    // clear dielectric (crystals, ice, gems, glass)
};

enum class RtEmission : std::uint8_t { None = 0, Point = 1, Area = 2 };

// The share of their art's tint clear ice and the crystal cages keep under physical ray tracing (ice 20 to 25 % of
// its blue, crystals 5 to 10 %; never darker). Their tint is in the colours their materials
// multiply and add (material and konst registers, material and vertex colours: Freezeflame's rink filters what shows through
// it by (0, 40, 65), its sheen by (55, 77, 92)); their textures are grey and what shows through them (the screen
// capture the ice refracts, its reflection) keeps its own colours. Those colours keep that share of their chroma, their
// luminance kept: RenderCore clears the registers in a draw's constants, the pixel shaders its rasterized colours
// (ztex_params.w = kept - 1).
inline constexpr float kRtIceTintKept = 0.225f;
inline constexpr float kRtCrystalTintKept = 0.075f;

struct RtMaterial {
    std::uint8_t surface_class = 0xFFu;   // RtSurfaceClass; 0xFF: as the state shows it
    RtEmission emission = RtEmission::None;
    bool no_light = false;                // skip: never a light, whatever the picture shows
    bool helper = false;                  // a lookup texture: skipped when a draw's material is chosen
    bool tint = false;                    // the emission's colour is the draw's own
    bool hide = false;                    // a faked light effect: not drawn under the traced lighting
    bool unlit_only = false;              // only unlit draws of the texture emit
    bool particle = false;                // smoke, steam, sparks: never traced geometry or water
    bool luminous = false;                // an effect that is light (sparkles, glows, flashes, fire, electricity,
                                          // beams): drawn as the game draws it, never lit by the traced light
    bool waterfall = false;               // falling water: water whose streaks run down it
    float white = 1.0f;                   // falling water: the share of its painted white water drawn (`white<n>`;
                                          // `unpainted` 0: clear water throughout; rt_engine.hlsl fall_water)
    bool mist = false;                    // an effect's sprites are spray: puffs of drops under the engine, the
                                          // sprites hidden (rt_engine.hlsl MistLightCS)
    bool mirror = false;                  // glass the water pass reflects as a mirror (the Observatory fountain)
    bool ice = false;                     // ice: clear as glass (its own look as the game draws it, a traced
                                          // reflection) yet shaded by the traced sun's shadow
    float tint_kept = 1.0f;               // the share of its art's tint kept under physical light: `ice` and
                                          // `icetint` kRtIceTintKept, `crystaltint` kRtCrystalTintKept
    bool foliage = false;                 // leaves, grass and flower cards (alpha-cut)
    bool lens = false;                    // a black hole's lens draw: the finish bends light around it
    bool matte = false;                   // foliage, cloth: absorbs light, no highlight or reflection
    bool sky = false;                     // a sky or backdrop mesh: drawn, never in the traced scene
    bool strip = false;                   // a long emitter (a cable): a light per stretch of it
    bool painted = false;                 // its detail is the material's, not light: never de-lit
    bool delight = false;                 // its texture carries painted light (`delight`): the only textures de-lit
    bool star = false;                    // a star's burning surface (the star core, the suns): its own colour,
                                          // brighter and more textured (rt_lighting.hlsl CompositePS)
    bool traced = false;                  // a clear surface the engine traces; its game draw not shown
    bool solid = false;                   // with `traced`: a closed clear solid (a crystal), traced through from the
                                          // scene's own geometry (rt_engine.hlsl clear_solid); color: what it passes
                                          // after `range` world units (0: it absorbs nothing)
    float ior = 0.0f;                     // index of refraction (token ior<n>, 1 to 5; 0: none): a dielectric's
                                          // reflectance; a solid's refraction (0: quartz's)
    bool seen = false;                    // shows its radiance; its light is the game's point light there
    bool recolour = false;                // a light source's sprite shown in the table's colour (its luminance kept)
    bool near_glow = false;               // a light source only near its glow light (the game's sprite of it)
    bool volume = false;                  // a glowing volume drawn as geometry (a beam, a flame jet): its light
                                          // leaves it every way, whatever its faces' normals (GlowEmitterCS)
    bool shadow = true;
    float roughness = -1.0f;              // < 0: the class's
    float metallic = -1.0f;               // < 0: the class's
    float color[3] = {1.0f, 1.0f, 1.0f};
    float intensity = 0.0f;               // relative to a Luma
    float range = 0.0f;
    // Falling water: the share of its clear water's light its bubbles scatter (the table's `aeration`; < 0: the
    // default, kFallAeration).
    float aeration = -1.0f;
    float pulse = 0.0f;
    float cone = 0.0f;                    // degrees; 0: a point light
    float glow = -1.0f;                   // < 0: the intensity
    // An emitter's glowing part of its texture (u0, v0, u1, v1; the table's `region`), for a texture that also holds
    // parts that do not glow.
    float region[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    // Surface relief from the texture's detail under the sun (ground, rock, bark, cloth; none on
    // skin, paint, metal, glass): the strength of the per-pixel normal its brightness gives.
    float relief = 0.0f;
};

class RtMaterialTable {
public:
    // Adds one file's rows; returns the rows it read.
    std::size_t read(std::istream& in);
    [[nodiscard]] bool empty() const noexcept { return materials_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return materials_.size(); }
    [[nodiscard]] std::size_t skipped() const noexcept { return skipped_; }
    // 0: not in the table; else its index + 1.
    [[nodiscard]] std::uint16_t index(std::uint64_t hash) const noexcept;
    [[nodiscard]] const RtMaterial* material(std::uint16_t index) const noexcept;
    // A row with a `stage` (the game's stage name) holds for that stage alone: its texture's material there
    // is the texture's own with that row on top (the Observatory's fountain is glass, the same water texture
    // elsewhere stays water). Per material index, its stage material.
    [[nodiscard]] std::vector<std::pair<std::uint16_t, const RtMaterial*>> stage_materials(
        std::string_view stage) const;
    // A row with a `material` ("Model/material": the model's resource name and its material name, models apart by
    // spaces) names those models' material alone (MotionSignal::drawMaterial's identity): its texture's material with
    // that row on top (the Observatory's white star lamps, not every white plastic its texture is on). 0: none.
    // With a `stage` as well, the row holds on that stage alone (stage_identities).
    [[nodiscard]] std::uint16_t identity_index(std::uint32_t identity) const noexcept;
    // The identity rows of a stage: per identity, its material index there.
    [[nodiscard]] std::vector<std::pair<std::uint32_t, std::uint16_t>> stage_identities(std::string_view stage) const;

private:
    struct StageRow {
        std::string stage;
        std::uint16_t index;
        RtMaterial material;
    };
    struct StageIdentity {
        std::string stage;
        std::uint32_t identity;
        std::uint16_t index;
    };
    std::vector<RtMaterial> materials_;
    std::unordered_map<std::uint64_t, std::uint16_t> by_hash_;
    std::unordered_map<std::uint32_t, std::uint16_t> by_identity_;
    std::vector<StageRow> stage_rows_;
    std::vector<StageIdentity> stage_identities_;
    // Per model name (lower case) the material indices its rows name.
    std::unordered_map<std::string, std::vector<std::uint16_t>> model_rows_;
    std::size_t skipped_ = 0;
};

// A stage's ray tracing numbers (RT-MASTER-PLAN 23.2): the pak's NebulaData/RtStages.tsv, cooked from
// crates/nebula-import/data/rt_stages.tsv (a mod's pack may carry rows for its own stages). Tab-separated, a header
// row naming the columns (a column's words may be joined by '_'; columns not named here are ignored):
//   stage  sun  sun gain  sun size  ambient gain  sky gain  exposure low  exposure high  air  water clear  shadow sets
// stage: the game's stage name (EggStarGalaxy). sun: auto (the sun the stage shows, else its key light held in the
//   world, else none where the key is off), object (the sun it shows only), key (its key light only), none, or three
//   numbers: the world direction toward the sun. shadow sets: keep (the zones' shadow light sets stay as the artists
//   made them while the tracer shadows; empty: they give way to their lit partners). air: the density of the air
//   that scatters light toward the eye (15.4), per unit: 0.000001 a day sky's (Koschmieder: a 39 km range), more in
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
    float exposure_low = 40.0f;               // the stage's exposure's limits, lux (3.1)
    float exposure_high = 150.0f;
    float air = 0.0f;                         // the air's density, per unit (15.4); 0: none (space, no row)
    float water_clear = 600.0f;               // units of water to mostly opaque (kWaterClear)
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
// rt_effects.tsv (RT-MASTER-PLAN 8.1 L-4; a mod's pack may carry rows for its own effects). Tab-separated, a header
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

// XXH64 with seed 0 of a texture's base level: the texture-pack key.
[[nodiscard]] std::uint64_t texture_pack_hash(const std::uint8_t* data, std::size_t bytes) noexcept;

// The table is loaded on the first call of this or of any reader below (not at start); false when it is missing or empty.
[[nodiscard]] bool rt_materials_loaded() noexcept;
// 0: not in the table; else its index + 1.
[[nodiscard]] std::uint16_t rt_material_index(std::uint64_t hash) noexcept;
// A material's identity: FNV-1a of "model/material" in lower case, the model without its extension
// (native/petari MotionSignal.cpp hashName).
[[nodiscard]] std::uint32_t rt_material_identity(std::string_view name) noexcept;
// The table's material for a material identity (RtMaterialTable::identity_index; 0: none), the loaded stage's own
// row for it first (RtMaterialTable::stage_identities).
[[nodiscard]] std::uint16_t rt_material_by_identity(std::uint32_t identity) noexcept;
// The hash of a row keyed `untextured`: on that row's stage, surfaces without texture coordinates (vertex-coloured
// models such as the Observatory's power cables) take it, since no texture row can name them.
inline constexpr std::uint64_t kRtUntexturedHash = 0xFFFFFFFFFFFFFFFFull;
// The rows of the game's particle effects (`effect` in the hash column): no texture of their own; each row's `material`
// names the effects ("Effect/<ParticleNames.bcsv name>") its properties hold for.
inline constexpr std::uint64_t kRtEffectHash = 0xFFFFFFFFFFFFFFFEull;
// The loaded stage's material for its untextured surfaces (0: the stage has no `untextured` row).
[[nodiscard]] std::uint16_t rt_stage_untextured_material() noexcept;
[[nodiscard]] const RtMaterial* rt_material(std::uint16_t index) noexcept;
// The loaded stage: its rows of the table (RtMaterialTable::stage_materials) take effect.
void rt_materials_set_stage(std::string_view stage);
// Counts the stages set: a new value is a new stage.
[[nodiscard]] std::uint32_t rt_materials_stage_serial() noexcept;

}  // namespace galaxy::render
