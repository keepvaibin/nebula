#pragma once

// An outside denoiser of the traced light (the master plan's section 17, work list 22.20). The built-in filters are
// the default and run unless the renderer runs a denoiser that takes the signals raw: today DLSS Ray Reconstruction
// through Streamline, which replaces the filters and the upscaler both while the player has chosen DLSS, the tracer
// runs and NVIDIA's plug-in is installed and supported. Then the tracer's filters pass this frame's signals through as
// traced (g_more.z bit 2048), the programs composite them as always, and the tracer hands the guides below to the
// renderer, which tags them for the denoiser with the composited picture, depth and motion.

#include <cstdint>

namespace galaxy::render::rt {

// The signals an outside denoiser takes raw (all of them for one that takes the composited picture).
enum Signal : std::uint32_t {
    kSignalSun = 1u,       // V
    kSignalLights = 2u,    // D
    kSignalIndirect = 4u,  // I
    kSignalReflect = 8u,   // R
    kSignalAll = 15u,
};

// This frame's guides, at the render size, as native textures in the shader-read state (D3D12: ID3D12Resource*;
// Vulkan: the backend's texture), written by rt_rr_guides.hlsl.
struct DenoiseGuides {
    void* diffuse_albedo = nullptr;    // RGBA8: the surface's colour (white at the sky)
    void* specular_albedo = nullptr;   // RGBA8: the split-sum environment term of what it mirrors
    void* normal_roughness = nullptr;  // RGBA16F: the world normal, w roughness
    void* hit_distance = nullptr;      // R16F: the reflection ray's length (0: none)
    float world_to_view[12] = {};      // the scene camera, rows of a 3x4 matrix (the eye looks down -z)
    bool valid = false;
};

// What the renderer's denoiser asks of the tracer.
class Denoiser {
public:
    virtual ~Denoiser() = default;
    // The signals to hand over raw this frame (0: the built-in filters run).
    [[nodiscard]] virtual std::uint32_t raw_signals() const noexcept = 0;
    // This frame's guides (after the tracer's passes, before the scene ends).
    virtual void guides(const DenoiseGuides& guides) noexcept = 0;
    // The guides it holds for this frame (not yet used), for the renderer to hand over with its other textures.
    [[nodiscard]] virtual DenoiseGuides pending_guides() const noexcept = 0;
    [[nodiscard]] virtual const char* name() const noexcept = 0;
};

}  // namespace galaxy::render::rt
