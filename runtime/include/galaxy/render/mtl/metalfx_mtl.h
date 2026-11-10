// SPDX-License-Identifier: GPL-3.0-only
// MetalFX's spatial scaler for the presentation chain (mtl/post_process_mtl.h): the picture enlarged to its size on
// screen by the system's own upscaler, in place of FSR 1's EASU pass. On every device with MetalFX (macOS 13, iOS 16:
// Apple GPUs from the A9, and Intel Macs' GPUs). The temporal scaler is mtl/temporal_upscaler_mtl.h's.
//
// Plain C++ (mtl_context.h): the scaler itself stays behind this class.
#pragma once

#include "galaxy/render/mtl/mtl_context.h"

#include <cstdint>
#include <memory>

namespace galaxy::render::mtl {

class SpatialScaler {
public:
    virtual ~SpatialScaler() = default;
    // The sizes and formats it was made for.
    [[nodiscard]] virtual bool matches(unsigned input_width, unsigned input_height, unsigned output_width, unsigned output_height,
                                       Format color_format, Format output_format) const noexcept = 0;
    // What the output texture must be made for (TextureUse bits); it must also be Storage::Private.
    [[nodiscard]] virtual std::uint32_t output_use() const noexcept = 0;
    // `color` (display values, 0 to 1) enlarged into `output`, recorded with no encoder open on `commands` (an
    // id<MTLCommandBuffer>).
    virtual void encode(Object commands, const Texture& color, const Texture& output) = 0;
};

// Null where the device has no spatial scaler (DeviceCaps::metalfx_spatial) or it refuses the sizes or formats.
[[nodiscard]] std::unique_ptr<SpatialScaler> create_spatial_scaler(unsigned input_width, unsigned input_height,
                                                                   unsigned output_width, unsigned output_height,
                                                                   Format color_format, Format output_format);

}  // namespace galaxy::render::mtl
