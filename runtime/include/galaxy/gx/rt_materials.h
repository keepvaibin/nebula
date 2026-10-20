// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The ray tracer's material and light table: textures named by their texture-pack key (XXH64 of
// the base level, as community texture packs name them) carry a surface (class, roughness,
// metalness) and, for light sources, an emission: a point or spot light at the object (Lumas, star
// bits, fire and ice enemies, a boss's glowing weak spot, a headlamp) or light from the surface
// itself to what sees it (lava, magma). The table holds hashes and numbers only, no game data.
//
// Files: GALAXY_RT_MATERIALS_FILE (several separated by ';'), else rt-materials.tsv and then
// rt-materials-lights.tsv beside the executable. Tab-separated, each with a header naming its
// columns:
//   hash  class  roughness  metallic  r  g  b  intensity  range  pulse  cone  shadow  (others ignored)
// A later row for the same texture (in the same or a later file) replaces only the fields it fills,
// so a lights file can carry emissions alone over the surfaces file.
// hash: a texture-pack name (tex1_<w>x<h>[_m]_<16 hex>_<format>) or the 16 hex digits alone.
// class: '+'-joined tokens. Surfaces: diffuse, glossy, metal, glass, crystal, ice, water, foliage,
// fabric, skin, unlit, sky, particle (never traced geometry or water). Lights: emitter (point light; a spot light with a cone),
// emissive-area (area light), tint (the emission takes the colour the draw shows: Luma and star-bit
// colours live in the game's colour registers, not in the shared texture), strip (a long emitter, a power cable: a
// light per 120-unit stretch along it instead of one for the whole object). helper: a lookup texture
// (environment, indirect or mask map) that never decides a draw's material. skip: never a light.
// hide: a light effect the game fakes (a beam, a cone, a flare) that the traced light replaces; not
// drawn while the ray-traced lighting is on. unlit-only: only the draws the game does not light
// emit (a lamp's glass beside its lit housing in one texture). painted: its brightness changes are the material's own
// (a face, lettering, a picture), not painted light: never de-lit (rt_delight.hlsl).
// roughness: GGX perceptual 0-1; metallic 0-1; r g b: 0-1; intensity: relative to a Luma (1);
// range: eye units (0: from the intensity); pulse: Hz (0: steady); cone: a spot light's half angle in
// degrees around the draw's mean normal (0: all around); shadow: 0 or 1; glow: the light source's own
// surface brightness in the intensity's units, apart from the light it sends (empty: the intensity): an
// area light's surface brightens by it, a point light's halo takes that share of its colour.

#include <cstddef>
#include <cstdint>
#include <istream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace galaxy::gx {

enum class RtEmission : std::uint8_t { None = 0, Point = 1, Area = 2 };

struct RtMaterial {
    std::uint8_t surface_class = 0xFFu;   // RtSurfaceClass; 0xFF: as the GX state shows it
    RtEmission emission = RtEmission::None;
    bool no_light = false;                // skip: never a light, whatever the picture shows
    bool helper = false;                  // a lookup texture: skipped when a draw's material is chosen
    bool tint = false;                    // the emission's colour is the draw's own
    bool hide = false;                    // a faked light effect: not drawn under the traced lighting
    bool unlit_only = false;              // only unlit draws of the texture emit
    bool particle = false;                // smoke, steam, sparks: never traced geometry or water
    bool waterfall = false;               // falling water: water whose streaks run down it
    bool mirror = false;                  // glass the water pass reflects as a mirror (the Observatory fountain)
    bool foliage = false;                 // leaves, grass and flower cards (alpha-cut)
    bool lens = false;                    // a black hole's lens draw: the finish bends light around it
    bool matte = false;                   // foliage, cloth: absorbs light, no highlight or reflection
    bool sky = false;                     // a sky or backdrop mesh: drawn, never in the traced scene
    bool strip = false;                   // a long emitter (a cable): a light per stretch of it
    bool painted = false;                 // its detail is the material's, not light: never de-lit
    bool star = false;                    // a star's burning surface (the star core, the suns): its own colour,
                                          // brighter and more textured (rt_lighting.hlsl CompositePS)
    bool shadow = true;
    float roughness = -1.0f;              // < 0: the class's
    float metallic = -1.0f;               // < 0: the class's
    float color[3] = {1.0f, 1.0f, 1.0f};
    float intensity = 0.0f;               // relative to a Luma
    float range = 0.0f;
    float pulse = 0.0f;
    float cone = 0.0f;                    // degrees; 0: a point light
    float glow = -1.0f;                   // < 0: the intensity
    // An emitter's glowing part of its texture (u0, v0, u1, v1; the table's `region`): Gusty Garden's spin bulbs glow,
    // their leaves (the same texture's other half) do not.
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
    [[nodiscard]] std::uint16_t identity_index(std::uint32_t identity) const noexcept;

private:
    struct StageRow {
        std::string stage;
        std::uint16_t index;
        RtMaterial material;
    };
    std::vector<RtMaterial> materials_;
    std::unordered_map<std::uint64_t, std::uint16_t> by_hash_;
    std::unordered_map<std::uint32_t, std::uint16_t> by_identity_;
    std::vector<StageRow> stage_rows_;
    // Per model name (lower case) the material indices its rows name.
    std::unordered_map<std::string, std::vector<std::uint16_t>> model_rows_;
    std::size_t skipped_ = 0;
};

// A stage's sun: GALAXY_RT_SUNS_FILE, else rt-suns.tsv beside the executable; tab-separated with a
// header naming its columns:
//   stage  source  x  y  z  r  g  b  strength  size  shafts  day  (others ignored)
// stage: the game's stage name (EggStarGalaxy). source: game (the game's visible sun, else the one its
// sky shows: as without a row), table (x y z: the world direction towards the sun, where the sky
// shows it), none (no sun or sky key: the stage's own lights carry it), core (the Comet Observatory: its star core is the
// light, a sphere of radiance `core` (relative to a white surface under the stage's full light); the key keeps the
// `strength` share as moonlight in r g b). r g b: its colour (0-1); strength: its light relative to the default; size: the disc's
// radius in degrees; shafts: the light shafts' strength (GALAXY_RT_POST_SHAFTS); day: 1 the day model (the
// art as drawn with modern shadows), 0 the night model (lit by its lights), between a blend; empty: by the
// brightness of its sky. Empty fields keep the defaults.
struct RtStageSun {
    enum class Source : std::uint8_t { Game = 0, Table = 1, None = 2, Core = 3 };
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
    [[nodiscard]] std::size_t size() const noexcept { return suns_.size(); }

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
// The table's material for a material identity (RtMaterialTable::identity_index; 0: none).
[[nodiscard]] std::uint16_t rt_material_by_identity(std::uint32_t identity) noexcept;
// The hash a row keyed `untextured` takes: on the stage its row names, surfaces without texture coordinates
// (vertex-coloured models: the Observatory's power cables) take that row; no texture row can name them.
inline constexpr std::uint64_t kRtUntexturedHash = 0xFFFFFFFFFFFFFFFFull;
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
