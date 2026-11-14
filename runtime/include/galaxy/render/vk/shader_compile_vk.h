// SPDX-License-Identifier: GPL-3.0-only
// HLSL to SPIR-V through glslang's HLSL front end (docs/VULKAN.md). The generated shaders and the renderer's own
// shaders keep their D3D12 register declarations; annotate() adds the Vulkan binding each register maps to, so the
// D3D12 sources (and the hashes their caches use) stay as they are.
#pragma once

#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::render::vk {

enum class ShaderStage { Vertex, Geometry, Fragment, Compute };

// One D3D12 register's Vulkan binding: `reg` as written in the source ("b0", "t0, space1", "s0"); a push-constant
// block when `push_constant` is set.
struct RegisterBinding {
    std::string_view reg;
    std::uint32_t set;
    std::uint32_t binding;
    bool push_constant = false;
};

// The bindless image array: every texture view at its TextureStore index and the null view in the last slot. The shaders
// declare it runtime-sized and the layout's upper bound is the device's (vk::Context::texture_array_max, at most
// kBindlessTextureSlotsCap, which keeps a driver that reports 2^32 - 1 from being asked for that); a set is allocated for
// the views in use (at least kBindlessTextureSlotsMin slots) and replaced by one twice as large when a view index reaches
// its end (RendererVk::grow_texture_array).
inline constexpr std::uint32_t kBindlessTextureSlotsMin = 4096u;
// The texture array's first size: it doubles as the views in use reach it.
inline constexpr std::uint32_t kBindlessTextureSlotsFirst = 64u;
inline constexpr std::uint32_t kBindlessTextureSlotsCap = 1u << 20u;
// The sampler array: one slot per distinct sampler (SamplerSlotCache, which holds kSamplerHeapSlots = this), the default
// sampler in slot 0. Its length is the cache's own number, so the set is fixed-size.
inline constexpr std::uint32_t kBindlessSamplerSlots = 2048u;
// The maps of one bound table on a device without descriptor indexing: the length of the programs' tex[] and samp[] in
// the build's fallback forms and of the layout's arrays (vk::Context::table_slots() must reach it). It is the Vulkan
// specification's minimum for maxPerStageDescriptorSampledImages and maxPerStageDescriptorSamplers (16), so every device
// holds it; the programs are built ahead of time and cannot take the device's own number.
inline constexpr std::uint32_t kBoundTableSlots = 16u;

// The draw layout: set 0 = view/material/light buffers, composed rows and instances, and the draws' texture lists (t18);
// set 1 = the textures (the bindless array, or one bound table); set 2 = the samplers (likewise); b3 = draw push constants.
// The two forms of set 1 and 2 share their bindings; program.hlsli declares one of them.
[[nodiscard]] std::span<const RegisterBinding> draw_bindings() noexcept;

// Inserts the Vulkan attribute before every resource declared with a mapped register.
[[nodiscard]] std::string annotate(std::string_view hlsl, std::span<const RegisterBinding> bindings);


struct CompileOptions {
    // Run the SPIR-V validator and fail on any message (the tests; the runtime trusts its sources).
    bool validate = false;
    // Keep the debug names (the tests read constant-buffer member names); the runtime strips them (Adreno).
    bool keep_names = false;
    // The programs' bound-table form (program.hlsli NEBULA_BOUND_TABLES, kBoundTableSlots maps), for devices without
    // descriptor indexing; else the bindless arrays.
    bool bound_tables = false;
};

// Changes with anything that changes compile_hlsl's output for the same source: the shaders this device built for
// mods are built again when it does (mod_shader_compiler_revision, the mods' cook signature).
inline constexpr std::uint32_t kCompilerRevision = 10;

// SPIR-V for `entry` of `hlsl`; empty with `error` set when it does not compile. `defines` are NAME=VALUE. Stage
// inputs and outputs sit at their semantics' locations, so stages and the vertex layout match as D3D12's do by name:
// vertex attributes POSITION 0, NORMAL 1, TANGENT 2, BINORMAL 3, COLOR0-1 4-5, BLENDINDICES0 6, POSITION1 and BEND 7,
// BLENDINDICES1 8, then TEXCOORDn 9+n; between stages COLOR0-1 0-1 and TEXCOORDn 2+n; pixel outputs SV_TARGETn n.
[[nodiscard]] std::vector<std::uint32_t> compile_hlsl(std::string_view hlsl, ShaderStage stage, const char* entry,
                                                      std::initializer_list<const char*> defines, std::string& error,
                                                      CompileOptions options = {});

}  // namespace galaxy::render::vk
