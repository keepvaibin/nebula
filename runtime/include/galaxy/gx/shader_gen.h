#pragma once

// Generates specialized HLSL (vs_5_1 / ps_5_1) from canonicalized shader keys.
// TEV stages, texgens and the alpha test are unrolled into straight-line code,
// but the shaders use the same constant buffer layouts (uber_constants.h) and
// root signature as the uber pipelines, so promotion needs no rebinding.
// TEV precision: stage math runs in float, and each stage result is quantized
// to the 8-bit lattice (floor(x * 255 + 0.5) / 255) before compare ops and the
// alpha test, so comparisons against ref/255 are exact.
// No Windows/D3D12 dependencies.

#include "galaxy/gx/shader_keys.h"

#include <string>
#include <string_view>

namespace galaxy::gx {

class ShaderGenerator {
public:
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

    // Stable human-readable key description, embedded as a comment in generated
    // source and used in pipeline-cache log lines.
    [[nodiscard]] static std::string describe(const PixelShaderKey& key);
    [[nodiscard]] static std::string describe(const VertexShaderKey& key);
};

}  // namespace galaxy::gx
