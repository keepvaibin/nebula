// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// The tracer's compiled shaders (runtime/src/render/rt, tools/dev/compile_rt_shaders.py): DXIL in a Direct3D 12 build,
// SPIR-V in a Vulkan build made with a dxc that writes it. The player's machine compiles nothing.

#include <cstddef>

namespace galaxy::render {

// In the order of runtime/src/render/rt/shaders.tsv.
enum class RtShader : unsigned {
    Probe, View, Skin, Light, LightTime, LightSpace, LightsTime, LightsSpace, Surface, Sun, Lights, NoiseFill, SunTiles, SunTime,
    SunBlur, Reactive,
    Reference, ReferenceOut, Signal, Indirect, IndirectUp, Reflect, ReflectTime,
    ReflectSpace, ReflectOut, Volume, Scatter, RrGuides, Omm, Count
};

struct RtShaderBytes {
    const void* data = nullptr;
    std::size_t size = 0u;
};

// Empty when this build holds no shaders for its back end.
[[nodiscard]] RtShaderBytes rt_shader(RtShader shader) noexcept;
[[nodiscard]] bool rt_shaders_present() noexcept;

}  // namespace galaxy::render
