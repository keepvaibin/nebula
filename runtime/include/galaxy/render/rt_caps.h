// SPDX-License-Identifier: GPL-3.0-only
#pragma once

// What the device reports for ray tracing, the same record on every back end. Ray tracing runs from what the device
// says it does, never from which device it is.

namespace galaxy::render {

struct RtCaps {
    bool ray_queries = false;              // inline ray queries: DXR 1.1, VK_KHR_ray_query
    bool acceleration_structures = false;  // ID3D12Device5, VK_KHR_acceleration_structure with device addresses
    bool bindless = false;                 // a compute shader reaches every texture by number
    bool shaders = false;                  // this build holds the tracer's shaders for this back end
    bool micromaps = false;                // opacity micromaps (DXR 1.2, VK_EXT_opacity_micromap): optional
    bool unified_memory = false;           // the GPU shares the system's memory: told, never a reason to refuse
    bool enabled = false;                  // the device was made with what the above needs
    bool triangle_ids = true;              // the prepass's pixel stage reads its triangle's number (Vulkan: geometry
                                           // shaders); else the surface pass finds it by a ray (D11)
};

// Why a device cannot trace; null when it can.
[[nodiscard]] constexpr const char* rt_refusal(const RtCaps& caps) noexcept {
    return !caps.ray_queries               ? "the GPU has no inline ray queries"
           : !caps.acceleration_structures ? "the GPU builds no acceleration structures"
           : !caps.bindless                ? "the GPU cannot index every texture from one shader"
           : !caps.shaders                 ? "this build has no ray tracing shaders for this graphics API"
           : !caps.enabled                 ? "the device was made without ray tracing"
                                           : nullptr;
}

[[nodiscard]] constexpr bool rt_capable(const RtCaps& caps) noexcept { return rt_refusal(caps) == nullptr; }

// The RAY TRACING row is shown to players only once the tracer has an effect to show (the shadows' step sets this).
// Until then the setting is reached from a settings file alone, and a player's build clamps it off (render_config.cpp).
inline constexpr bool kRayTracingRowOffered = false;

// The renderer's record of its device, set once the device exists (render thread) and read from any thread.
void set_rt_caps(const RtCaps& caps) noexcept;
[[nodiscard]] RtCaps rt_caps() noexcept;
// One line for the log: what the device reports and what follows from it. `wanted`: the setting is on.
void log_rt_caps(const char* api, const RtCaps& caps, bool wanted);

}  // namespace galaxy::render
