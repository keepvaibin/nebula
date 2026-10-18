// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// HLSL to SPIR-V through glslang's HLSL front end (docs/VULKAN.md). The generated GX shaders and the renderer's own
// shaders keep their D3D12 register declarations; annotate() adds the Vulkan binding each register maps to, so the
// D3D12 sources (and the hashes their caches use) stay as they are.

#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::gx::vk {

enum class ShaderStage { Vertex, Geometry, Fragment };

// One D3D12 register's Vulkan binding: `reg` as written in the source ("b0", "t0, space1", "s0"); a push-constant
// block when `push_constant` is set.
struct RegisterBinding {
    std::string_view reg;
    std::uint32_t set;
    std::uint32_t binding;
    bool push_constant = false;
};

// The GX draw layout (RendererVk): set 0 = b0 and b1 (dynamic uniform buffers) and t0 space1 (the matrix palette,
// a dynamic storage buffer); set 1 = t0-t7 (textures); set 2 = s0-s7 (samplers).
[[nodiscard]] std::span<const RegisterBinding> gx_draw_bindings() noexcept;

// Inserts the Vulkan attribute before every resource declared with a mapped register.
[[nodiscard]] std::string annotate(std::string_view hlsl, std::span<const RegisterBinding> bindings);

// SPIR-V for `entry` of `hlsl`; empty with `error` set when it does not compile. `defines` are NAME=VALUE.
// `validate` runs the SPIR-V validator and fails on any message (the tests; the runtime trusts its sources).
[[nodiscard]] std::vector<std::uint32_t> compile_hlsl(std::string_view hlsl, ShaderStage stage, const char* entry,
                                                      std::initializer_list<const char*> defines, std::string& error,
                                                      bool validate = false);

}  // namespace galaxy::gx::vk
