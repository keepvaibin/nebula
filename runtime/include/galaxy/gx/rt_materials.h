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
// colours live in the game's colour registers, not in the shared texture). helper: a lookup texture
// (environment, indirect or mask map) that never decides a draw's material. skip: never a light.
// hide: a light effect the game fakes (a beam, a cone, a flare) that the traced light replaces; not
// drawn while the ray-traced lighting is on. unlit-only: only the draws the game does not light
// emit (a lamp's glass beside its lit housing in one texture).
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
    bool lens = false;                    // a black hole's lens draw: the finish bends light around it
    bool matte = false;                   // foliage, cloth: absorbs light, no highlight or reflection
    bool sky = false;                     // a sky or backdrop mesh: drawn, never in the traced scene
    bool shadow = true;
    float roughness = -1.0f;              // < 0: the class's
    float metallic = -1.0f;               // < 0: the class's
    float color[3] = {1.0f, 1.0f, 1.0f};
    float intensity = 0.0f;               // relative to a Luma
    float range = 0.0f;
    float pulse = 0.0f;
    float cone = 0.0f;                    // degrees; 0: a point light
    float glow = -1.0f;                   // < 0: the intensity
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

private:
    std::vector<RtMaterial> materials_;
    std::unordered_map<std::uint64_t, std::uint16_t> by_hash_;
    std::size_t skipped_ = 0;
};

// A stage's sun: GALAXY_RT_SUNS_FILE, else rt-suns.tsv beside the executable; tab-separated with a
// header naming its columns:
//   stage  source  x  y  z  r  g  b  strength  size  shafts  (others ignored)
// stage: the game's stage name (EggStarGalaxy). source: game (the game's visible sun, else the one its
// sky shows: as without a row), table (x y z: the world direction towards the sun, where the sky
// shows it), none (no sun or sky key: the stage's own lights carry it). r g b: its colour (0-1); strength: its light relative to the default; size: the disc's
// radius in degrees; shafts: the light shafts' strength (GALAXY_RT_POST_SHAFTS). Empty fields keep
// the defaults.
struct RtStageSun {
    enum class Source : std::uint8_t { Game = 0, Table = 1, None = 2 };
    Source source = Source::Game;
    float direction[3] = {0.0f, 1.0f, 0.0f};
    bool has_color = false;
    float color[3] = {1.0f, 1.0f, 1.0f};
    float strength = 1.0f;
    float size = -1.0f;                   // < 0: the default
    float shafts = -1.0f;                 // < 0: the default
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
[[nodiscard]] const RtMaterial* rt_material(std::uint16_t index) noexcept;

}  // namespace galaxy::gx
