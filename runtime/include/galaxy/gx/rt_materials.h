// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The ray tracer's material and light table: textures named by their texture-pack key (XXH64 of
// the base level, as community texture packs name them) carry a surface class and, for light
// sources, an emission: a point light at the object (Lumas, star bits, fire and ice enemies, a
// boss's glowing weak spot) or light from the surface itself to what sees it (lava, magma).
// Read once from GALAXY_RT_MATERIALS_FILE, else rt-materials.tsv beside the executable. The table
// holds hashes and numbers only, no game data. Tab-separated, with a header naming the columns:
//   hash  class  r  g  b  intensity  range  pulse  shadow  (others ignored)
// hash: a texture-pack name (tex1_<w>x<h>[_m]_<16 hex>_<format>) or the 16 hex digits alone.
// class: '+'-joined tokens: emitter (point light), emissive-area (area light), metal, glossy,
// glass, crystal, ice, water, diffuse, unlit, skip (no light, whatever the picture shows).
// r g b: 0-1; intensity: relative to a Luma (1); range: eye units (0: from the intensity);
// pulse: Hz (0: steady); shadow: 0 or 1.

#include <cstddef>
#include <cstdint>

namespace galaxy::gx {

enum class RtEmission : std::uint8_t { None = 0, Point = 1, Area = 2 };

struct RtMaterial {
    std::uint8_t surface_class = 0xFFu;   // RtSurfaceClass; 0xFF: as the GX state shows it
    RtEmission emission = RtEmission::None;
    bool no_light = false;                // skip: never a light, whatever the picture shows
    bool shadow = true;
    float color[3] = {1.0f, 1.0f, 1.0f};
    float intensity = 0.0f;               // relative to a Luma
    float range = 0.0f;
    float pulse = 0.0f;
};

// XXH64 with seed 0 of a texture's base level: the texture-pack key.
[[nodiscard]] std::uint64_t texture_pack_hash(const std::uint8_t* data, std::size_t bytes) noexcept;

// The table is loaded on first use; false when it is missing or empty (nothing is hashed then).
[[nodiscard]] bool rt_materials_loaded() noexcept;
// 0: not in the table; else its index + 1.
[[nodiscard]] std::uint16_t rt_material_index(std::uint64_t hash) noexcept;
[[nodiscard]] const RtMaterial* rt_material(std::uint16_t index) noexcept;

}  // namespace galaxy::gx
