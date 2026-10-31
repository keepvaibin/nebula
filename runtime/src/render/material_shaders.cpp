// The material shaders' sources and build-time bytecode (material_shaders.h).
#include "galaxy/render/material_shaders.h"

#include "galaxy/render/render_vertex.h"

#include <cstddef>
#include <cstdio>

namespace galaxy::render {

// The sources (configure-time generated uber_shader_embed.cpp, NUL-terminated) and the variants' DXBC
// (material_ps_dxbc.cpp, CMakeLists.txt: the fxc variants).
extern const char galaxy_material_ps_hlsl[];
extern const char galaxy_material_vs_hlsl[];
extern const char galaxy_material_curve_hlsl[];
extern const char galaxy_material_lines_hlsl[];
extern const char galaxy_curve_sort_hlsl[];
struct MaterialPsBytecode {
    const unsigned char* data;
    std::size_t size;
};
extern const MaterialPsBytecode galaxy_material_ps_dxbc[kMaterialPsVariants];
extern const MaterialPsBytecode galaxy_material_vs_dxbc[kMaterialVsVariants];
extern const MaterialPsBytecode galaxy_material_curve_dxbc[8];
extern const MaterialPsBytecode galaxy_material_lines_dxbc[2];

namespace {

std::string define_text(std::initializer_list<std::pair<const char*, bool>> defines) {
    std::string text;
    for (const auto& [name, on] : defines) {
        text += "#define ";
        text += name;
        text += on ? " 1\n" : " 0\n";
    }
    return text;
}

std::span<const std::uint8_t> bytes(const MaterialPsBytecode& code) noexcept {
    return {code.data, code.size};
}

}  // namespace

std::string material_ps_defines(unsigned variant) {
    const MaterialPsVariant v = material_ps_variant_form(variant);
    return define_text({{"MAT_PPL", v.ppl}, {"MAT_ENH", v.enhanced}, {"MAT_MOTION", v.motion},
                        {"MAT_REACTIVE", v.reactive}, {"MAT_LAYER", v.layer}, {"MAT_DEPTH", v.depth},
                        {"MAT_DUAL", v.dual}, {"MAT_EARLY", v.early}});
}

std::string_view material_ps_source() noexcept {
    return std::string_view(galaxy_material_ps_hlsl);
}

std::string material_ps_hlsl(const PixelShaderKey& key) {
    std::string text = material_ps_defines(material_ps_variant(key));
    text += material_ps_source();
    return text;
}

std::span<const std::uint8_t> material_ps_dxbc(unsigned variant) noexcept {
    return variant < kMaterialPsVariants ? bytes(galaxy_material_ps_dxbc[variant]) : std::span<const std::uint8_t>{};
}

std::string material_vs_defines(unsigned variant) {
    const MaterialVsVariant v = material_vs_variant_form(variant);
    return define_text({{"MAT_LIGHT_OUT", v.light_out}, {"MAT_MOTION", v.motion}, {"MAT_CURVED", v.curved},
                        {"MAT_BLEND", v.blend}});
}

std::string_view material_vs_source() noexcept {
    return std::string_view(galaxy_material_vs_hlsl);
}

std::string material_vs_hlsl(const VertexShaderKey& key) {
    std::string text = material_vs_defines(material_vs_variant(key));
    text += material_vs_source();
    return text;
}

std::span<const std::uint8_t> material_vs_dxbc(unsigned variant) noexcept {
    return variant < kMaterialVsVariants ? bytes(galaxy_material_vs_dxbc[variant]) : std::span<const std::uint8_t>{};
}

std::string line_geometry_shader_source() {
    return define_text({{"MAT_POINTS", false}}) + galaxy_material_lines_hlsl;
}

std::string point_geometry_shader_source() {
    return define_text({{"MAT_POINTS", true}}) + galaxy_material_lines_hlsl;
}

std::string curved_stage_source(unsigned variant, bool domain) {
    return define_text({{"MAT_DOMAIN", domain}, {"MAT_LIGHT_OUT", (variant & 1u) != 0u},
                        {"MAT_MOTION", (variant & 2u) != 0u}}) +
           galaxy_material_curve_hlsl;
}

std::string curve_sort_source() {
    char defines[256];
    std::snprintf(defines, sizeof(defines),
                  "#define VERTEX_STRIDE %zuu\n#define VERTEX_MTX %zuu\n#define VERTEX_CURVE %zuu\n"
                  "#define CB_POS_BASE %zuu\n#define CB_ROW_BASES %zuu\n#define CB_INLINE_POS %zuu\n"
                  "#define CB_RASTER %zuu\n",
                  sizeof(RenderVertex), offsetof(RenderVertex, mtx_indices), offsetof(RenderVertex, curve),
                  offsetof(VertexConstants, pos_base), offsetof(VertexConstants, row_bases),
                  offsetof(VertexConstants, inline_pos_matrix), offsetof(VertexConstants, line_point_raster));
    return std::string(defines) + galaxy_curve_sort_hlsl;
}

std::span<const std::uint8_t> line_geometry_dxbc() noexcept {
    return bytes(galaxy_material_lines_dxbc[0]);
}

std::span<const std::uint8_t> point_geometry_dxbc() noexcept {
    return bytes(galaxy_material_lines_dxbc[1]);
}

std::span<const std::uint8_t> curved_stage_dxbc(unsigned variant, bool domain) noexcept {
    return bytes(galaxy_material_curve_dxbc[(variant & 3u) * 2u + (domain ? 1u : 0u)]);
}

std::string describe_key(const PixelShaderKey& key) {
    char buf[160];
    std::snprintf(buf, sizeof(buf), "PS:stages=%u coords=%u chans=%u ind=%u fog=%u alpha=%u/%u/%u flags=0x%02x variant=%u",
                  static_cast<unsigned>(key.num_tev_stages), static_cast<unsigned>(key.num_texgens),
                  static_cast<unsigned>(key.num_chans), static_cast<unsigned>(key.num_ind_stages),
                  static_cast<unsigned>(key.fog_type), static_cast<unsigned>(key.alpha_comp0),
                  static_cast<unsigned>(key.alpha_comp1), static_cast<unsigned>(key.alpha_logic),
                  static_cast<unsigned>(key.flags), material_ps_variant(key));
    return buf;
}

std::string describe_key(const VertexShaderKey& key) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "VS:coords=%u chans=%u flags=0x%02x slots=0x%02x features=0x%x variant=%u",
                  static_cast<unsigned>(key.num_texgens), static_cast<unsigned>(key.num_chans),
                  static_cast<unsigned>(key.flags), static_cast<unsigned>(key.texmtx_idx_mask),
                  static_cast<unsigned>(key.features), material_vs_variant(key));
    return buf;
}

}  // namespace galaxy::render
