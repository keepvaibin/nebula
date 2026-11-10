// SPDX-License-Identifier: GPL-3.0-only
// Temporal upscaling of the Metal scene: MetalFX's temporal scaler (Apple GPUs from the A14 and M1) and Snapdragon
// Game Super Resolution 2 (two fragment passes on any Metal device). As vk/temporal_upscaler_vk.h for Vulkan.
#pragma once

#include "galaxy/render/mtl/mtl_context.h"

#include <array>
#include <cstdint>
#include <memory>

namespace galaxy::render::mtl {

// Scene inputs for one temporal upscale, recorded with no encoder open on `commands`. Sizes are the regions used, from
// each texture's origin.
struct TemporalUpscaleInputs {
    Object commands = nullptr;        // id<MTLCommandBuffer>
    const Texture* color = nullptr;     // display values, jittered
    const Texture* depth = nullptr;     // reversed (near = 1)
    const Texture* motion = nullptr;    // RG16F, render pixels, current -> previous
    const Texture* reactive = nullptr;  // R8, blended draws' alpha
    const Texture* output = nullptr;    // the scene's format, written by the scaler
    unsigned render_width = 0u;
    unsigned render_height = 0u;
    unsigned output_width = 0u;
    unsigned output_height = 0u;
    float jitter_x = 0.0f;  // render pixels, the offset applied to the projection
    float jitter_y = 0.0f;
    float camera_near = 1.0f;
    float camera_far = 100000.0f;
    float fov_y = 1.0f;  // radians
    float frame_ms = 0.0f;
    bool reset = false;
    // The scene camera (world to eye, rows 3x4; the eye looks down -z), when known.
    std::array<float, 12> view{};
    bool view_known = false;
};

class TemporalUpscaler {
public:
    virtual ~TemporalUpscaler() = default;
    // False when the pass could not be recorded (the caller falls back to a resample for this frame).
    virtual bool evaluate(const TemporalUpscaleInputs& inputs) = 0;
    [[nodiscard]] virtual unsigned jitter_phase_count(unsigned render_width, unsigned output_width) const = 0;
    // Sub-pixel offset (render pixels, -0.5..0.5) for frame `index`.
    virtual void jitter_offset(unsigned index, unsigned phase_count, float& x, float& y) const = 0;
    [[nodiscard]] virtual const char* name() const noexcept = 0;
    // The output is written by a compute or MetalFX pass (the output texture needs kUseWrite), not drawn.
    [[nodiscard]] virtual bool writes_output() const noexcept = 0;
};

// Snapdragon Game Super Resolution 2 (sgsr2_mtl.mm). Null when a pipeline or texture cannot be made.
[[nodiscard]] std::unique_ptr<TemporalUpscaler> create_sgsr2_upscaler(unsigned render_width, unsigned render_height,
                                                                      unsigned output_width, unsigned output_height,
                                                                      Format output_format);
// MetalFX's temporal scaler (metalfx_mtl.mm). Null where the device has none (DeviceCaps::metalfx_temporal) or it
// refuses the sizes.
[[nodiscard]] std::unique_ptr<TemporalUpscaler> create_metalfx_upscaler(unsigned render_width, unsigned render_height,
                                                                        unsigned output_width, unsigned output_height,
                                                                        Format color_format, Format output_format);

}  // namespace galaxy::render::mtl
