// Material shader tests: every build-time variant (material pixel and vertex shaders, the line/point geometry stages,
// the curved surfaces' hull and domain stages) is its source compiled as the runtime would compile it (the CMake
// define tables and material_shaders.cpp agree), the curve sort compiles, and keys pick the variants of their
// interfaces. Exit code: the number of failures.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#pragma warning(push, 0)
#include <Windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#pragma warning(pop)

#include "galaxy/render/material_shaders.h"
#include "galaxy/render/shader_keys.h"

#include <cstdio>
#include <cstring>
#include <span>
#include <string>

#pragma comment(lib, "d3dcompiler.lib")

using namespace galaxy::render;

namespace {

int g_failures = 0;

void fail(const char* what, unsigned index) {
    ++g_failures;
    std::printf("FAILED: %s %u\n", what, index);
}

// The source compiled as the build does (fxc /O1 /Ges /Zpr), compared with the build's bytecode.
void expect_built(const std::string& source, const char* target, const char* entry, std::span<const std::uint8_t> built,
                  const char* what, unsigned index) {
    Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
    const HRESULT hr = D3DCompile(source.data(), source.size(), what, nullptr, nullptr, entry, target,
                                  D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_PACK_MATRIX_ROW_MAJOR |
                                      D3DCOMPILE_OPTIMIZATION_LEVEL1,
                                  0, &code, &errors);
    if (FAILED(hr)) {
        std::printf("%s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "(no error blob)");
        fail(what, index);
        return;
    }
    if (built.size() != code->GetBufferSize() || std::memcmp(built.data(), code->GetBufferPointer(), built.size()) != 0) {
        std::printf("build-time %zu bytes, compiled %zu\n", built.size(), static_cast<std::size_t>(code->GetBufferSize()));
        fail(what, index);
    }
}

void expect_equal(unsigned actual, unsigned expected, const char* what) {
    if (actual != expected) {
        ++g_failures;
        std::printf("FAILED: %s: %u, expected %u\n", what, actual, expected);
    }
}

}  // namespace

int main() {
    for (unsigned v = 0; v < kMaterialPsVariants; ++v) {
        expect_built(material_ps_defines(v) + std::string(material_ps_source()), "ps_5_1", "main", material_ps_dxbc(v),
                     "material pixel variant", v);
    }
    for (unsigned v = 0; v < kMaterialVsVariants; ++v) {
        expect_built(material_vs_defines(v) + std::string(material_vs_source()), "vs_5_1", "main", material_vs_dxbc(v),
                     "material vertex variant", v);
    }
    expect_built(line_geometry_shader_source(), "gs_5_1", "main", line_geometry_dxbc(), "line geometry", 0u);
    expect_built(point_geometry_shader_source(), "gs_5_1", "main", point_geometry_dxbc(), "point geometry", 0u);
    for (unsigned v = 0; v < 4u; ++v) {
        expect_built(curved_stage_source(v, false), "hs_5_1", "main", curved_stage_dxbc(v, false), "curved hull", v);
        expect_built(curved_stage_source(v, true), "ds_5_1", "main", curved_stage_dxbc(v, true), "curved domain", v);
    }
    // The curve sort is compiled by the renderers: it must compile, entry by entry.
    for (const char* entry : {"classify", "offsets", "scatter"}) {
        const std::string sort = curve_sort_source();
        Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
        if (FAILED(D3DCompile(sort.data(), sort.size(), "curve_sort", nullptr, nullptr, entry, "cs_5_1",
                              D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_PACK_MATRIX_ROW_MAJOR, 0, &code, &errors))) {
            std::printf("%s\n", errors ? static_cast<const char*>(errors->GetBufferPointer()) : "(no error blob)");
            fail("curve sort", 0u);
        }
    }

    // Pixel variants: the outputs' precedence (motion, layer, reactive, depth export, dual source) and lighting.
    PixelShaderKey ps{};
    ps.num_tev_stages = 1u;
    expect_equal(material_ps_variant(ps), 1u, "pixel plain early");
    ps.flags = 0x01u;
    expect_equal(material_ps_variant(ps), 0u, "pixel plain late");
    ps.flags = 0x03u;
    expect_equal(material_ps_variant(ps), 4u, "pixel depth export");
    ps.flags = 0x83u;
    expect_equal(material_ps_variant(ps), 5u, "pixel depth export, dual source");
    ps.flags = 0x82u;
    expect_equal(material_ps_variant(ps), 3u, "pixel Z texture early, dual source");
    ps.flags = 0x00u;
    ps.reserved = kPsKeyMotion | kPsKeyReactive;
    expect_equal(material_ps_variant(ps), 7u, "pixel motion before reactive");
    ps.reserved = kPsKeyReactive | kPsKeyLayer | kPsKeyEnhancedLighting;
    expect_equal(material_ps_variant(ps), 2u * kMaterialPsOutputs + 13u, "pixel layer, enhanced");
    ps.reserved = kPsKeyPerPixelLighting | kPsKeyEnhancedLighting;
    ps.flags = 0x01u;
    expect_equal(material_ps_variant(ps), 3u * kMaterialPsOutputs, "pixel per-pixel and enhanced, late");
    for (unsigned variant = 0; variant < kUberPsVariants; ++variant) {
        const MaterialPsVariant form = material_ps_variant_form(uber_ps_material_variant(variant));
        if (form.dual != uber_ps_variant_dual_source(variant) || form.depth != uber_ps_variant_export_depth(variant) ||
            form.early != uber_ps_variant_early_depth(variant) || form.layer != uber_ps_variant_layer(variant) ||
            form.ppl || form.enhanced || form.motion) {
            fail("uber pixel variant", variant);
        }
    }

    // Vertex variants: outputs and blending; the stand-in has none of the outputs and blends.
    VertexShaderKey vs{};
    expect_equal(material_vs_variant(vs), 0u, "vertex plain");
    vs.flags = kVsKeyEnhancedLighting;
    expect_equal(material_vs_variant(vs), 1u, "vertex enhanced outputs");
    vs.flags = kVsKeyPerPixelLighting | kVsKeyMotion | kVsKeyCurved;
    vs.features = kVsFeatureVertexBlend | kVsFeatureDebake;
    expect_equal(material_vs_variant(vs), 15u, "vertex every output, blended");
    const MaterialVsVariant stand_in = material_vs_variant_form(kMaterialVsStandIn);
    if (stand_in.light_out || stand_in.motion || stand_in.curved || !stand_in.blend) {
        fail("vertex stand-in", kMaterialVsStandIn);
    }

    if (g_failures != 0) {
        std::printf("%d material shader check(s) FAILED\n", g_failures);
    } else {
        std::printf("all material shader checks passed\n");
    }
    return g_failures;
}
