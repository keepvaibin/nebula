// The material pixel shader's source and build-time bytecode (material_shaders.h).
#include "galaxy/render/material_shaders.h"

namespace galaxy::render {

// Defined in the configure-time generated uber_shader_embed.cpp (NUL-terminated) and material_ps_dxbc.cpp
// (CMakeLists.txt: the fxc variants).
extern const char galaxy_material_ps_hlsl[];
struct MaterialPsBytecode {
    const unsigned char* data;
    std::size_t size;
};
extern const MaterialPsBytecode galaxy_material_ps_dxbc[kMaterialPsVariants];

std::string material_ps_defines(unsigned variant) {
    const MaterialPsVariant v = material_ps_variant_form(variant);
    std::string text;
    const auto define = [&text](const char* name, bool on) {
        text += "#define ";
        text += name;
        text += on ? " 1\n" : " 0\n";
    };
    define("MAT_PPL", v.ppl);
    define("MAT_ENH", v.enhanced);
    define("MAT_MOTION", v.motion);
    define("MAT_REACTIVE", v.reactive);
    define("MAT_LAYER", v.layer);
    define("MAT_DEPTH", v.depth);
    define("MAT_DUAL", v.dual);
    define("MAT_EARLY", v.early);
    return text;
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
    if (variant >= kMaterialPsVariants) {
        return {};
    }
    const MaterialPsBytecode& code = galaxy_material_ps_dxbc[variant];
    return {code.data, code.size};
}

}  // namespace galaxy::render
