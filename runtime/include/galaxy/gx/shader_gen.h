#pragma once

// Generates specialized HLSL (vs_5_1 / ps_5_1) from canonicalized shader keys. TEV stages, texgens
// and the alpha test are unrolled into straight-line code. The shaders use the uber pipelines'
// constant buffer layouts (uber_constants.h) and root signature, so promotion needs no rebinding.
// TEV math runs in float. Each stage result is quantized to the 8-bit lattice
// (floor(x * 255 + 0.5) / 255) before compare ops and the alpha test, so comparisons against
// ref/255 are exact.

#include "galaxy/gx/shader_keys.h"

#include <string>
#include <string_view>

namespace galaxy::gx {

// Forms of the pixel shaders for drivers that compile the default form wrongly (vk/device_quirks_vk.h). The defaults
// are the D3D12 shaders, unchanged.
struct ShaderGenOptions {
    // TEV byte masks (x & 255) one component at a time, never one vector AND (Mali returns the constant vector).
    bool scalar_byte_masks = false;
    // A zero TEV colour input A or B read from the constants (uber_ps_header.w, always zero) instead of a literal
    // (Qualcomm folds the literal wrongly).
    bool tev_zero_from_constant = false;
};

class ShaderGenerator {
public:
    void set_options(const ShaderGenOptions& options) noexcept { options_ = options; }

    // Transforms position/normal through the per-vertex matrix index and applies
    // the key's channel lighting and texgens.
    [[nodiscard]] std::string generate_vs(const VertexShaderKey& key) const;

    // Unrolled TEV stages, alpha test and fog.
    [[nodiscard]] std::string generate_ps(const PixelShaderKey& key) const;

    // Fixed geometry shaders that expand GX lines/points into quads. D3D12 line
    // and point rasterization is fixed at one physical pixel and cannot represent
    // BP 0x22 widths or internal-resolution scaling.
    [[nodiscard]] static std::string_view line_geometry_shader_source() noexcept;
    [[nodiscard]] static std::string_view point_geometry_shader_source() noexcept;

    // Curved surfaces' hull shader, or with `domain` their domain shader (entry main), Phong tessellation (Boubekeur
    // and Alexa 2008), for a vertex output variant (curved_variant: kCurvedPerPixel, kCurvedMotion in shader_keys.h).
    [[nodiscard]] static std::string curved_stage_source(unsigned variant, bool domain);

    // Stable human-readable key description, embedded as a comment in generated
    // source and used in pipeline-cache log lines.
    [[nodiscard]] static std::string describe(const PixelShaderKey& key);
    [[nodiscard]] static std::string describe(const VertexShaderKey& key);

private:
    ShaderGenOptions options_;
};

}  // namespace galaxy::gx
