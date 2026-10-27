// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The ray tracer's material and light table. Rows are keyed by texture-pack hash (XXH64 of the base level, as
// community texture packs name textures) and give a surface (class, roughness, metalness) and, for light sources, an
// emission: a point or spot light at the object (Lumas, star bits, a headlamp) or light from the surface itself
// (lava, magma). The table holds hashes and numbers only, no game data.
//
// Files: GALAXY_RT_MATERIALS_FILE (several separated by ';'), else rt-materials.tsv then rt-materials-lights.tsv
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

namespace galaxy::gx {

enum class RtEmission : std::uint8_t { None = 0, Point = 1, Area = 2 };

// The share of their art's tint clear ice and the crystal cages keep under physical ray tracing (the owner, 27 October
// 2026: ice 20 to 25 % of its blue, crystals 5 to 10 %; never darker). Their tint is in the colours their materials
// multiply and add (TEV and konst registers, material and vertex colours: Freezeflame's rink filters what shows through
// it by (0, 40, 65), its sheen by (55, 77, 92)); their textures are grey and what shows through them (the screen
// capture the ice refracts, its reflection) keeps its own colours. Those colours keep that share of their chroma, their
// luminance kept: GxBackend clears the registers in a draw's constants, the pixel shaders its rasterized colours
// (ztex_params.w = kept - 1).
inline constexpr float kRtIceTintKept = 0.225f;
inline constexpr float kRtCrystalTintKept = 0.075f;

struct RtMaterial {
    std::uint8_t surface_class = 0xFFu;   // RtSurfaceClass; 0xFF: as the GX state shows it
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
    // The radiance (relative to a white surface under the stage's full light) of the brightest light source among
    // the textures whose rows name `model` in their object column ("Model/texture", or models apart by spaces),
    // case-insensitive; 0: the model has none.
    [[nodiscard]] float model_light(std::string_view model) const;
    // A row with a `material` ("Model/material": the model's resource name and J3D's material name, models apart by
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

// A stage's sun: GALAXY_RT_SUNS_FILE, else rt-suns.tsv beside the executable. Tab-separated, with a header row naming
// the columns:
//   stage  source  x  y  z  r  g  b  strength  size  shafts  day  ambient  core  (others ignored)
// stage: the game's stage name (EggStarGalaxy).
// source: game (the game's visible sun, else the one its sky shows; the same as no row); table (x y z: the world
//   direction towards the sun, where the sky shows it); none (no sun or sky key: the stage's own lights carry it);
//   core (the Comet Observatory: its star core is the light, a sphere of radiance `core` relative to a white surface
//   under the stage's full light; the key keeps the `strength` share as moonlight in r g b); moon (a night stage's
//   moon: x y z its world direction, the `strength` share of the stage's key in r g b, the camera exposed for the
//   stage's full light).
// r g b: its colour (0-1). strength: its light relative to the default. size: the disc's radius in degrees.
// shafts: the light shafts' strength (times GALAXY_RT_CAMERA_SHAFTS). day: 1 the day model (the art as drawn, with
//   modern shadows), 0 the night model (lit by its lights), a blend between; empty: from the sky's brightness.
// ambient: the night model's open-art level (times GALAXY_RT_NIGHT_LEVEL). Empty fields keep the defaults.
struct RtStageSun {
    enum class Source : std::uint8_t { Game = 0, Table = 1, None = 2, Core = 3, Moon = 4 };
    Source source = Source::Game;
    float direction[3] = {0.0f, 1.0f, 0.0f};
    bool has_color = false;
    float color[3] = {1.0f, 1.0f, 1.0f};
    float strength = 1.0f;
    float size = -1.0f;                   // < 0: the default
    float shafts = -1.0f;                 // < 0: the default
    float day = -1.0f;                    // < 0: by the sky's brightness
    float ambient = 1.0f;                 // the night model's open-art level for this stage (x GALAXY_RT_NIGHT_LEVEL)
    float core = 0.0f;                    // source core: the star core's radiance (white under the full light: 1)
};

class RtStageSunTable {
public:
    std::size_t read(std::istream& in);
    [[nodiscard]] const RtStageSun* find(std::string_view stage) const noexcept;

private:
    std::unordered_map<std::string, RtStageSun> suns_;
};

// The stage's row of the loaded sun table; null without one.
[[nodiscard]] const RtStageSun* rt_stage_sun(std::string_view stage) noexcept;

// XXH64 with seed 0 of a texture's base level: the texture-pack key.
[[nodiscard]] std::uint64_t texture_pack_hash(const std::uint8_t* data, std::size_t bytes) noexcept;

// The table is loaded on first use; false when it is missing or empty (nothing is hashed then).
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
// names the effects ("Effect/<ParticleNames.bcsv name>", motion::kMarkerEffect) its properties hold for.
inline constexpr std::uint64_t kRtEffectHash = 0xFFFFFFFFFFFFFFFEull;
// The loaded stage's material for its untextured surfaces (0: the stage has no `untextured` row).
[[nodiscard]] std::uint16_t rt_stage_untextured_material() noexcept;
[[nodiscard]] const RtMaterial* rt_material(std::uint16_t index) noexcept;
// GALAXY_RT_LOG_GLOWS=1: the texture-pack name of each texture as uploaded, kept by guest address, and
// printed for the glow draws ([rt-glow]) and the traced surfaces ([rt-surface]) the table does not name.
void note_rt_texture(std::uint32_t guest_addr, std::uint64_t pack, std::uint32_t width, std::uint32_t height,
                     std::uint32_t format);
void note_rt_unnamed_glow(std::uint32_t guest_addr);
void note_rt_unnamed_surface(std::uint32_t guest_addr);
[[nodiscard]] bool rt_log_glows() noexcept;
// The loaded stage: its rows of the table (RtMaterialTable::stage_materials) take effect.
void rt_materials_set_stage(std::string_view stage);
// Counts the stages set: a new value is a new stage.
[[nodiscard]] std::uint32_t rt_materials_stage_serial() noexcept;

}  // namespace galaxy::gx
