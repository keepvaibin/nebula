// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// Surface classes of ray-traced draws, read from the draw's GX shader keys. The game marks its
// shiny materials in the fixed-function state itself: an environment map is a texture looked up
// through a texgen from normals (gold, silver and chrome decorations), a specular highlight is a
// lit channel with the specular light function, and water, ice and lava distort their textures
// through indirect stages. Map geometry carries baked vertex colour without lighting ("unlit");
// its emitters are the unlit pixels that are bright, which the shaders decide per pixel.

#include "galaxy/gx/shader_keys.h"

#include <cstdint>

namespace galaxy::gx {

enum class RtSurfaceClass : std::uint8_t {
    Diffuse = 0,    // lit by the game's lights
    Glossy = 1,     // a lit channel with the specular function
    Metal = 2,      // an environment map
    Unlit = 3,      // baked or self-lit colour (no lit channel reaches the TEV)
    Water = 4,      // opaque surface with indirect texturing
};
inline constexpr unsigned kRtSurfaceClasses = 5u;

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

}  // namespace galaxy::gx
