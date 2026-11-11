#pragma once

// Temporal upscaling of the D3D12 scene (AMD FidelityFX FSR 3.1).

#include "galaxy/windows_api.h"
#include <d3d12.h>

#include "galaxy/render/post_process_settings.h"

#include <array>
#include <cstdint>
#include <memory>

namespace galaxy::render {

// Scene inputs for one temporal upscale, recorded into `list` at the end of
// the game's 3D scene. Every resource is in its steady renderer state
// (color and output RENDER_TARGET, depth DEPTH_WRITE, motion RENDER_TARGET)
// and is left in it. Sizes are the regions used, from each texture's origin.
struct TemporalUpscaleInputs {
    ID3D12GraphicsCommandList* list = nullptr;
    ID3D12Resource* color = nullptr;   // RGBA8, gamma encoded, jittered
    ID3D12Resource* depth = nullptr;   // the scene depth (kSceneDepthResourceFormat), reversed (near = 1)
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
    // The textures are in the common state (Direct3D 12 with enhanced barriers, for an upscaler whose SDK records its own
    // legacy barriers: the two models meet only there); else as each field says.
    bool common = false;
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
    // Its SDK records its own (legacy) barriers on the textures it is handed (FSR 3, Streamline); false: the caller's
    // barriers put them where it reads them (XeSS).
    [[nodiscard]] virtual bool own_barriers() const noexcept { return false; }
};

// AMD FidelityFX FSR 3.1 (statically linked, MIT). Null when the runtime was
// built without the FidelityFX SDK or the device lacks shader model 6.2.
// The context is sized for the given maxima; callers re-create it when a
// render or output size exceeds them.
[[nodiscard]] std::unique_ptr<TemporalUpscaler> create_fsr3_upscaler(
    ID3D12Device* device,
    unsigned max_render_width, unsigned max_render_height,
    unsigned max_output_width, unsigned max_output_height);

// Intel XeSS Super Resolution (xess_upscaler.cpp, the library from xess_library.h). quality: the temporal_quality row. Null
// without the library or when the device refuses.
[[nodiscard]] std::unique_ptr<TemporalUpscaler> create_xess_upscaler(ID3D12Device* device, unsigned output_width,
                                                                     unsigned output_height, unsigned quality);

}  // namespace galaxy::render
