// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Surface classes of ray-traced draws, read from the draw's GX shader keys.
// The game marks its shiny materials in the fixed-function state: an environment map is a texture looked up through a
// texgen from normals (gold, silver and chrome decorations), a specular highlight is a lit channel with the specular
// light function, and water, ice and lava distort their textures through indirect stages. Map geometry carries baked
// vertex colour without lighting (unlit); the shaders find its emitters per pixel.

#include "galaxy/gx/shader_keys.h"

#include <cstdint>
#include <string_view>

namespace galaxy::gx {

enum class RtSurfaceClass : std::uint8_t {
    Diffuse = 0,    // lit by the game's lights
    Glossy = 1,     // a lit channel with the specular function
    Metal = 2,      // an environment map
    Unlit = 3,      // baked or self-lit colour (no lit channel reaches the TEV)
    Water = 4,      // opaque surface with indirect texturing
    // 5, 6: the reflections' translucent water (rt_reflections.hlsl).
    Crystal = 7,    // clear dielectric named by its object (crystals, ice, gems, glass)
};

// Features behind the class (diagnostics).
inline constexpr std::uint32_t kRtSurfaceEnvMap = 1u << 0u;        // texgen from normals feeds a colour combiner
inline constexpr std::uint32_t kRtSurfaceEnvAdded = 1u << 1u;      // ... whose lookup is added to another colour
inline constexpr std::uint32_t kRtSurfaceEnvBlended = 1u << 2u;    // ... whose lookup replaces or scales the colour
inline constexpr std::uint32_t kRtSurfaceSpecular = 1u << 3u;      // a lit channel with the specular function
inline constexpr std::uint32_t kRtSurfaceUnlit = 1u << 4u;         // no lit raster colour reaches the TEV
inline constexpr std::uint32_t kRtSurfaceIndirect = 1u << 5u;      // an indirect texture stage
inline constexpr std::uint32_t kRtSurfaceProjected = 1u << 6u;     // texgen from positions feeds a combiner

struct RtSurface {
    RtSurfaceClass surface_class = RtSurfaceClass::Diffuse;
    std::uint8_t features = 0;
    // The first stage that looks up the environment map, 0xFF for none.
    std::uint8_t env_stage = 0xFFu;
};

[[nodiscard]] RtSurface classify_rt_surface(const VertexShaderKey& vs, const PixelShaderKey& ps) noexcept;

// The class of a draw of the object named `name` (NameObj::mName): materials the GX state does not
// tell apart from metal (crystals, ice, gems, glass) by name; else `gx`.
[[nodiscard]] RtSurfaceClass rt_named_class(std::string_view name, RtSurfaceClass gx) noexcept;

// GALAXY_RT_MATERIALS=1: a census of the scene's 3D materials, one [rt-material] line per new
// combination of object name, class, blend and environment texture.
struct RtMaterialNote {
    const char* name = "";           // the drawing NameObj's name ("" unknown)
    std::uint32_t object = 0;        // its guest address
    RtSurface surface{};
    std::uint32_t stages = 0;
    std::uint32_t texgens = 0;
    std::uint32_t indirect = 0;
    std::uint32_t channel = 0;       // colour channel 0 control (XF)
    std::uint32_t combiner = 0;      // the environment map stage's colour combiner (0: none)
    std::uint32_t blend = 0;         // 0 opaque, 1 alpha, 2 additive, 3 other
    std::uint32_t texture = 0;       // the environment map (else map 0): guest address
    std::uint32_t texture_size = 0;  // width | height << 16
    std::uint32_t texture_format = 0;
    std::uint32_t phase = 0;
    std::uint32_t buffer = 0xFFFFu;  // MR::DrawBufferType of the draw (bit 16: its translucent list)
    std::uint64_t pack = 0;          // texture-pack keys (the table's hashes): the texture above, the material's
    std::uint64_t material_pack = 0;
};
[[nodiscard]] bool rt_material_census() noexcept;
void note_rt_material(const RtMaterialNote& note);

}  // namespace galaxy::gx
