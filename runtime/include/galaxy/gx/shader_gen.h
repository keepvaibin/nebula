#pragma once

// ShaderGenerator: translates canonicalized shader keys into specialized
// HLSL source (vs_5_1 / ps_5_1).  Generated shaders are fully unrolled and
// constant-folded — the TEV program, texgen chain, and alpha test are baked
// into straight-line code — but bind the exact same constant buffer layouts
// (uber_constants.h) and root signature as the uber pipelines, so promotion
// from uber to specialized is binding-invisible.
//
// TEV fidelity rule: stage math runs in float but each stage result is
// quantized to the 8-bit lattice (floor(x * 255 + 0.5) / 255) before compare
// ops and the alpha test, making comparisons against ref/255 exact.
//
// No Windows/D3D12 includes — pure string generation, unit-testable offline.

#include "galaxy/gx/shader_keys.h"

#include <string>
#include <string_view>

namespace galaxy::gx {

class ShaderGenerator {
public:
    // Specialized vertex shader: position/normal transform through the
    // per-vertex matrix palette index, channel lighting per the key, and the
    // key's texgen chain.
    [[nodiscard]] std::string generate_vs(const VertexShaderKey& key) const;

    // Specialized pixel shader: unrolled TEV stages, alpha test, fog.
    [[nodiscard]] std::string generate_ps(const PixelShaderKey& key) const;

    // Fixed geometry shaders that turn GX lines/points into quads. D3D12's
    // native line and point rasterizers are fixed at one physical pixel and
    // cannot represent BP 0x22 or internal-resolution scaling.
    [[nodiscard]] static std::string_view line_geometry_shader_source() noexcept;
    [[nodiscard]] static std::string_view point_geometry_shader_source() noexcept;

    // Debug aid: stable human-readable description of a key (stage list,
    // ops, args) embedded as a comment header in generated source and used
    // in pipeline-cache log lines.
    [[nodiscard]] static std::string describe(const PixelShaderKey& key);
    [[nodiscard]] static std::string describe(const VertexShaderKey& key);
};

}  // namespace galaxy::gx
