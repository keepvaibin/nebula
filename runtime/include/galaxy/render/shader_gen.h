#pragma once

// Generates specialized HLSL (vs_5_1) from canonicalized vertex shader keys, and the fixed line/point geometry,
// curved-surface and curve-sort stages. The pixel stage is the material shader (material_shaders.h).

#include "galaxy/render/shader_keys.h"

#include <string>
#include <string_view>

namespace galaxy::render {

// Forms of the generated shaders for drivers that compile the default form wrongly (vk/device_quirks_vk.h). The pixel
// stage no longer comes from here; the fields stay for the Vulkan fallback ladder's records.
struct ShaderGenOptions {
    bool scalar_byte_masks = false;
    bool tev_zero_from_constant = false;
};

class ShaderGenerator {
public:
    void set_options(const ShaderGenOptions& options) noexcept { options_ = options; }

    // Transforms position/normal through the per-vertex matrix index and applies
    // the key's channel lighting and texgens.
    [[nodiscard]] std::string generate_vs(const VertexShaderKey& key) const;

    // Fixed geometry shaders that expand GX lines/points into quads. D3D12 line
    // and point rasterization is fixed at one physical pixel and cannot represent
    // BP 0x22 widths or internal-resolution scaling.
    [[nodiscard]] static std::string_view line_geometry_shader_source() noexcept;
    [[nodiscard]] static std::string_view point_geometry_shader_source() noexcept;

    // Curved surfaces' hull shader, or with `domain` their domain shader (entry main), Phong tessellation (Boubekeur
    // and Alexa 2008), for a vertex output variant (curved_variant: kCurvedPerPixel, kCurvedMotion in shader_keys.h).
    [[nodiscard]] static std::string curved_stage_source(unsigned variant, bool domain);
    // Curved surfaces' triangles sorted before their draws (compute, entries classify, offsets, scatter): those the
    // hull shader would leave whole to a triangle list, the rest to patches (RendererD3D12::sort_curved).
    [[nodiscard]] static std::string curve_sort_source();

    // Stable human-readable key description, embedded as a comment in generated
    // source and used in pipeline-cache log lines.
    [[nodiscard]] static std::string describe(const PixelShaderKey& key);
    [[nodiscard]] static std::string describe(const VertexShaderKey& key);

private:
    ShaderGenOptions options_;
};

}  // namespace galaxy::render
