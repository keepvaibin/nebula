#pragma once

// Temporal upscaling of the D3D12 scene (AMD FidelityFX FSR 3.1).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>

#include "galaxy/gx/post_process_settings.h"

#include <array>
#include <cstdint>
#include <memory>

namespace galaxy::gx {

// Scene inputs for one temporal upscale, recorded into `list` at the end of
// the guest's 3D scene. Every resource is in its steady renderer state
// (color and output RENDER_TARGET, depth DEPTH_WRITE, motion RENDER_TARGET)
// and is left in it. Sizes are the regions used, from each texture's origin.
struct TemporalUpscaleInputs {
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Resource* color = nullptr;   // RGBA8, gamma encoded, jittered
    ID3D12Resource* depth = nullptr;   // D24S8 typeless, reversed (near = 1)
    ID3D12Resource* motion = nullptr;  // RG16F, render pixels, current -> previous
    ID3D12Resource* reactive = nullptr;  // R8, blended draws' alpha (optional)
    ID3D12Resource* output = nullptr;  // RGBA8 with unordered access
    unsigned render_width = 0;
    unsigned render_height = 0;
    unsigned output_width = 0;
    unsigned output_height = 0;
    float jitter_x = 0.0f;  // render pixels, the offset applied to the projection
    float jitter_y = 0.0f;
    float camera_near = 1.0f;
    float camera_far = 100000.0f;
    float fov_y = 1.0f;     // radians
    float frame_ms = 16.667f;
    bool reset = false;
    // The scene camera (world to eye, rows 3x4; the eye looks down -z), when known.
    std::array<float, 12> view{};
    bool view_known = false;
};

class TemporalUpscaler {
public:
    virtual ~TemporalUpscaler() = default;
    // False when the dispatch could not be recorded (the caller falls back
    // to a spatial resample for this frame).
    virtual bool evaluate(const TemporalUpscaleInputs& inputs) = 0;
    // Jitter sequence length for a render-to-output width ratio.
    [[nodiscard]] virtual unsigned jitter_phase_count(
        unsigned render_width, unsigned output_width) const = 0;
    // Sub-pixel offset (render pixels, -0.5..0.5) for frame `index`.
    virtual void jitter_offset(unsigned index, unsigned phase_count,
                               float& x, float& y) const = 0;
    [[nodiscard]] virtual const char* name() const noexcept = 0;
};

// AMD FidelityFX FSR 3.1 (statically linked, MIT). Null when the runtime was
// built without the FidelityFX SDK or the device lacks shader model 6.2.
// The context is sized for the given maxima; callers re-create it when a
// render or output size exceeds them.
[[nodiscard]] std::unique_ptr<TemporalUpscaler> create_fsr3_upscaler(
    ID3D12Device* device,
    unsigned max_render_width, unsigned max_render_height,
    unsigned max_output_width, unsigned max_output_height);


}  // namespace galaxy::gx
