// Lock-free PSO completion queue (Vyukov MPSC) and PipelineCache with background shader specialization.
//
// get(), drain_completions() and try_pop() run on the render thread only; push() on any compile worker.
// published_ and in_flight_ are render-thread-only and need no locks. The job intake (jobs_, job_mutex_,
// job_cv_) uses a mutex and condition variable; the path back to the render thread stays lock-free.

#include "galaxy/gx/pipeline_cache.h"

#include "galaxy/gx/render_config.h"
#include "galaxy/gx/renderer_d3d12.h"
#include "galaxy/gx/shader_gen.h"
#include "galaxy/gx/uber_shaders.h"
#include "galaxy/gx/shader_keys.h"
#include "galaxy/gx/shader_pair_compile.h"
#include "galaxy/scope_exit.h"

#pragma warning(push, 0)
#include <d3dcompiler.h>
#include <fstream>
#include <d3d12shader.h>
#include <Windows.h>
#include "galaxy/windows_power.h"
#pragma warning(pop)

#include <atomic>
#include <array>
#include <limits>
#include <new>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <iostream>
#include <share.h>
#include <stdexcept>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <type_traits>

#pragma comment(lib, "d3dcompiler.lib")

namespace galaxy::gx {

#if defined(GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO)
using PipelineTestCreator = Microsoft::WRL::ComPtr<ID3D12PipelineState> (*)(const PsoKey&);
static PipelineTestCreator pipeline_test_creator = nullptr;
void set_pipeline_cache_test_creator(PipelineTestCreator creator) {
    pipeline_test_creator = creator;
}
static thread_local void (*completion_wait_test_hook)() = nullptr;
static thread_local unsigned completion_test_parks = 0u;
void set_pipeline_cache_completion_wait_hook(void (*hook)()) {
    completion_wait_test_hook = hook;
    completion_test_parks = 0u;
}
unsigned pipeline_cache_completion_test_parks() { return completion_test_parks; }
#endif

namespace {
#if defined(GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO)
void persistence_test_load_publication(unsigned stage);
#endif

// Prewarm at most this many cached PSO keys at startup, and wait at most this long for them.
constexpr std::uint64_t kPsoPrewarmLimit = 4096u;
constexpr std::uint64_t kPsoPrewarmTimeoutMs = 30000u;
// How long first use waits for a pipeline a worker is already building.
constexpr std::uint64_t kInFlightPsoTimeoutMs = 30000u;

// GALAXY_TEST_SHADER_KEY_CAPTURE=<file>: appends each shader key the session resolves, once ('V' or 'P', then the
// key's bytes), for galaxy_vk_shader_tests to compile through the Vulkan path. Render thread only.
std::FILE* shader_key_capture_file() {
    static std::FILE* const file = []() -> std::FILE* {
        wchar_t path[1024]{};
        std::size_t length = 0;
        if (_wgetenv_s(&length, path, std::size(path), L"GALAXY_TEST_SHADER_KEY_CAPTURE") != 0 || length <= 1) {
            return nullptr;
        }
        return _wfsopen(path, L"ab", _SH_DENYWR);
    }();
    return file;
}

void capture_shader_keys(const PixelShaderKey& ps_key, const VertexShaderKey& vs_key) {
    static std::unordered_set<std::uint64_t> vs_seen;
    static std::unordered_set<std::uint64_t> ps_seen;
    std::FILE* const file = shader_key_capture_file();
    if (vs_seen.insert(vs_key.hash()).second) {
        std::fputc('V', file);
        std::fwrite(&vs_key, sizeof(vs_key), 1, file);
    }
    if (ps_seen.insert(ps_key.hash()).second) {
        std::fputc('P', file);
        std::fwrite(&ps_key, sizeof(ps_key), 1, file);
    }
    std::fflush(file);
}

std::uint64_t elapsed_us(
    std::chrono::steady_clock::time_point start,
    std::chrono::steady_clock::time_point end) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            end - start).count());
}

// Input layout for the fixed 144-byte GxVertexOut stream (vertex_loader.h).
constexpr D3D12_INPUT_ELEMENT_DESC kGxInputLayout[] = {
    {"POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TANGENT",      0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"BINORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"COLOR",        0, DXGI_FORMAT_R32_UINT,        0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"COLOR",        1, DXGI_FORMAT_R32_UINT,        0, 52, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,    0, 56, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD",     1, DXGI_FORMAT_R32G32_FLOAT,    0, 64, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD",     2, DXGI_FORMAT_R32G32_FLOAT,    0, 72, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD",     3, DXGI_FORMAT_R32G32_FLOAT,    0, 80, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD",     4, DXGI_FORMAT_R32G32_FLOAT,    0, 88, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD",     5, DXGI_FORMAT_R32G32_FLOAT,    0, 96, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD",     6, DXGI_FORMAT_R32G32_FLOAT,    0, 104, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"TEXCOORD",     7, DXGI_FORMAT_R32G32_FLOAT,    0, 112, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32_UINT, 0, 120, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    {"BEND",         0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 132, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
};

// GX BlendFactor -> D3D12, src side (GX factor 2/3 reference the destination).
D3D12_BLEND gx_src_blend(std::uint32_t f) {
    switch (f) {
    case 0: return D3D12_BLEND_ZERO;
    case 1: return D3D12_BLEND_ONE;
    case 2: return D3D12_BLEND_DEST_COLOR;
    case 3: return D3D12_BLEND_INV_DEST_COLOR;
    case 4: return D3D12_BLEND_SRC_ALPHA;
    case 5: return D3D12_BLEND_INV_SRC_ALPHA;
    case 6: return D3D12_BLEND_DEST_ALPHA;
    case 7: return D3D12_BLEND_INV_DEST_ALPHA;
    default: return D3D12_BLEND_ONE;
    }
}

// GX BlendFactor -> D3D12, dst side (GX factor 2/3 reference the source).
D3D12_BLEND gx_dst_blend(std::uint32_t f) {
    switch (f) {
    case 0: return D3D12_BLEND_ZERO;
    case 1: return D3D12_BLEND_ONE;
    case 2: return D3D12_BLEND_SRC_COLOR;
    case 3: return D3D12_BLEND_INV_SRC_COLOR;
    case 4: return D3D12_BLEND_SRC_ALPHA;
    case 5: return D3D12_BLEND_INV_SRC_ALPHA;
    case 6: return D3D12_BLEND_DEST_ALPHA;
    case 7: return D3D12_BLEND_INV_DEST_ALPHA;
    default: return D3D12_BLEND_ZERO;
    }
}

// Color-only blend factors are invalid for the alpha channel; demote them.
D3D12_BLEND to_alpha_blend(D3D12_BLEND b) {
    switch (b) {
    case D3D12_BLEND_SRC_COLOR:      return D3D12_BLEND_SRC_ALPHA;
    case D3D12_BLEND_INV_SRC_COLOR:  return D3D12_BLEND_INV_SRC_ALPHA;
    case D3D12_BLEND_DEST_COLOR:     return D3D12_BLEND_DEST_ALPHA;
    case D3D12_BLEND_INV_DEST_COLOR: return D3D12_BLEND_INV_DEST_ALPHA;
    default:                         return b;
    }
}

// GX LogicOp 0-15 -> D3D12.
D3D12_LOGIC_OP gx_logic_op(std::uint32_t m) {
    switch (m) {
    case 0:  return D3D12_LOGIC_OP_CLEAR;
    case 1:  return D3D12_LOGIC_OP_AND;
    case 2:  return D3D12_LOGIC_OP_AND_REVERSE;
    case 3:  return D3D12_LOGIC_OP_COPY;
    case 4:  return D3D12_LOGIC_OP_AND_INVERTED;
    case 5:  return D3D12_LOGIC_OP_NOOP;
    case 6:  return D3D12_LOGIC_OP_XOR;
    case 7:  return D3D12_LOGIC_OP_OR;
    case 8:  return D3D12_LOGIC_OP_NOR;
    case 9:  return D3D12_LOGIC_OP_EQUIV;
    case 10: return D3D12_LOGIC_OP_INVERT;
    case 11: return D3D12_LOGIC_OP_OR_REVERSE;
    case 12: return D3D12_LOGIC_OP_COPY_INVERTED;
    case 13: return D3D12_LOGIC_OP_OR_INVERTED;
    case 14: return D3D12_LOGIC_OP_NAND;
    case 15: return D3D12_LOGIC_OP_SET;
    default: return D3D12_LOGIC_OP_NOOP;
    }
}

// Compile one HLSL source; logs the compiler error (capped) on failure.
// `fast` skips FXC optimization: the first-use path publishes that pipeline
// at once and a worker replaces it with the level-1 build (see get()).
Microsoft::WRL::ComPtr<ID3DBlob> compile_shader_with_level(
    const std::string& src,
    const char* target,
    const char* label,
    bool fast) {
    Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
    // Level 1: the driver re-optimizes DXBC when it builds the PSO, so FXC's
    // level 3 would only lengthen the synchronous first-use compile.
    const UINT flags = D3DCOMPILE_ENABLE_STRICTNESS |
        D3DCOMPILE_PACK_MATRIX_ROW_MAJOR |
        (fast ? D3DCOMPILE_SKIP_OPTIMIZATION : D3DCOMPILE_OPTIMIZATION_LEVEL1);
    const HRESULT hr = D3DCompile(
        src.data(), src.size(), label, nullptr, nullptr, "main", target,
        flags, 0, &code, &errors);
    if (FAILED(hr)) {
        static std::atomic<int> s_err_logs{0};
        if (s_err_logs.fetch_add(1) < 4) {
            std::cerr << "[PipelineCache] D3DCompile(" << target << ") failed for "
                      << label << ":\n"
                      << (errors ? static_cast<const char*>(
                                       errors->GetBufferPointer())
                                 : "(no error blob)")
                      << '\n';
        }
        return nullptr;
    }
    return code;
}

Microsoft::WRL::ComPtr<ID3DBlob> compile_shader(
    const std::string& src, const char* target, const char* label) {
    return compile_shader_with_level(src, target, label, false);
}

Microsoft::WRL::ComPtr<ID3DBlob> compile_shader_fast(
    const std::string& src, const char* target, const char* label) {
    return compile_shader_with_level(src, target, label, true);
}

// Translate a RenderStateKey + compiled shaders into a full PSO desc.
D3D12_GRAPHICS_PIPELINE_STATE_DESC make_gx_pso_desc_impl(
    ID3D12RootSignature* root_signature,
    const RenderStateKey& rs,
    ID3DBlob* vs,
    ID3DBlob* ps,
    ID3DBlob* line_geometry_shader,
    ID3DBlob* point_geometry_shader,
    ID3DBlob* hull_shader,
    ID3DBlob* domain_shader) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = root_signature;
    pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pd.InputLayout = {kGxInputLayout,
                      static_cast<UINT>(std::size(kGxInputLayout))};

    D3D12_RENDER_TARGET_BLEND_DESC& rt = pd.BlendState.RenderTarget[0];
    const std::uint32_t bb = rs.blend_bits;
    // GX EFB write masks (key bits 25/26 = cmode0 color_update/alpha_update).
    // Z-clear / z-prepass draws disable color update while writing depth.
    rt.RenderTargetWriteMask = static_cast<UINT8>(
        ((bb & (1u << 25)) ? (D3D12_COLOR_WRITE_ENABLE_RED |
                              D3D12_COLOR_WRITE_ENABLE_GREEN |
                              D3D12_COLOR_WRITE_ENABLE_BLUE) : 0) |
        ((bb & (1u << 26)) ? static_cast<int>(D3D12_COLOR_WRITE_ENABLE_ALPHA) : 0));
    if (bb & 1u) {                       // blend_enable
        rt.BlendEnable = TRUE;
        const bool subtract = (bb & 2u) != 0;
        if (subtract) {
            // GX subtract: dst = dst - src, factors forced to one.
            rt.SrcBlend  = D3D12_BLEND_ONE;
            rt.DestBlend = D3D12_BLEND_ONE;
            rt.BlendOp   = D3D12_BLEND_OP_REV_SUBTRACT;
        } else {
            rt.SrcBlend  = gx_src_blend((bb >> 2) & 0x7u);
            rt.DestBlend = gx_dst_blend((bb >> 5) & 0x7u);
            rt.BlendOp   = D3D12_BLEND_OP_ADD;
            if (rs.pixfmt != 1u) {
                if (rt.SrcBlend == D3D12_BLEND_DEST_ALPHA)
                    rt.SrcBlend = D3D12_BLEND_ONE;
                else if (rt.SrcBlend == D3D12_BLEND_INV_DEST_ALPHA)
                    rt.SrcBlend = D3D12_BLEND_ZERO;
                if (rt.DestBlend == D3D12_BLEND_DEST_ALPHA)
                    rt.DestBlend = D3D12_BLEND_ONE;
                else if (rt.DestBlend == D3D12_BLEND_INV_DEST_ALPHA)
                    rt.DestBlend = D3D12_BLEND_ZERO;
            }
        }
        rt.SrcBlendAlpha  = to_alpha_blend(rt.SrcBlend);
        rt.DestBlendAlpha = to_alpha_blend(rt.DestBlend);
        rt.BlendOpAlpha   = rt.BlendOp;
        if (bb & (1u << 27)) {           // dual-source dst-alpha
            // The PS exports the TEV alpha on SV_Target1 (the matching
            // PixelShaderKey flags bit7 is derived from the same register
            // snapshot): RGB blends against SRC1_ALPHA while the alpha
            // channel keeps its independently selected alpha equation.
            rt.SrcBlend = (rt.SrcBlend == D3D12_BLEND_SRC_ALPHA)
                ? D3D12_BLEND_SRC1_ALPHA
                : (rt.SrcBlend == D3D12_BLEND_INV_SRC_ALPHA)
                    ? D3D12_BLEND_INV_SRC1_ALPHA
                    : rt.SrcBlend;
            rt.DestBlend = (rt.DestBlend == D3D12_BLEND_SRC_ALPHA)
                ? D3D12_BLEND_SRC1_ALPHA
                : (rt.DestBlend == D3D12_BLEND_INV_SRC_ALPHA)
                    ? D3D12_BLEND_INV_SRC1_ALPHA
                    : rt.DestBlend;
        }
        if (bb & (1u << 28)) { // Constant destination alpha is independent.
            rt.SrcBlendAlpha = D3D12_BLEND_ONE;
            rt.DestBlendAlpha = D3D12_BLEND_ZERO;
            rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        }
    } else if (bb & (1u << 8)) {         // logic_op_enable
        rt.LogicOpEnable = TRUE;
        rt.LogicOp = gx_logic_op((bb >> 9) & 0xFu);
    }

    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    // GX screen space is Y-down relative to D3D NDC (the EFB viewport does not flip), so a GX
    // front face lands as a D3D back face and vice versa, as in Dolphin's D3D backends.
    // CullMode::All (rasterize nothing) has no PSO equivalent and maps to None.
    switch (static_cast<CullMode>(rs.cull)) {
    case CullMode::Front:
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
        break;
    case CullMode::Back:
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
        break;
    case CullMode::None:
    case CullMode::All:
    default:
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        break;
    }
    pd.RasterizerState.FrontCounterClockwise = FALSE;
    pd.RasterizerState.DepthClipEnable = TRUE;

    // Depth is reversed-Z relative to the GX z24 values (near=1, far=0; the projection negation
    // in gx_backend.cpp fill_projection), so GX LESS maps to D3D GREATER, GX LEQUAL to
    // GREATER_EQUAL, and so on, as in Dolphin's D3D backends.
    static constexpr D3D12_COMPARISON_FUNC kGxDepthFunc[8] = {
        D3D12_COMPARISON_FUNC_NEVER,           // GX_NEVER
        D3D12_COMPARISON_FUNC_GREATER,         // GX_LESS     (reversed-Z)
        D3D12_COMPARISON_FUNC_EQUAL,           // GX_EQUAL
        D3D12_COMPARISON_FUNC_GREATER_EQUAL,   // GX_LEQUAL   (reversed-Z)
        D3D12_COMPARISON_FUNC_LESS,            // GX_GREATER  (reversed-Z)
        D3D12_COMPARISON_FUNC_NOT_EQUAL,       // GX_NEQUAL
        D3D12_COMPARISON_FUNC_LESS_EQUAL,      // GX_GEQUAL   (reversed-Z)
        D3D12_COMPARISON_FUNC_ALWAYS,          // GX_ALWAYS
    };
    const std::uint32_t zb = rs.zmode_bits;
    if (zb & 1u) {
        pd.DepthStencilState.DepthEnable = TRUE;
        pd.DepthStencilState.DepthFunc = kGxDepthFunc[(zb >> 1) & 0x7u];
        pd.DepthStencilState.DepthWriteMask = (zb & (1u << 4))
            ? D3D12_DEPTH_WRITE_MASK_ALL
            : D3D12_DEPTH_WRITE_MASK_ZERO;
    }

    pd.SampleMask            = UINT_MAX;
    switch (static_cast<D3D12_PRIMITIVE_TOPOLOGY_TYPE>(
        rs.primitive_topology & kTopologyTypeMask)) {
    case D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT:
        if (point_geometry_shader == nullptr) {
            throw std::invalid_argument(
                "point PSO requires the GX point geometry shader");
        }
        pd.GS = {
            point_geometry_shader->GetBufferPointer(),
            point_geometry_shader->GetBufferSize()};
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
        // GX culling applies to polygons, not points expanded by the host.
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        break;
    case D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE:
        if (line_geometry_shader == nullptr) {
            throw std::invalid_argument(
                "line PSO requires the GX line geometry shader");
        }
        pd.GS = {
            line_geometry_shader->GetBufferPointer(),
            line_geometry_shader->GetBufferSize()};
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        // GX culling applies to polygons, not lines expanded by the host.
        pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        break;
    case D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE:
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        break;
    case D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH:
        // A curved surface's patches (kVsKeyCurved): the hull and domain shaders of its vertex output's variant.
        if (hull_shader == nullptr || domain_shader == nullptr) {
            throw std::invalid_argument(
                "curved PSO requires the curved hull and domain shaders");
        }
        pd.HS = {hull_shader->GetBufferPointer(), hull_shader->GetBufferSize()};
        pd.DS = {domain_shader->GetBufferPointer(), domain_shader->GetBufferSize()};
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
        break;
    default:
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        break;
    }
    pd.NumRenderTargets      = 1;
    pd.RTVFormats[0]         = (rs.samples_log2 & kEfbVariantHdr) != 0u ? kEfbHdrColorFormat : kEfbColorFormat;
    if ((bb & kRenderStateMotionTarget) != 0u) {
        // Opaque motion writer: render target 1 takes the motion vectors
        // with render target 0's write mask (independent blend off).
        pd.NumRenderTargets = 2;
        pd.RTVFormats[1] = kMotionVectorFormat;
    } else if ((bb & kRenderStateReactiveTarget) != 0u) {
        // Blended draw: render target 1 keeps the largest reactive value.
        pd.NumRenderTargets = 2;
        pd.RTVFormats[1] = kReactiveMaskFormat;
        pd.BlendState.IndependentBlendEnable = TRUE;
        D3D12_RENDER_TARGET_BLEND_DESC& mask = pd.BlendState.RenderTarget[1];
        mask.BlendEnable = TRUE;
        mask.SrcBlend = D3D12_BLEND_ONE;
        mask.DestBlend = D3D12_BLEND_ONE;
        mask.BlendOp = D3D12_BLEND_OP_MAX;
        mask.SrcBlendAlpha = D3D12_BLEND_ONE;
        mask.DestBlendAlpha = D3D12_BLEND_ONE;
        mask.BlendOpAlpha = D3D12_BLEND_OP_MAX;
        mask.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED;
    }
    pd.DSVFormat             = kEfbDepthFormat;
    pd.SampleDesc.Count      = 1u << (rs.samples_log2 & kEfbVariantSamplesMask);

    return pd;
}

// Translate a RenderStateKey + compiled shaders into a full PSO desc and
// create the pipeline.  Returns null on failure (logged, capped).
Microsoft::WRL::ComPtr<ID3D12PipelineState> create_gx_pso(
    ID3D12Device* device,
    ID3D12RootSignature* root_signature,
    const RenderStateKey& rs,
    ID3DBlob* vs,
    ID3DBlob* ps,
    ID3DBlob* line_geometry_shader,
    ID3DBlob* point_geometry_shader,
    ID3DBlob* hull_shader,
    ID3DBlob* domain_shader) {
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC pd =
        make_gx_pso_desc_impl(
            root_signature,
            rs,
            vs,
            ps,
            line_geometry_shader,
            point_geometry_shader,
            hull_shader,
            domain_shader);

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
    const HRESULT hr =
        device->CreateGraphicsPipelineState(&pd, IID_PPV_ARGS(&pso));
    if (FAILED(hr)) {
        static std::atomic<int> s_pso_logs{0};
        if (s_pso_logs.fetch_add(1) < 4) {
            std::cerr << "[PipelineCache] CreateGraphicsPipelineState failed"
                         " HRESULT=0x"
                      << std::hex << static_cast<unsigned>(hr) << std::dec
                      << '\n';
        }
        return nullptr;
    }
    return pso;
}

std::wstring pso_library_name(const PsoKey& key) {
    wchar_t name[160]{};
    swprintf_s(
        name,
        L"gx_%016llx_%016llx_%08x_%08x_%02x_%02x_%02x",
        static_cast<unsigned long long>(key.vs_hash),
        static_cast<unsigned long long>(key.ps_hash),
        key.render_state.blend_bits,
        key.render_state.zmode_bits,
        static_cast<unsigned>(key.render_state.cull),
        static_cast<unsigned>(key.render_state.pixfmt),
        static_cast<unsigned>(key.render_state.primitive_topology));
    std::wstring result = name;
    if (key.render_state.samples_log2 != 0u) {
        // Multisampled or HDR: a suffixed name. Single-sample names have no suffix, so existing libraries still match.
        result += L"_m" + std::to_wstring(key.render_state.samples_log2);
    }
    return result;
}

}  // anonymous namespace

D3D12_GRAPHICS_PIPELINE_STATE_DESC make_gx_pso_desc(
    ID3D12RootSignature* root_signature,
    const RenderStateKey& render_state,
    ID3DBlob* vertex_shader,
    ID3DBlob* pixel_shader,
    ID3DBlob* line_geometry_shader,
    ID3DBlob* point_geometry_shader,
    ID3DBlob* hull_shader,
    ID3DBlob* domain_shader) {
    return make_gx_pso_desc_impl(
        root_signature,
        render_state,
        vertex_shader,
        pixel_shader,
        line_geometry_shader,
        point_geometry_shader,
        hull_shader,
        domain_shader);
}

// PsoCompletionQueue: Vyukov MPSC intrusive list with a sentinel node. push() (any thread) swings
// head_ to a new node with an atomic exchange, then links it from the previous head. try_pop()
// (render thread) moves the item out of the sentinel's successor, which becomes the new sentinel,
// and frees the old one; a null successor means the queue is empty.

struct PsoCompletionQueue::Node {
    PsoCompletionQueue::Item item;
    std::atomic<Node*> next{nullptr};
};

PsoCompletionQueue::PsoCompletionQueue() {
    Node* sentinel = new Node{};
    head_.store(sentinel, std::memory_order_relaxed);
    tail_ = sentinel;
}

PsoCompletionQueue::~PsoCompletionQueue() {
    Item discard;
    while (try_pop(discard)) {}
    delete tail_;
}

void PsoCompletionQueue::push(Item item) {
    Node* node = new Node{std::move(item), nullptr};
    Node* prev = head_.exchange(node, std::memory_order_acq_rel);
    // Until this store lands, try_pop() sees the list end at prev and takes the node on a later call.
    prev->next.store(node, std::memory_order_release);
}

bool PsoCompletionQueue::try_pop(Item& out) {
    Node* next = tail_->next.load(std::memory_order_acquire);
    if (next == nullptr) {
        return false;
    }
    out = std::move(next->item);
    delete tail_;
    tail_ = next;
    return true;
}

bool PipelineCache::initialize(
    ID3D12Device* device,
    ID3D12RootSignature* root_signature,
    const std::filesystem::path& cache_dir,
    unsigned worker_count) {

    if (device == nullptr || root_signature == nullptr || device_ ||
        !workers_.empty() || disk_flush_) {
        return false;
    }
    worker_error_ = nullptr;
    shader_cache_at_capacity_ = false;
    pso_cache_at_capacity_ = false;
    worker_failed_.store(false, std::memory_order_relaxed);
    device_         = device;
    root_signature_ = root_signature;
    cache_dir_      = cache_dir;

    const std::string line_geometry_source{
        ShaderGenerator::line_geometry_shader_source()};
    const std::string point_geometry_source{
        ShaderGenerator::point_geometry_shader_source()};
    line_geometry_shader_ = compile_shader(
        line_geometry_source, "gs_5_1", "gx_line_gs");
    point_geometry_shader_ = compile_shader(
        point_geometry_source, "gs_5_1", "gx_point_gs");
    if (!line_geometry_shader_ || !point_geometry_shader_) {
        return false;
    }
    // Curved surfaces: a hull and domain pair per vertex output variant. Without them the draws stay flat.
    curved_ready_ = true;
    for (unsigned variant = 0; variant < kCurvedVariants; ++variant) {
        curved_hull_shaders_[variant] =
            compile_shader(ShaderGenerator::curved_stage_source(variant, false), "hs_5_1", "gx_curve_hs");
        curved_domain_shaders_[variant] =
            compile_shader(ShaderGenerator::curved_stage_source(variant, true), "ds_5_1", "gx_curve_ds");
        curved_ready_ = curved_ready_ && curved_hull_shaders_[variant] && curved_domain_shaders_[variant];
    }
    set_curved_surfaces_offered(curved_ready_);

    stopping_.store(false, std::memory_order_relaxed);


    // Failure only disables disk persistence.
    std::error_code ec;
    std::filesystem::create_directories(cache_dir_, ec);

    uber_enabled_ = true;
    uber_blobs_ready_.store(false, std::memory_order_relaxed);
    uber_compile_thread_ = std::thread([this] {
        galaxy::host::name_current_thread(L"Nebula Uber Compile");
        (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        compile_uber_blobs();
    });

    load_disk_cache();
    load_pso_key_cache();
    load_pipeline_library();
    const std::uint64_t prewarm_jobs = enqueue_cached_pso_prewarm(worker_count);
    if (prewarm_jobs != 0) {
        wait_for_cached_pso_prewarm();
    }

    std::ostringstream ready;
    ready << "[pipeline-cache-ready] pairs=" << shader_blobs_.size()
          << " vertex-stages=" << vertex_shader_blobs_.size()
          << " pixel-stages=" << pixel_shader_blobs_.size()
          << " known-keys=" << warm_pso_keys_.size()
          << " prewarm-enqueued=" << prewarm_jobs
          << " published=" << published_.size()
          << " pending=" << in_flight_.size()
          << " shader-repair=" << shader_cache_repair_pending_
          << " key-repair=" << pso_cache_repair_pending_
          << " driver-library=" << (pipeline_library_ ? "available" : "unavailable")
          << " loaded-library-bytes=" << pipeline_library_blob_.size() << '\n';
    std::cerr << ready.str();

    return true;
}

PipelineCache::~PipelineCache() noexcept {
    try {
        shutdown();
    } catch (const std::exception& e) {
        std::cerr << "[PipelineCache] shutdown failed: " << e.what() << '\n';
    } catch (...) {
        std::cerr << "[PipelineCache] shutdown failed\n";
    }
}

void PipelineCache::shutdown() {
    // Joined first, so the disk writer is joined even if draining completions throws.
    finish_disk_flush(true);
    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        stopping_.store(true, std::memory_order_relaxed);
        // Queued prewarm jobs are dropped; running jobs finish.
        jobs_.clear();
    }
    job_cv_.notify_all();
    completion_cv_.notify_all();

    for (auto& t : workers_) {
        if (t.joinable()) {
            t.join();
        }
    }
    workers_.clear();
    if (uber_compile_thread_.joinable()) {
        uber_compile_thread_.join();
    }
    drain_completions();
    flush_disk();
    if (uber_enabled_) {
        std::cerr << "[uber-pipeline] mode=hybrid"
                  << " blobs-ready=" << (uber_blobs_ready_.load(std::memory_order_acquire) ? 1 : 0)
                  << " uber-psos=" << uber_psos_.size()
                  << " resolutions=" << uber_resolutions_
                  << " async-specializations=" << uber_async_specializations_
                  << " sync-misses=" << uber_fallbacks_ << '\n';
    }

    published_.clear();
    uber_psos_.clear();
    uber_pending_.clear();
    uber_vs_blob_.Reset();
    for (auto& blob : uber_ps_blobs_) blob.Reset();
    uber_blobs_ready_.store(false, std::memory_order_relaxed);
    retired_pipelines_.clear();
    in_flight_.clear();
    vertex_shader_blobs_.clear();
    pixel_shader_blobs_.clear();
    // Failed persistence still owns these CPU DXBC blobs. Keep them usable
    // by a later flush_disk() retry after render resources have shut down.
    if (unsaved_blobs_.empty()) {
        shader_blobs_.clear();
    }
    line_geometry_shader_.Reset();
    point_geometry_shader_.Reset();
    for (unsigned variant = 0; variant < kCurvedVariants; ++variant) {
        curved_hull_shaders_[variant].Reset();
        curved_domain_shaders_[variant].Reset();
    }
    persisted_pso_keys_.clear();
    warm_pso_keys_.clear();
    // Pending keys are retired only by successful persistence in flush_disk().
    // A failed save must remain retryable after render resources shut down.
    pipeline_library_.Reset();
    pipeline_library_blob_.clear();
    pipeline_library_dirty_ = false;
    root_signature_.Reset();
    device_.Reset();
}

ID3D12PipelineState* PipelineCache::get(
    const PsoKey& key,
    const PixelShaderKey& ps_key,
    const VertexShaderKey& vs_key) {

    if (worker_failed_.load(std::memory_order_acquire)) [[unlikely]] {
        std::lock_guard<std::mutex> lock(job_mutex_);
        if (worker_error_) std::rethrow_exception(worker_error_);
    }
    last_get_used_uber_ = false;
    seen_samples_mask_ |= static_cast<std::uint8_t>(1u << key.render_state.samples_log2);
    // One-entry memo of the last resolution. published_ never loses an entry during a session,
    // so a memo hit returns what the map probe would. The hash is compared first; a collision
    // falls through to the full key comparison.
    const std::uint64_t requested_hash = key.hash();
    if (last_hit_pipeline_ != nullptr &&
        last_hit_key_.hash == requested_hash &&
        last_hit_key_.key == key) {
        ++specialized_hits_;
        return last_hit_pipeline_;
    }
    // Hashed once for the memo and both map probes (published_ and in_flight_).
    const MemoizedPsoKey memoized{key, requested_hash};
    {
        const auto it = published_.find(memoized);
        if (it != published_.end() && it->second) {
            ++specialized_hits_;
            last_hit_key_ = memoized;
            last_hit_pipeline_ = it->second.Get();
            last_hit_owner_ = it->second;
            return it->second.Get();
        }
    }
    if (shader_key_capture_file() != nullptr) [[unlikely]] {
        capture_shader_keys(ps_key, vs_key);
    }
    if (in_flight_.find(memoized) != in_flight_.end()) {
        // A worker that finished after begin_frame()'s drain left its pipeline on the completion
        // queue: take it rather than wait for a job that is already done.
        drain_completions();
        if (const auto it = published_.find(memoized);
            it != published_.end() && it->second) {
            ++specialized_hits_;
            last_hit_key_ = memoized;
            last_hit_pipeline_ = it->second.Get();
            last_hit_owner_ = it->second;
            return it->second.Get();
        }
        if (in_flight_.find(memoized) != in_flight_.end()) {
            // Its specialization is compiling: draw with the uber pipeline
            // instead of waiting for the worker.
            if (uber_enabled_) {
                if (ID3D12PipelineState* uber = uber_pipeline(
                        flat_render_state(key.render_state), uber_ps_variant(ps_key))) {
                    last_get_used_uber_ = true;
                    ++uber_resolutions_;
                    return uber;
                }
            }
            ID3D12PipelineState* in_flight_pso = nullptr;
            if (wait_for_in_flight_pso(memoized, in_flight_pso) &&
                in_flight_pso != nullptr) {
                ++specialized_hits_;
                if (const auto it = published_.find(memoized);
                    it != published_.end() && it->second) {
                    last_hit_key_ = memoized;
                    last_hit_pipeline_ = it->second.Get();
                    last_hit_owner_ = it->second;
                }
                return in_flight_pso;
            }
        }
    }

    // When this render state's uber pipeline exists, draw with it and compile
    // the specialization on a worker. Otherwise uber_pipeline() queued that
    // uber PSO for later misses and this one compiles synchronously below.
    if (uber_enabled_) {
        const unsigned variant = uber_ps_variant(ps_key);
        if (ID3D12PipelineState* uber = uber_pipeline(flat_render_state(key.render_state), variant)) {
            enqueue_async_specialization(memoized, ps_key, vs_key);
            last_get_used_uber_ = true;
            ++uber_resolutions_;
            return uber;
        }
        static int s_uber_miss_logs = 0;
        if (s_uber_miss_logs < 64) {
            ++s_uber_miss_logs;
            UberPsoKey probe{};
            probe.render_state.blend_bits = key.render_state.blend_bits;
            probe.render_state.zmode_bits = key.render_state.zmode_bits;
            probe.render_state.cull = key.render_state.cull;
            probe.render_state.pixfmt = key.render_state.pixfmt;
            probe.render_state.primitive_topology = key.render_state.primitive_topology;
            probe.variant = variant;
            const auto built = uber_psos_.find(probe);
            std::cerr << "[uber-miss] blend=0x" << std::hex << key.render_state.blend_bits
                      << " z=0x" << key.render_state.zmode_bits << std::dec
                      << " cull=" << static_cast<unsigned>(key.render_state.cull)
                      << " pixfmt=" << static_cast<unsigned>(key.render_state.pixfmt)
                      << " topo=" << static_cast<unsigned>(key.render_state.primitive_topology)
                      << " variant=" << variant
                      << " blobs=" << (uber_blobs_ready_.load(std::memory_order_acquire) ? 1 : 0)
                      << " pending=" << (uber_pending_.contains(probe) ? 1 : 0)
                      << " failed=" << (built != uber_psos_.end() && !built->second ? 1 : 0)
                      << '\n';
        }
    }

    // Compile synchronously: skipping the draw until an async compile lands would show as
    // pop-in. The DXBC cache (in memory by shader hash, plus shaders.bin) limits FXC to each
    // shader's first compile; render-state variants reuse the blobs and pay only for the
    // driver PSO build.
    ++uber_fallbacks_;

    // Fast first use: an unoptimized FXC build is published now, and a worker replaces it with
    // the level-1 build.
    bool fast_compiled = false;
    const bool fast_first = !stopping_.load(std::memory_order_relaxed);
    const ShaderPair* blobs =
        get_or_compile_blobs(key, ps_key, vs_key, fast_first, &fast_compiled);

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
    if (blobs != nullptr && blobs->vs && blobs->ps) {
        pso = create_or_load_pso(
            key, blobs->vs.Get(), blobs->ps.Get(), /*store_in_library=*/!fast_compiled);
    }
    // Preserve map ownership if a completion populated this key during the
    // in-flight wait. Upgrade only a null entry; never discard a successful
    // PSO or return a pointer from this temporary ComPtr.
    auto published_it = published_.find(memoized);
    if (published_it == published_.end() && pso) {
        published_it = published_.emplace(memoized, std::move(pso)).first;
    } else if (published_it != published_.end() && !published_it->second && pso) {
        published_it->second = std::move(pso);
    }
    ID3D12PipelineState* raw = published_it == published_.end()
        ? nullptr : published_it->second.Get();
    if (raw != nullptr) {
        remember_pso_key(key);
        last_hit_key_ = memoized;
        last_hit_pipeline_ = raw;
        last_hit_owner_ = published_it->second;
    }
    if (raw != nullptr && fast_compiled) {
        ++fast_first_compiles_;
        ensure_compile_workers(1u);
        {
            std::lock_guard<std::mutex> lock(job_mutex_);
            CompileJob job{};
            job.key = key;
            job.ps_key = ps_key;
            job.vs_key = vs_key;
            job.reoptimize = true;
            jobs_.push_back(std::move(job));
        }
        job_cv_.notify_one();
    }
    return raw;
}

void PipelineCache::drain_completions() {
    // Completions add or replace published pipelines (a re-optimized build replaces its fast
    // one), so the last-hit memo is dropped first.
    clear_last_hit_memo();
    if (uber_enabled_) {
        prewarm_uber_for_seen_samples();
    }
    PsoCompletionQueue::Item item;
    while (completions_.try_pop(item)) {
        if (item.uber) {
            UberPsoKey uber_key{item.key.render_state, item.uber_variant};
            uber_pending_.erase(uber_key);
            // A failed build stays recorded as null so it is not retried.
            uber_psos_[uber_key] = std::move(item.pipeline);
            continue;
        }
        // Retire the in-flight marker before anything below can throw, or a
        // retried get() would wait for a completion already consumed.
        const MemoizedPsoKey memoized{item.key};
        in_flight_.erase(memoized);
        // A failed prewarm must leave this key available for a synchronous
        // first-use attempt. A successful completion can replace a null
        // synchronous result but must never replace another successful PSO.
        if (item.pipeline) {
            auto published_it = published_.find(memoized);
            if (published_it == published_.end()) {
#if defined(GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO)
                persistence_test_load_publication(7u);
#endif
                published_it = published_
                    .emplace(memoized, std::move(item.pipeline)).first;
            } else if (!published_it->second) {
                published_it->second = std::move(item.pipeline);
            } else if (item.reoptimize) {
                // The optimized build replaces the fast first-use pipeline.
                // Frames in flight may still execute the old one: retire it
                // instead of releasing it.
                retired_pipelines_.push_back(std::move(published_it->second));
                published_it->second = std::move(item.pipeline);
                ++reoptimized_pipelines_;
            }
            remember_pso_key(item.key);
            if ((item.reoptimize || item.remember_pair) && item.vs_blob && item.ps_blob) {
                remember_reoptimized_pair(item.key, item.vs_blob, item.ps_blob);
            }
        }
    }
}

bool PipelineCache::wait_for_in_flight_pso(
    const MemoizedPsoKey& memoized,
    ID3D12PipelineState*& out) {
    out = nullptr;
    const PsoKey& key = memoized.key;
    // A job still in `jobs_` has no owner, so the render thread may take it and build the PSO
    // itself. A job a worker already took must be waited on, or two threads would create the
    // same pipeline.
    CompileJob inline_job{};
    bool claimed_queued_job = false;
    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        const auto queued = std::find_if(jobs_.begin(), jobs_.end(),
            [&](const CompileJob& job) { return job.key == key; });
        if (queued != jobs_.end()) {
            // Taken under the queue lock, so no worker can start it as well.
            inline_job = std::move(*queued);
            jobs_.erase(queued);
            claimed_queued_job = true;
        }
    }
    // A claimed job builds here from cached DXBC; a running job is waited on below.
    if (claimed_queued_job) {
        try {
            ShaderPair stages{};
            const bool have_blobs = inline_job.use_cached_blobs
                ? static_cast<bool>(inline_job.vs_blob) && static_cast<bool>(inline_job.ps_blob)
                : find_cached_shader_stages(key, stages);
            if (inline_job.use_cached_blobs) {
                stages.vs = inline_job.vs_blob;
                stages.ps = inline_job.ps_blob;
            }
            if (have_blobs && stages.vs && stages.ps) {
                auto inline_pso = create_or_load_pso(key, stages.vs.Get(), stages.ps.Get());
                if (inline_pso) {
                    auto resolved = published_.find(memoized);
                    if (resolved == published_.end()) {
                        resolved = published_.emplace(memoized, std::move(inline_pso)).first;
                    } else if (!resolved->second) {
                        resolved->second = std::move(inline_pso);
                    }
                    in_flight_.erase(memoized);
                    remember_pso_key(key);
                    out = resolved->second.Get();
                    last_hit_key_ = memoized;
                    last_hit_pipeline_ = out;
                    last_hit_owner_ = resolved->second;
                    return out != nullptr;
                }
            }
            // No completion will arrive for this job: retire the marker and
            // let get() compile synchronously.
            in_flight_.erase(memoized);
            return false;
        } catch (...) {
            // The job is gone on every exit, so its marker must not leave a
            // later request waiting.
            in_flight_.erase(memoized);
            throw;
        }
    }
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        const auto observed = completion_snapshot();
        drain_completions();
        if (const auto it = published_.find(memoized); it != published_.end()) {
            out = it->second.Get();
            return true;
        }
        if (in_flight_.find(memoized) == in_flight_.end()) {
            return false;
        }

        const auto elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
        if (static_cast<std::uint64_t>(elapsed_ms) >= kInFlightPsoTimeoutMs) {
            throw std::runtime_error("timed out waiting for a running GX pipeline job");
        }

        wait_for_worker_completion(observed, std::chrono::milliseconds(
            kInFlightPsoTimeoutMs - static_cast<std::uint64_t>(elapsed_ms)));
    }
}

std::uint64_t PipelineCache::completion_snapshot() {
    std::lock_guard<std::mutex> lock(job_mutex_);
    if (worker_error_) std::rethrow_exception(worker_error_);
    return completion_sequence_;
}

void PipelineCache::wait_for_worker_completion(
    std::uint64_t observed, std::chrono::milliseconds timeout) {
#if defined(GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO)
    if (completion_wait_test_hook) completion_wait_test_hook();
#endif
    std::unique_lock<std::mutex> lock(job_mutex_);
    const auto ready = [this, observed] {
        return completion_sequence_ != observed || worker_error_ ||
            stopping_.load(std::memory_order_relaxed);
    };
#if defined(GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO)
    if (!ready()) ++completion_test_parks;
#endif
    // The publisher advances the sequence under this mutex after linking the
    // completed item, so no notification is lost between the check and the wait.
    (void)completion_cv_.wait_for(lock, timeout, ready);
    if (worker_error_) std::rethrow_exception(worker_error_);
    if (completion_sequence_ == observed &&
        stopping_.load(std::memory_order_relaxed)) {
        throw std::runtime_error("GX pipeline workers stopped before required completion");
    }
}

const PipelineCache::ShaderPair* PipelineCache::get_or_compile_blobs(
    const PsoKey& key,
    const PixelShaderKey& ps_key,
    const VertexShaderKey& vs_key,
    bool fast_first,
    bool* fast_compiled) {
    if (fast_compiled != nullptr) *fast_compiled = false;
    const ShaderPairKey ph{key.vs_hash, key.ps_hash};
    {
        const auto it = shader_blobs_.find(ph);
        if (it != shader_blobs_.end() &&
            it->second.vs_hash == key.vs_hash &&
            it->second.ps_hash == key.ps_hash && it->second.vs && it->second.ps) {
            return &it->second;
        }
    }

    ShaderPair pair{};
    pair.vs_hash = key.vs_hash;
    pair.ps_hash = key.ps_hash;
    if (const auto it = vertex_shader_blobs_.find(key.vs_hash);
        it != vertex_shader_blobs_.end()) {
        pair.vs = it->second;
    }
    if (const auto it = pixel_shader_blobs_.find(key.ps_hash);
        it != pixel_shader_blobs_.end()) {
        pair.ps = it->second;
    }

    const bool need_vs = !pair.vs;
    const bool need_ps = !pair.ps;
    // Both stages resident: they came from shaders.bin or earlier compiles.
    if (!need_vs && !need_ps) {
        ++blobs_from_cache_;
    } else {
        ++shader_pair_compiles_;
    }
    // Times the FXC work only.
    const auto compile_start = std::chrono::steady_clock::now();
    if ((need_vs || need_ps) && fast_first && fast_compiled != nullptr) {
        // Fast first use: unoptimized FXC for the missing stages, kept out of
        // every cache (a worker compiles and persists the optimized pair).
        if (need_vs && need_ps) {
            auto compiled = compile_independent_shader_pair(
                generator_.generate_vs(vs_key), generator_.generate_ps(ps_key),
                compile_shader_fast);
            pair.vs = std::move(compiled.first);
            pair.ps = std::move(compiled.second);
        } else if (need_vs) {
            pair.vs = compile_shader_fast(generator_.generate_vs(vs_key), "vs_5_1", "gx_vs");
        } else {
            pair.ps = compile_shader_fast(generator_.generate_ps(ps_key), "ps_5_1", "gx_ps");
        }
        blob_compile_us_ +=
            elapsed_us(compile_start, std::chrono::steady_clock::now());
        if (!pair.vs || !pair.ps) {
            return nullptr;
        }
        *fast_compiled = true;
        fast_pair_scratch_ = std::move(pair);
        return &fast_pair_scratch_;
    }
    if (need_vs && need_ps) {
        const std::string vs_src = generator_.generate_vs(vs_key);
        const std::string ps_src = generator_.generate_ps(ps_key);
        auto compiled = compile_independent_shader_pair(
            vs_src, ps_src, compile_shader);
        pair.vs = std::move(compiled.first);
        pair.ps = std::move(compiled.second);
    } else if (need_vs) {
        const std::string vs_src = generator_.generate_vs(vs_key);
        pair.vs = compile_shader(vs_src, "vs_5_1", "gx_vs");
    } else if (need_ps) {
        const std::string ps_src = generator_.generate_ps(ps_key);
        pair.ps = compile_shader(ps_src, "ps_5_1", "gx_ps");
    }
    blob_compile_us_ +=
        elapsed_us(compile_start, std::chrono::steady_clock::now());

    if (pair.vs) {
        vertex_shader_blobs_.try_emplace(key.vs_hash, pair.vs);
    }
    if (pair.ps) {
        pixel_shader_blobs_.try_emplace(key.ps_hash, pair.ps);
    }

    if (pair.vs && pair.ps) {
        if (!shader_cache_at_capacity_) unsaved_blobs_.push_back(ph);
    } else {
        // A transient compiler failure must remain retryable.
        return nullptr;
    }
    try {
#if defined(GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO)
        persistence_test_load_publication(7u);
#endif
        const auto [it, inserted] = shader_blobs_.emplace(ph, std::move(pair));
        (void)inserted;
        return &it->second;
    } catch (...) {
        // Drop the identity appended above; the resident stages let a retry
        // recombine the pair without compiling.
        if (!shader_cache_at_capacity_) unsaved_blobs_.pop_back();
        throw;
    }
}

// shaders.bin: u32 magic 'GXSC', u32 version, then records of u64 vs_hash, u64 ps_hash,
// u32 vs_size, u32 ps_size, u64 vs and ps FNV-1a checksums, vs DXBC, ps DXBC.
// psos.bin: u32 magic 'GKPC', u32 version, then bare PsoKey records (sizeof(PsoKey) bytes each).

namespace {
constexpr std::uint32_t kShaderCacheMagic   = 0x43535847u;  // "GXSC"
// The version gates generator output, not only the file format: bump it whenever
// generate_vs or generate_ps emits different HLSL for the same key, or stale DXBC
// would be served for hash-identical keys.
constexpr std::uint32_t kShaderCacheVersion = 49u;
constexpr std::uint32_t kPsoKeyCacheMagic = 0x43504B47u;  // "GKPC"
// PSO keys name shader-cache stages, so psos.bin is rebuilt whenever the shader cache
// version changes; a key whose stages are missing is skipped at prewarm.
constexpr std::uint32_t kPsoKeyCacheVersion = 0x100u + kShaderCacheVersion;
constexpr std::uint32_t kPipelineLibraryMagic = 0x4C505847u; // "GXPL"
constexpr std::uint32_t kPipelineLibraryVersion = 10u;
constexpr std::uint64_t kMaxPipelineLibraryBytes = 64ull << 20;
constexpr std::uint64_t kMaxShaderCacheBytes = 128ull << 20;
constexpr std::uint32_t kMaxCacheRecords = 65536u;
// Resident records a file does not hold, with nothing pending, trigger a rewrite only once
// there are more than this many, not on every flush.
constexpr std::size_t kCacheCoverageAppendBatch = 64u;

bool valid_pso_key(const PsoKey& key) {
    const auto& state = key.render_state;
    return state.primitive_topology >= 1u && state.primitive_topology <= 3u &&
        state.cull <= 3u && state.pixfmt <= 7u && state.samples_log2 <= 7u &&
        (state.blend_bits & 0x80000000u) == 0u && (state.zmode_bits & ~0x1fu) == 0u &&
        std::all_of(std::begin(state.reserved), std::end(state.reserved),
            [](std::uint8_t value) { return value == 0u; });
}

enum class ShaderRecordRead : std::uint8_t { Ok, Invalid, Resource };

#if defined(GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO)
std::size_t persistence_test_read_budget = std::numeric_limits<std::size_t>::max();
bool persistence_test_reflection_failure = false;
unsigned persistence_test_load_stage = 0u;
std::size_t persistence_test_load_budget = std::numeric_limits<std::size_t>::max();
unsigned persistence_test_load_exception = 0u;
void persistence_test_load_publication(unsigned stage) {
    if (stage != persistence_test_load_stage) return;
    if (persistence_test_load_budget != 0u) {
        --persistence_test_load_budget;
        return;
    }
    if (persistence_test_load_exception == 0u) throw std::bad_alloc{};
    if (persistence_test_load_exception == 1u) throw std::length_error("cache load publication");
    throw std::runtime_error("cache load publication");
}
#endif

// A failed allocation does not mean the cached bytes are corrupt.
ShaderRecordRead valid_shader_blob(ID3DBlob* blob, bool vertex) {
    if (blob == nullptr) return ShaderRecordRead::Invalid;
    Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
    HRESULT reflected;
#if defined(GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO)
    if (persistence_test_read_budget == 0u && persistence_test_reflection_failure) {
        reflected = E_OUTOFMEMORY;
    } else
#endif
    reflected = D3DReflect(blob->GetBufferPointer(), blob->GetBufferSize(),
        IID_PPV_ARGS(&reflection));
    if (reflected == E_OUTOFMEMORY) return ShaderRecordRead::Resource;
    if (FAILED(reflected)) return ShaderRecordRead::Invalid;
    D3D12_SHADER_DESC desc{};
    if (FAILED(reflection->GetDesc(&desc))) return ShaderRecordRead::Resource;
    // DXBC shader version tokens encode the program type in their high word:
    // pixel = 0, vertex = 1 (D3D12_SHVER_*).
    return (desc.Version >> 16) == (vertex ? 1u : 0u)
        ? ShaderRecordRead::Ok : ShaderRecordRead::Invalid;
}

ShaderRecordRead read_shader_record(FILE* f, std::uint64_t& vs_hash, std::uint64_t& ps_hash,
    Microsoft::WRL::ComPtr<ID3DBlob>& vs, Microsoft::WRL::ComPtr<ID3DBlob>& ps) {
    std::uint32_t vs_size = 0u, ps_size = 0u;
    std::uint64_t vs_checksum = 0u, ps_checksum = 0u;
    if (std::fread(&vs_hash, sizeof(vs_hash), 1, f) != 1 ||
        std::fread(&ps_hash, sizeof(ps_hash), 1, f) != 1 ||
        std::fread(&vs_size, sizeof(vs_size), 1, f) != 1 ||
        std::fread(&ps_size, sizeof(ps_size), 1, f) != 1 ||
        std::fread(&vs_checksum, sizeof(vs_checksum), 1, f) != 1 ||
        std::fread(&ps_checksum, sizeof(ps_checksum), 1, f) != 1) {
        // A short read is a truncated record unless the stream itself failed.
        return std::ferror(f) != 0 ? ShaderRecordRead::Resource : ShaderRecordRead::Invalid;
    }
    if (vs_size == 0u || ps_size == 0u ||
        vs_size > (16u << 20) || ps_size > (16u << 20)) return ShaderRecordRead::Invalid;
    const __int64 payload_start = _ftelli64(f);
    if (payload_start < 0 || _fseeki64(f, 0, SEEK_END) != 0) return ShaderRecordRead::Resource;
    const __int64 end = _ftelli64(f);
    if (end < 0) return ShaderRecordRead::Resource;
    if (end < payload_start ||
        static_cast<std::uint64_t>(end - payload_start) <
            static_cast<std::uint64_t>(vs_size) + ps_size) return ShaderRecordRead::Invalid;
    if (_fseeki64(f, payload_start, SEEK_SET) != 0) return ShaderRecordRead::Resource;
#if defined(GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO)
    if (persistence_test_read_budget == 0u && !persistence_test_reflection_failure)
        return ShaderRecordRead::Resource;
#endif
    if (FAILED(D3DCreateBlob(vs_size, &vs)) || FAILED(D3DCreateBlob(ps_size, &ps))) {
        return ShaderRecordRead::Resource;
    }
    if (std::fread(vs->GetBufferPointer(), 1, vs_size, f) != vs_size ||
        std::fread(ps->GetBufferPointer(), 1, ps_size, f) != ps_size) {
        return std::ferror(f) != 0 ? ShaderRecordRead::Resource : ShaderRecordRead::Invalid;
    }
    if (fnv1a64(vs->GetBufferPointer(), vs_size) != vs_checksum ||
        fnv1a64(ps->GetBufferPointer(), ps_size) != ps_checksum)
        return ShaderRecordRead::Invalid;
    const auto vertex = valid_shader_blob(vs.Get(), true);
    if (vertex != ShaderRecordRead::Ok) return vertex;
    const auto pixel = valid_shader_blob(ps.Get(), false);
#if defined(GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO)
    if (pixel == ShaderRecordRead::Ok && persistence_test_read_budget != 0u)
        --persistence_test_read_budget;
#endif
    return pixel;
}

// Serializes replacement writers across processes. The cache remains optional:
// a busy or inaccessible lock leaves pending records for a later retry.
class CacheWriterLease {
public:
    explicit CacheWriterLease(const std::filesystem::path& path) {
        auto lock_path = path;
        lock_path += L".lock";
        handle_ = CreateFileW(lock_path.c_str(), GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    ~CacheWriterLease() { if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); }
    explicit operator bool() const { return handle_ != INVALID_HANDLE_VALUE; }
private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};


// Both record formats keep their valid prefix. Persistence writes a replacement
// beside the original, so an interrupted write cannot corrupt the next attempt.
#if defined(GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO)
std::size_t persistence_test_write_budget = std::numeric_limits<std::size_t>::max();
bool persistence_test_fail_close = false;
HANDLE persistence_test_write_entered = nullptr;
HANDLE persistence_test_write_release = nullptr;
#endif

bool write_cache_bytes(FILE* f, const void* data, std::size_t bytes) {
#if defined(GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO)
    if (persistence_test_write_entered && persistence_test_write_release) {
        SetEvent(persistence_test_write_entered);
        if (WaitForSingleObject(persistence_test_write_release, 30000u) != WAIT_OBJECT_0)
            return false;
    }
    if (bytes > persistence_test_write_budget) {
        if (persistence_test_write_budget != 0u) {
            (void)std::fwrite(data, 1, persistence_test_write_budget, f);
        }
        persistence_test_write_budget = 0u;
        return false;
    }
    persistence_test_write_budget -= bytes;
#endif
    return std::fwrite(data, 1, bytes, f) == bytes;
}

bool close_cache_output(FILE* f) {
    const bool closed = std::fclose(f) == 0;
#if defined(GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO)
    return closed && !persistence_test_fail_close;
#else
    return closed;
#endif
}

template <typename CollectShader>
bool find_cache_prefix(
    FILE* f, std::uint32_t expected_magic, std::uint32_t expected_version,
    bool shader_records, std::uint64_t& prefix_bytes, std::uint32_t& retained_records,
    CollectShader collect_shader) {
    prefix_bytes = 0u;
    retained_records = 0u;
    if (_fseeki64(f, 0, SEEK_END) != 0) return false;
    const __int64 signed_size = _ftelli64(f);
    if (signed_size < 0 || _fseeki64(f, 0, SEEK_SET) != 0) return false;
    const auto size = static_cast<std::uint64_t>(signed_size);
    if (size < 8u) return true;
    std::uint32_t magic = 0u, version = 0u;
    if (std::fread(&magic, sizeof(magic), 1, f) != 1 ||
        std::fread(&version, sizeof(version), 1, f) != 1) return false;
    if (magic != expected_magic || version != expected_version) return true;
    prefix_bytes = 8u;
    if (!shader_records) {
        PsoKey key{};
        std::uint32_t records = 0u;
        while (records++ < kMaxCacheRecords && std::fread(&key, sizeof(key), 1, f) == 1 &&
               valid_pso_key(key)) {
            prefix_bytes += sizeof(key);
            ++retained_records;
        }
        return std::ferror(f) == 0;
    }
    std::uint32_t records = 0u;
    while (size - prefix_bytes >= 40u && prefix_bytes < kMaxShaderCacheBytes &&
           records++ < kMaxCacheRecords) {
        std::uint64_t vs_hash = 0u, ps_hash = 0u;
        Microsoft::WRL::ComPtr<ID3DBlob> vs, ps;
        const auto read = read_shader_record(f, vs_hash, ps_hash, vs, ps);
        if (read == ShaderRecordRead::Resource) return false;
        if (read == ShaderRecordRead::Invalid) break;
        const auto end = _ftelli64(f);
        if (end < 0) return false;
        if (static_cast<std::uint64_t>(end) > kMaxShaderCacheBytes) break;
        collect_shader(vs_hash, ps_hash);
        prefix_bytes = static_cast<std::uint64_t>(end);
        ++retained_records;
    }
    return std::ferror(f) == 0;
}

bool cache_has_tail(FILE* f, __int64 last_complete) {
    if (last_complete < 0 || std::ferror(f) != 0 ||
        _fseeki64(f, 0, SEEK_END) != 0) return false;
    return _ftelli64(f) > last_complete;
}

// Validate under the writer lease, collecting shader identities during that
// scan. Copy the complete prefix verbatim; append only records not represented
// there. Any resource/identity-allocation failure leaves the original untouched.
template <typename CollectShader, typename WritePending>
bool replace_record_cache(
    const std::filesystem::path& path, std::uint32_t magic,
    std::uint32_t version, bool shader_records,
    CollectShader collect_shader, WritePending write_pending) {
    CacheWriterLease lease(path);
    if (!lease) return false;
    FILE* source = nullptr;
    const galaxy::ScopeExit close_source([&]() noexcept {
        if (source != nullptr) {
            std::fclose(source);
            source = nullptr;
        }
    });
    std::uint64_t prefix_bytes = 0u;
    std::uint32_t retained_records = 0u;
    try {
        if (_wfopen_s(&source, path.c_str(), L"rb") == 0 && source != nullptr) {
            if (!find_cache_prefix(source, magic, version, shader_records, prefix_bytes, retained_records, collect_shader) ||
                _fseeki64(source, 0, SEEK_SET) != 0) {
                return false;
            }
        } else {
            std::error_code ec;
            // A read failure for an existing cache is not proof it is corrupt.
            const bool exists = std::filesystem::exists(path, ec);
            if (ec || exists) return false;
        }
    } catch (const std::bad_alloc&) {
        return false;
    } catch (const std::length_error&) {
        return false;
    }
    // `source` is closed on every exit. The temp file is removed unless published, also on
    // an exception, but only once this call has created or truncated it, so a concurrent
    // writer's temp file survives.
    FILE* output = nullptr;
    bool output_owned = false;
    bool published = false;
    // Outlives the guard below, which removes it on failure.
    std::filesystem::path tmp = path;
    tmp += L".tmp";
    const galaxy::ScopeExit close_output_on_failure([&]() noexcept {
        if (output != nullptr) {
            close_cache_output(output);
            output = nullptr;
        }
        if (output_owned && !published) {
            std::error_code cleanup_ec;
            std::filesystem::remove(tmp, cleanup_ec);
        }
    });
    output_owned = _wfopen_s(&output, tmp.c_str(), L"wb") == 0 && output != nullptr;
    if (!output_owned) return false;
    // `source` is null only when no cache file existed, and then prefix_bytes is
    // zero, so the copy loop is skipped.
    bool written = true;
    std::array<unsigned char, 64u << 10> buffer{};
    std::uint64_t remaining = prefix_bytes;
    while (written && remaining != 0u) {
        const auto chunk = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
        written = source != nullptr &&
            std::fread(buffer.data(), 1, chunk, source) == chunk &&
            write_cache_bytes(output, buffer.data(), chunk);
        remaining -= chunk;
    }
    if (written && prefix_bytes == 0u) {
        written = write_cache_bytes(output, &magic, sizeof(magic)) &&
            write_cache_bytes(output, &version, sizeof(version));
    }
    // The copy leaves `source` at the end of the prefix; write_pending reads the retained
    // records from the first one, after the 8-byte header.
    if (written && retained_records != 0u) {
        written = source != nullptr && _fseeki64(source, 8, SEEK_SET) == 0;
    }
    // An allocation failure fails this save only; the pending records stay pending.
    if (written) {
        try {
            written = write_pending(output, source, retained_records);
        } catch (const std::bad_alloc&) {
            written = false;
        } catch (const std::length_error&) {
            written = false;
        }
    }
    // Both handles are closed, and the results checked, before MoveFileExW replaces the source.
    const bool closed_output = close_cache_output(output);
    output = nullptr;
    bool closed_source = true;
    if (source != nullptr) {
        closed_source = std::fclose(source) == 0;
        source = nullptr;
    }
    // A failed close or replacement fails the save, and the guard removes the temp file.
    if (!closed_output || !closed_source) written = false;
    if (written && MoveFileExW(
            tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        published = true;
        return true;
    }
    written = false;
    return false;
}
}  // namespace

#if defined(GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO)
void set_pipeline_cache_persistence_test_fault(std::size_t write_budget, bool fail_close) {
    persistence_test_write_budget = write_budget;
    persistence_test_fail_close = fail_close;
}
void set_pipeline_cache_persistence_test_write_gate(HANDLE entered, HANDLE release) {
    persistence_test_write_entered = entered;
    persistence_test_write_release = release;
}
void set_pipeline_cache_persistence_test_read_fault(std::size_t read_budget, bool reflection) {
    persistence_test_read_budget = read_budget;
    persistence_test_reflection_failure = reflection;
}
void set_pipeline_cache_persistence_test_load_fault(
    unsigned stage, std::size_t budget, unsigned exception_kind) {
    persistence_test_load_stage = stage;
    persistence_test_load_budget = budget;
    persistence_test_load_exception = exception_kind;
}
#endif

void PipelineCache::load_disk_cache() {
    const std::filesystem::path path = cache_dir_ / "shaders.bin";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || f == nullptr) {
        return;
    }
    const galaxy::ScopeExit close_input([&]() noexcept { std::fclose(f); });
    std::uint32_t magic = 0, version = 0;
    if (std::fread(&magic, sizeof(magic), 1, f) != 1 ||
        std::fread(&version, sizeof(version), 1, f) != 1 ||
        magic != kShaderCacheMagic || version != kShaderCacheVersion) {
        const bool read_failed = std::ferror(f) != 0;
        if (read_failed) return;
        // A stale or foreign file is ignored, and replaced by the next save.
        shader_cache_repair_pending_ = true;
        return;
    }
    unsigned loaded = 0;
    __int64 last_complete = _ftelli64(f);
    bool resource_failed = last_complete < 0;
    try {
        while (!resource_failed && loaded < kMaxCacheRecords &&
            last_complete < static_cast<__int64>(kMaxShaderCacheBytes)) {
            std::uint64_t vs_hash = 0, ps_hash = 0;
            ShaderPair pair{};
            const auto read = read_shader_record(f, vs_hash, ps_hash, pair.vs, pair.ps);
            const auto record_end = _ftelli64(f);
            if (read != ShaderRecordRead::Ok || record_end < 0 ||
                record_end > static_cast<__int64>(kMaxShaderCacheBytes)) {
                resource_failed = read == ShaderRecordRead::Resource || record_end < 0;
                break;
            }
            pair.vs_hash = vs_hash;
            pair.ps_hash = ps_hash;
#if defined(GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO)
            persistence_test_load_publication(1u);
#endif
            pair.vs = vertex_shader_blobs_.try_emplace(vs_hash, pair.vs).first->second;
#if defined(GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO)
            persistence_test_load_publication(2u);
#endif
            pair.ps = pixel_shader_blobs_.try_emplace(ps_hash, pair.ps).first->second;
#if defined(GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO)
            persistence_test_load_publication(3u);
#endif
            shader_blobs_.emplace(ShaderPairKey{vs_hash, ps_hash}, std::move(pair));
            ++loaded;
            last_complete = record_end;
        }
    } catch (const std::bad_alloc&) {
        resource_failed = true;
    } catch (const std::length_error&) {
        resource_failed = true;
    }
    // After a resource failure the unread tail is not corruption, so no repair is scheduled.
    shader_cache_repair_pending_ = !resource_failed && cache_has_tail(f, last_complete);
    // Records the file holds; ones past a corrupt tail are written again by the next flush.
    cached_shader_records_ = loaded;
}

void PipelineCache::load_pso_key_cache() {
    const std::filesystem::path path = cache_dir_ / "psos.bin";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || f == nullptr) {
        return;
    }
    const galaxy::ScopeExit close_input([&]() noexcept { std::fclose(f); });

    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    // psos.bin is a bare run of PsoKey records, so changing PsoKey needs a version bump;
    // shader_keys.h asserts its size and that it has no padding.
    static_assert(std::is_trivially_copyable_v<PsoKey>,
        "psos.bin is a bare run of PsoKey records and is read/written with "
        "fread/fwrite, so PsoKey must stay trivially copyable; changing it "
        "requires bumping kPsoKeyCacheVersion so stale files are rejected rather "
        "than misparsed");
    static_assert(sizeof(magic) == 4u && sizeof(version) == 4u,
        "psos.bin header is two 32-bit words");
    if (std::fread(&magic, sizeof(magic), 1, f) != 1 ||
        std::fread(&version, sizeof(version), 1, f) != 1 ||
        magic != kPsoKeyCacheMagic || version != kPsoKeyCacheVersion) {
        if (std::ferror(f) != 0) return;
        pso_cache_repair_pending_ = true;
        return;
    }

    PsoKey key{};
    __int64 last_complete = _ftelli64(f);
    std::uint32_t records = 0;
    bool resource_failed = last_complete < 0;
    try {
        while (!resource_failed && records++ < kMaxCacheRecords &&
            std::fread(&key, sizeof(key), 1, f) == 1) {
            if (!valid_pso_key(key)) break;
#if defined(GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO)
            persistence_test_load_publication(4u);
#endif
            const auto [position, inserted] = persisted_pso_keys_.insert(key);
            if (inserted) {
                try {
#if defined(GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO)
                    persistence_test_load_publication(5u);
#endif
                    warm_pso_keys_.push_back(key);
                } catch (...) {
                    // Do not leave a key resident but permanently absent from
                    // prewarming when publication of this new key fails.
                    persisted_pso_keys_.erase(position);
                    throw;
                }
            }
            last_complete = _ftelli64(f);
            resource_failed = last_complete < 0;
        }
    } catch (const std::bad_alloc&) {
        resource_failed = true;
    } catch (const std::length_error&) {
        resource_failed = true;
    }
    resource_failed = resource_failed || std::ferror(f) != 0;
    pso_cache_repair_pending_ = !resource_failed && cache_has_tail(f, last_complete);
    // Records the file holds; see load_disk_cache.
    cached_pso_records_ = persisted_pso_keys_.size();
}

void PipelineCache::remember_pso_key(const PsoKey& key) {
    const auto [position, inserted] = persisted_pso_keys_.insert(key);
    if (inserted) {
        try {
#if defined(GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO)
            persistence_test_load_publication(6u);
#endif
            if (!pso_cache_at_capacity_) unsaved_pso_keys_.push_back(key);
        } catch (...) {
            persisted_pso_keys_.erase(position);
            throw;
        }
    }
}

void PipelineCache::load_pipeline_library() {
    D3D12_FEATURE_DATA_SHADER_CACHE shader_cache{};
    if (FAILED(device_->CheckFeatureSupport(
            D3D12_FEATURE_SHADER_CACHE,
            &shader_cache,
            sizeof(shader_cache))) ||
        (shader_cache.SupportFlags & D3D12_SHADER_CACHE_SUPPORT_LIBRARY) == 0) {
        return;
    }

    Microsoft::WRL::ComPtr<ID3D12Device1> device1;
    if (FAILED(device_.As(&device1))) {
        return;
    }

    const LUID adapter_luid = device_->GetAdapterLuid();
    const std::filesystem::path path = cache_dir_ / "pipelines.bin";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") == 0 && f != nullptr) {
        const galaxy::ScopeExit close_input([&]() noexcept { std::fclose(f); });
        std::uint32_t magic = 0;
        std::uint32_t version = 0;
        std::uint32_t shader_version = 0;
        std::uint32_t luid_low = 0;
        std::uint32_t luid_high = 0;
        std::uint64_t blob_size = 0;
        const bool header_ok =
            std::fread(&magic, sizeof(magic), 1, f) == 1 &&
            std::fread(&version, sizeof(version), 1, f) == 1 &&
            std::fread(&shader_version, sizeof(shader_version), 1, f) == 1 &&
            std::fread(&luid_low, sizeof(luid_low), 1, f) == 1 &&
            std::fread(&luid_high, sizeof(luid_high), 1, f) == 1 &&
            std::fread(&blob_size, sizeof(blob_size), 1, f) == 1;
        const bool matches_device =
            luid_low == adapter_luid.LowPart &&
            static_cast<LONG>(luid_high) == adapter_luid.HighPart;
        if (!header_ok ||
            magic != kPipelineLibraryMagic ||
            version != kPipelineLibraryVersion ||
            shader_version != kShaderCacheVersion ||
            !matches_device ||
            blob_size == 0 ||
            blob_size > kMaxPipelineLibraryBytes) {
            pipeline_library_blob_.clear();
        } else {
            try {
                pipeline_library_blob_.resize(static_cast<std::size_t>(blob_size));
                const std::size_t got = std::fread(
                    pipeline_library_blob_.data(), 1, pipeline_library_blob_.size(), f);
                if (got != pipeline_library_blob_.size()) pipeline_library_blob_.clear();
            } catch (const std::bad_alloc&) {
                pipeline_library_blob_.clear();
            } catch (const std::length_error&) {
                pipeline_library_blob_.clear();
            }
        }
    }

    const void* blob_ptr = pipeline_library_blob_.empty()
        ? nullptr
        : pipeline_library_blob_.data();
    const SIZE_T blob_size = pipeline_library_blob_.size();
    HRESULT hr = device1->CreatePipelineLibrary(
        blob_ptr,
        blob_size,
        IID_PPV_ARGS(&pipeline_library_));
    if (FAILED(hr) && !pipeline_library_blob_.empty()) {
        pipeline_library_.Reset();
        pipeline_library_blob_.clear();
        hr = device1->CreatePipelineLibrary(
            nullptr,
            0,
            IID_PPV_ARGS(&pipeline_library_));
    }

    if (FAILED(hr)) {
        pipeline_library_.Reset();
    }
}

Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineCache::create_or_load_pso(
    const PsoKey& key,
    ID3DBlob* vs_blob,
    ID3DBlob* ps_blob,
    bool store_in_library) {
#if defined(GALAXY_PIPELINE_CACHE_PERSISTENCE_TEST_IO)
    if (pipeline_test_creator != nullptr) return pipeline_test_creator(key);
#endif
    const unsigned curved = curved_variant(key.render_state.primitive_topology);
    ID3DBlob* const hull = curved_hull_shaders_[curved].Get();
    ID3DBlob* const domain = curved_domain_shaders_[curved].Get();
    if ((key.render_state.primitive_topology & kTopologyTypeMask) == kTopologyPatch &&
        (hull == nullptr || domain == nullptr)) {
        return nullptr;
    }
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = make_gx_pso_desc(
        root_signature_.Get(),
        key.render_state,
        vs_blob,
        ps_blob,
        line_geometry_shader_.Get(),
        point_geometry_shader_.Get(),
        hull,
        domain);
    const std::wstring name = pso_library_name(key);

    {
        std::lock_guard<std::mutex> lock(pipeline_library_mutex_);
        if (pipeline_library_) {
            Microsoft::WRL::ComPtr<ID3D12PipelineState> cached;
            const HRESULT load_hr = pipeline_library_->LoadGraphicsPipeline(
                name.c_str(),
                &pd,
                IID_PPV_ARGS(&cached));
            if (SUCCEEDED(load_hr)) {
                pso_library_loads_.fetch_add(1, std::memory_order_relaxed);
                return cached;
            }
        }
    }

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso = create_gx_pso(
        device_.Get(),
        root_signature_.Get(),
        key.render_state,
        vs_blob,
        ps_blob,
        line_geometry_shader_.Get(),
        point_geometry_shader_.Get(),
        hull,
        domain);
    if (!pso) {
        return nullptr;
    }
    pso_compiles_.fetch_add(1, std::memory_order_relaxed);

    if (store_in_library) {
        std::lock_guard<std::mutex> lock(pipeline_library_mutex_);
        if (pipeline_library_) {
            const HRESULT store_hr =
                pipeline_library_->StorePipeline(name.c_str(), pso.Get());
            if (SUCCEEDED(store_hr)) {
                pipeline_library_dirty_ = true;
            }
        }
    }

    return pso;
}

void PipelineCache::flush_pipeline_library() {
    // create_or_load_pso takes pipeline_library_mutex_ around every Load/StorePipeline on the
    // render thread. Only the library calls need it: the library is serialized into a local
    // copy under the lock and the file is written after releasing it, so no pipeline lookup
    // waits on the disk.
    std::vector<std::uint8_t> bytes;
    bool needs_retry = false;
    const galaxy::ScopeExit retry_failed_save([&]() noexcept {
        if (needs_retry) {
            std::lock_guard<std::mutex> lock(pipeline_library_mutex_);
            pipeline_library_dirty_ = true;
        }
    });
    {
        std::lock_guard<std::mutex> lock(pipeline_library_mutex_);
        if (!pipeline_library_ || !pipeline_library_dirty_) {
            return;
        }

        const SIZE_T size = pipeline_library_->GetSerializedSize();
        if (size == 0 || size > kMaxPipelineLibraryBytes) {
            return;
        }

        bytes.resize(size);
        const HRESULT hr =
            pipeline_library_->Serialize(bytes.data(), bytes.size());
        if (FAILED(hr)) {
            bytes.clear();
            return;
        }
        // Cleared under the lock so a concurrent StorePipeline stays pending
        // after this snapshot is saved; any failure below re-arms it.
        needs_retry = true;
        pipeline_library_dirty_ = false;
    }

    const std::filesystem::path path = cache_dir_ / "pipelines.bin";
    CacheWriterLease lease(path);
    if (!lease) return;
    const std::filesystem::path tmp = cache_dir_ / "pipelines.bin.tmp";
    FILE* f = nullptr;
    if (_wfopen_s(&f, tmp.c_str(), L"wb") != 0 || f == nullptr) {
        return;
    }
    const LUID adapter_luid = device_->GetAdapterLuid();
    const std::uint32_t magic = kPipelineLibraryMagic;
    const std::uint32_t version = kPipelineLibraryVersion;
    const std::uint32_t shader_version = kShaderCacheVersion;
    const std::uint32_t luid_low = adapter_luid.LowPart;
    const std::uint32_t luid_high =
        static_cast<std::uint32_t>(adapter_luid.HighPart);
    const std::uint64_t blob_size =
        static_cast<std::uint64_t>(bytes.size());
    bool wrote_header =
        write_cache_bytes(f, &magic, sizeof(magic)) &&
        write_cache_bytes(f, &version, sizeof(version)) &&
        write_cache_bytes(f, &shader_version, sizeof(shader_version)) &&
        write_cache_bytes(f, &luid_low, sizeof(luid_low)) &&
        write_cache_bytes(f, &luid_high, sizeof(luid_high)) &&
        write_cache_bytes(f, &blob_size, sizeof(blob_size));
    const bool wrote = wrote_header && write_cache_bytes(f, bytes.data(), bytes.size());
    const bool closed = close_cache_output(f);
    if (!wrote || !closed) {
        std::error_code ec;
        std::filesystem::remove(tmp, ec);
        return;
    }

    std::error_code ec;
    if (!MoveFileExW(tmp.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(tmp, ec);
        return;
    }

    // The dirty flag is left alone: workers may have stored pipelines since the snapshot.
    needs_retry = false;
}

bool PipelineCache::find_cached_shader_stages(
    const PsoKey& key, ShaderPair& out) const {
    out = {};
    const auto vs = vertex_shader_blobs_.find(key.vs_hash);
    const auto ps = pixel_shader_blobs_.find(key.ps_hash);
    if (vs == vertex_shader_blobs_.end() || !vs->second ||
        ps == pixel_shader_blobs_.end() || !ps->second) return false;
    out.vs_hash = key.vs_hash;
    out.ps_hash = key.ps_hash;
    out.vs = vs->second;
    out.ps = ps->second;
    return true;
}

std::uint64_t PipelineCache::enqueue_cached_pso_prewarm(unsigned worker_count) {
    if (warm_pso_keys_.empty()) {
        return 0;
    }

    published_.reserve(warm_pso_keys_.size());
    in_flight_.reserve(warm_pso_keys_.size());
    std::uint64_t enqueued = 0;
    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        for (const PsoKey& key : warm_pso_keys_) {
            if (enqueued >= kPsoPrewarmLimit) {
                break;
            }
            // The stages may be resident under other pairs; their hashes
            // identify the DXBC, so the exact pair need not be.
            ShaderPair stages{};
            if (!find_cached_shader_stages(key, stages)) {
                continue;
            }
            jobs_.push_back(CompileJob{
                key,
                PixelShaderKey{},
                VertexShaderKey{},
                stages.vs,
                stages.ps,
                true});
            in_flight_.insert(MemoizedPsoKey{key});
            ++enqueued;
        }
    }
    if (enqueued != 0) {
        const unsigned threads = static_cast<unsigned>(
            std::min<std::uint64_t>(std::clamp(worker_count, 1u, 4u), enqueued));
        workers_.reserve(threads);
        for (unsigned i = 0; i < threads; ++i) {
            workers_.emplace_back([this] {
                // Below the render thread (ABOVE_NORMAL), so prewarm never delays a draw's pipeline
                // lookup; a key needed before its job ran is built by the first-use path.
                galaxy::host::name_current_thread(L"Nebula PSO Prewarm");
                (void)SetThreadPriority(
                    GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
                worker_main();
            });
        }
        job_cv_.notify_all();
    }
    return enqueued;
}

void PipelineCache::wait_for_cached_pso_prewarm() {
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        const auto observed = completion_snapshot();
        drain_completions();
        if (in_flight_.empty()) {
            return;
        }
        const auto elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
        if (static_cast<std::uint64_t>(elapsed_ms) >= kPsoPrewarmTimeoutMs) {
            return;
        }
        wait_for_worker_completion(observed, std::chrono::milliseconds(
            kPsoPrewarmTimeoutMs - static_cast<std::uint64_t>(elapsed_ms)));
    }
}

template<class State>
PipelineCache::DiskFlushResult PipelineCache::flush_disk_records(State& state) {
    DiskFlushResult result{};
    auto& cache_dir_ = state.cache_dir_;
    auto& shader_blobs_ = state.shader_blobs_;
    auto& unsaved_blobs_ = state.unsaved_blobs_;
    auto& persisted_pso_keys_ = state.persisted_pso_keys_;
    auto& unsaved_pso_keys_ = state.unsaved_pso_keys_;
    auto& cached_shader_records_ = state.cached_shader_records_;
    auto& cached_pso_records_ = state.cached_pso_records_;
    auto& shader_cache_at_capacity_ = state.shader_cache_at_capacity_;
    auto& pso_cache_at_capacity_ = state.pso_cache_at_capacity_;
    auto& shader_cache_repair_pending_ = state.shader_cache_repair_pending_;
    auto& pso_cache_repair_pending_ = state.pso_cache_repair_pending_;
    const bool shader_coverage_pending = shader_blobs_.size() >
        cached_shader_records_ + kCacheCoverageAppendBatch;
    const bool pso_coverage_pending = persisted_pso_keys_.size() >
        cached_pso_records_ + kCacheCoverageAppendBatch;
    const bool save_shaders = !shader_cache_at_capacity_ &&
        (!unsaved_blobs_.empty() || shader_cache_repair_pending_ ||
         shader_coverage_pending);
    const bool save_psos = !pso_cache_at_capacity_ &&
        (!unsaved_pso_keys_.empty() || pso_cache_repair_pending_ ||
         pso_coverage_pending);
    if (!save_shaders && !save_psos) return result;

    if (save_shaders) {
        std::uint32_t saved_records = 0u;
        bool reached_capacity = false;
        // The validated prefix is copied forward and `records` counts its records;
        // only pairs the prefix lacks are written.
        std::unordered_set<ShaderPairKey, ShaderPairKeyHasher> in_prefix;
        const bool saved = replace_record_cache(
            cache_dir_ / "shaders.bin", kShaderCacheMagic, kShaderCacheVersion, true,
            [&](std::uint64_t vs_hash, std::uint64_t ps_hash) {
                in_prefix.insert(ShaderPairKey{vs_hash, ps_hash});
            },
            [&](FILE* f, FILE*, std::uint32_t records) {
                const auto write_pair = [&](const ShaderPair& pair) {
                    const auto position = _ftelli64(f);
                    if (position < 0 || pair.vs->GetBufferSize() > (16u << 20u) ||
                        pair.ps->GetBufferSize() > (16u << 20u)) return false;
                    if (records >= kMaxCacheRecords ||
                        static_cast<std::uint64_t>(position) + 40u +
                            pair.vs->GetBufferSize() + pair.ps->GetBufferSize() > kMaxShaderCacheBytes) {
                        reached_capacity = true;
                        return true; // Publish all complete records that fit.
                    }
                    ++records;
                    const auto vs_size = static_cast<std::uint32_t>(pair.vs->GetBufferSize());
                    const auto ps_size = static_cast<std::uint32_t>(pair.ps->GetBufferSize());
                    const auto vs_checksum = fnv1a64(pair.vs->GetBufferPointer(), vs_size);
                    const auto ps_checksum = fnv1a64(pair.ps->GetBufferPointer(), ps_size);
                    return write_cache_bytes(f, &pair.vs_hash, sizeof(pair.vs_hash)) &&
                        write_cache_bytes(f, &pair.ps_hash, sizeof(pair.ps_hash)) &&
                        write_cache_bytes(f, &vs_size, sizeof(vs_size)) &&
                        write_cache_bytes(f, &ps_size, sizeof(ps_size)) &&
                        write_cache_bytes(f, &vs_checksum, sizeof(vs_checksum)) &&
                        write_cache_bytes(f, &ps_checksum, sizeof(ps_checksum)) &&
                        write_cache_bytes(f, pair.vs->GetBufferPointer(), vs_size) &&
                        write_cache_bytes(f, pair.ps->GetBufferPointer(), ps_size);
                };
                // Identities come from the validated scan, so the shaders are not hashed and reflected
                // again. Resident pairs are written, then pending ones; a pair can be both (shutdown keeps
                // shader_blobs_ while unsaved_blobs_ is non-empty), and `emitted` drops the duplicate.
                std::unordered_set<ShaderPairKey, ShaderPairKeyHasher> emitted;
                for (const auto& entry : shader_blobs_) {
                    if (reached_capacity) break;
                    const ShaderPair& pair = entry.second;
                    if (!pair.vs || !pair.ps) return false;
                    if (in_prefix.find(entry.first) != in_prefix.end()) continue;
                    if (!write_pair(pair)) return false;
                    emitted.insert(entry.first);
                }
                for (const ShaderPairKey& ph : unsaved_blobs_) {
                    if (reached_capacity) break;
                    const auto it = shader_blobs_.find(ph);
                    if (it == shader_blobs_.end() || !it->second.vs || !it->second.ps) return false;
                    if (in_prefix.find(ph) != in_prefix.end()) continue;
                    if (!emitted.insert(ph).second) continue;
                    if (!write_pair(it->second)) return false;
                }
                saved_records = records;
                return true;
            });
        if (saved) {
            result.shaders_saved = true;
            unsaved_blobs_.clear();
            shader_cache_repair_pending_ = false;
            cached_shader_records_ = saved_records;
            shader_cache_at_capacity_ = reached_capacity;
            if (reached_capacity) {
                std::cerr << "[PipelineCache] shader cache capacity reached; retained-records="
                          << saved_records << " resident-pairs=" << shader_blobs_.size() << '\n';
            }
        }
    }

    if (save_psos) {
        std::uint32_t saved_records = 0u;
        bool reached_capacity = false;
        const bool saved = replace_record_cache(
            cache_dir_ / "psos.bin", kPsoKeyCacheMagic, kPsoKeyCacheVersion, false,
            [](std::uint64_t, std::uint64_t) {},
            [&](FILE* f, FILE* src, std::uint32_t records) {
                // PsoKey::operator== ignores the reserved bytes, which valid_pso_key forces to zero.
                std::unordered_set<PsoKey, PsoKeyHasher> in_prefix;
                in_prefix.reserve(records);
                for (std::uint32_t i = 0; i < records; ++i) {
                    PsoKey key{};
                    if (std::fread(&key, sizeof(key), 1, src) != 1) return false;
                    in_prefix.insert(key);
                }
                // Resident and pending keys are both written: after shutdown clears persisted_pso_keys_,
                // a retry has only unsaved_pso_keys_. `emitted` drops a key that is in both.
                std::unordered_set<PsoKey, PsoKeyHasher> emitted;
                const auto emit = [&](const PsoKey& key) {
                    if (in_prefix.find(key) != in_prefix.end()) return true;
                    if (!emitted.insert(key).second) return true;
                    if (records >= kMaxCacheRecords) {
                        reached_capacity = true;
                        return true;
                    }
                    ++records;
                    return write_cache_bytes(f, &key, sizeof(key));
                };
                for (const PsoKey& key : persisted_pso_keys_) {
                    if (reached_capacity) break;
                    if (!emit(key)) return false;
                }
                for (const PsoKey& key : unsaved_pso_keys_) {
                    if (reached_capacity) break;
                    if (!emit(key)) return false;
                }
                saved_records = records;
                return true;
            });
        if (saved) {
            result.psos_saved = true;
            unsaved_pso_keys_.clear();
            pso_cache_repair_pending_ = false;
            cached_pso_records_ = saved_records;
            pso_cache_at_capacity_ = reached_capacity;
            if (reached_capacity) {
                std::cerr << "[PipelineCache] PSO cache capacity reached; retained-records="
                          << saved_records << " resident-keys=" << persisted_pso_keys_.size() << '\n';
            }
        }
    }

    return result;
}

void PipelineCache::finish_disk_flush(bool wait) {
    if (!disk_flush_ || (!wait && !disk_flush_->done.load(std::memory_order_acquire)))
        return;
    // `done` is set after the writer's last access; only flush_disk() and
    // shutdown wait for unfinished I/O.
    if (disk_writer_.joinable()) disk_writer_.join();
    const auto& saved = *disk_flush_;
    if (saved.result.shaders_saved) {
        cached_shader_records_ = saved.cached_shader_records_;
        shader_cache_at_capacity_ = saved.shader_cache_at_capacity_;
        shader_cache_repair_pending_ = false;
        if (shader_cache_at_capacity_) unsaved_blobs_.clear();
        else unsaved_blobs_.erase(unsaved_blobs_.begin(),
            unsaved_blobs_.begin() + saved.pending_shaders);
    }
    if (saved.result.psos_saved) {
        cached_pso_records_ = saved.cached_pso_records_;
        pso_cache_at_capacity_ = saved.pso_cache_at_capacity_;
        pso_cache_repair_pending_ = false;
        if (pso_cache_at_capacity_) unsaved_pso_keys_.clear();
        else unsaved_pso_keys_.erase(unsaved_pso_keys_.begin(),
            unsaved_pso_keys_.begin() + saved.pending_psos);
    }
    disk_flush_.reset();
}

void PipelineCache::poll_disk_flush() {
    finish_disk_flush(false);
}

void PipelineCache::flush_disk_async() {
    poll_disk_flush();
    if (disk_flush_) return;
    const bool shaders_pending = !shader_cache_at_capacity_ &&
        (!unsaved_blobs_.empty() || shader_cache_repair_pending_ ||
         shader_blobs_.size() > cached_shader_records_ + kCacheCoverageAppendBatch);
    const bool psos_pending = !pso_cache_at_capacity_ &&
        (!unsaved_pso_keys_.empty() || pso_cache_repair_pending_ ||
         persisted_pso_keys_.size() > cached_pso_records_ + kCacheCoverageAppendBatch);
    if (!shaders_pending && !psos_pending) return;
    try {
        auto snapshot = std::make_unique<DiskFlushSnapshot>();
        snapshot->cache_dir_ = cache_dir_;
        snapshot->shader_blobs_ = shader_blobs_;
        snapshot->unsaved_blobs_ = unsaved_blobs_;
        snapshot->persisted_pso_keys_ = persisted_pso_keys_;
        snapshot->unsaved_pso_keys_ = unsaved_pso_keys_;
        snapshot->cached_shader_records_ = cached_shader_records_;
        snapshot->cached_pso_records_ = cached_pso_records_;
        snapshot->shader_cache_at_capacity_ = shader_cache_at_capacity_;
        snapshot->pso_cache_at_capacity_ = pso_cache_at_capacity_;
        snapshot->shader_cache_repair_pending_ = shader_cache_repair_pending_;
        snapshot->pso_cache_repair_pending_ = pso_cache_repair_pending_;
        snapshot->pending_shaders = unsaved_blobs_.size();
        snapshot->pending_psos = unsaved_pso_keys_.size();
        auto* batch = snapshot.get();
        disk_writer_ = std::thread([batch] {
            galaxy::host::name_current_thread(L"Nebula PSO Disk Writer");
            (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            try { batch->result = flush_disk_records(*batch); }
            catch (...) {
                // Optional persistence must not terminate the process. Pending
                // live records were never removed, so the next flush can retry.
            }
            batch->done.store(true, std::memory_order_release);
        });
        disk_flush_ = std::move(snapshot);
    } catch (const std::bad_alloc&) {
        // Snapshot allocation or thread creation leaves every live record pending.
    } catch (const std::length_error&) {
    } catch (const std::system_error&) {
    }
}

void PipelineCache::flush_disk() {
    finish_disk_flush(true);
    (void)flush_disk_records(*this);
    flush_pipeline_library();
}

std::uint64_t PipelineCache::specialized_hits() const {
    return specialized_hits_;
}

std::uint64_t PipelineCache::uber_fallbacks() const {
    return uber_fallbacks_;
}

PipelineCache::CompileStats PipelineCache::compile_stats() const {
    // Render thread. pso_compiles_ and pso_library_loads_ are atomics because prewarm workers
    // increment them too; relaxed loads suffice for these monotonic counters.
    CompileStats stats{};
    stats.pso_compiles = pso_compiles_.load(std::memory_order_relaxed);
    stats.pso_library_loads = pso_library_loads_.load(std::memory_order_relaxed);
    stats.shader_pair_compiles = shader_pair_compiles_;
    stats.blobs_from_cache = blobs_from_cache_;
    stats.blob_compile_us = blob_compile_us_;
    stats.shader_records = shader_blobs_.size();
    stats.shader_records_on_disk = cached_shader_records_;
    stats.pso_keys = persisted_pso_keys_.size();
    stats.pso_keys_on_disk = cached_pso_records_;
    stats.shader_cache_at_capacity = shader_cache_at_capacity_;
    stats.pso_cache_at_capacity = pso_cache_at_capacity_;
    return stats;
}

// Render states common in play. Their uber pipelines are built in the background once the
// uber DXBC exists, so a first-use material on a fresh install finds one.
namespace {
struct KnownRenderState {
    std::uint32_t blend_bits;
    std::uint16_t zmode_bits;
    std::uint8_t cull;
    std::uint8_t pixfmt;
    std::uint8_t topology;
};
constexpr KnownRenderState kKnownRenderStates[] = {
    {0x06000000u, 0x0017u, 1u, 1u, 3u},
    {0x02000000u, 0x0017u, 1u, 1u, 3u},
    {0x06000000u, 0x0017u, 0u, 1u, 3u},
    {0x02000000u, 0x0017u, 0u, 1u, 3u},
    {0x0a0000b1u, 0x0000u, 0u, 1u, 3u},
    {0x0a0000b1u, 0x0007u, 0u, 1u, 3u},
    {0x0a000031u, 0x0007u, 0u, 1u, 3u},
    {0x0a000031u, 0x0007u, 1u, 1u, 3u},
    {0x0a0000b1u, 0x0007u, 1u, 1u, 3u},
    {0x02000025u, 0x0007u, 1u, 1u, 3u},
    {0x0e0000b1u, 0x0000u, 0u, 1u, 3u},
    {0x0a000031u, 0x0000u, 0u, 1u, 3u},
    {0x04000027u, 0x000du, 1u, 1u, 3u},
    {0x04000025u, 0x000du, 2u, 1u, 3u},
    {0x020000b1u, 0x0000u, 0u, 0u, 3u},
    {0x060000ddu, 0x0000u, 1u, 1u, 3u},
    {0x0400003du, 0x0000u, 1u, 1u, 3u},
    {0x04000000u, 0x0007u, 0u, 1u, 3u},
    {0x00000000u, 0x001fu, 2u, 1u, 3u},
    {0x0a000031u, 0x0017u, 2u, 1u, 3u},
    {0x06000000u, 0x001fu, 1u, 1u, 3u},
    {0x02000000u, 0x0000u, 0u, 1u, 3u},
    {0x06000000u, 0x0000u, 0u, 1u, 3u},
    {0x04000000u, 0x000du, 1u, 1u, 3u},
    {0x04000000u, 0x0000u, 1u, 1u, 3u},
    {0x0a000051u, 0x0007u, 1u, 1u, 3u},
    {0x02000025u, 0x0000u, 0u, 1u, 3u},
    {0x0e000031u, 0x0007u, 0u, 1u, 3u},
    {0x1e0000b1u, 0x000fu, 0u, 1u, 3u},
    {0x02000025u, 0x0007u, 2u, 1u, 3u},
    {0x00000000u, 0x001fu, 1u, 1u, 3u},
    {0x0e000031u, 0x0000u, 0u, 1u, 3u},
    {0x16000025u, 0x0007u, 1u, 1u, 3u},
    {0x00000000u, 0x001fu, 1u, 0u, 3u},
    {0x02000000u, 0x0000u, 0u, 0u, 3u},
    {0x02000000u, 0x001fu, 1u, 0u, 3u},
    {0x02000000u, 0x0007u, 1u, 1u, 3u},
    {0x0a000031u, 0x0017u, 1u, 1u, 3u},
    {0x04000019u, 0x0000u, 0u, 1u, 3u},
    {0x1e000051u, 0x0007u, 0u, 1u, 3u},
    {0x04000027u, 0x0007u, 2u, 1u, 3u},
    {0x0a000031u, 0x0017u, 0u, 1u, 3u},
    {0x160000f9u, 0x0000u, 0u, 1u, 3u},
    {0x02000025u, 0x0007u, 0u, 1u, 3u},
    {0x16000045u, 0x0007u, 0u, 1u, 3u},
    {0x04000000u, 0x0000u, 0u, 1u, 3u},
    {0x04000025u, 0x0007u, 1u, 1u, 3u},
    {0x040000f9u, 0x0000u, 0u, 1u, 3u},
    {0x04000000u, 0x000fu, 0u, 1u, 3u},
    {0x0a000051u, 0x0017u, 1u, 1u, 3u},
    {0x02000045u, 0x0007u, 1u, 1u, 3u},
    {0x0a000031u, 0x0019u, 2u, 1u, 3u},
    {0x1e0000b1u, 0x0007u, 1u, 1u, 3u},
    {0x1e0000b1u, 0x0017u, 1u, 1u, 3u},
    {0x02000005u, 0x0017u, 2u, 1u, 3u},
    {0x0a0000b1u, 0x0017u, 1u, 1u, 3u},
    {0x02000009u, 0x0000u, 0u, 1u, 3u},
    {0x02000027u, 0x0007u, 1u, 1u, 3u},
    {0x0e0000b1u, 0x0007u, 0u, 1u, 3u},
    {0x0e0000b1u, 0x0007u, 1u, 1u, 3u},
    {0x16000025u, 0x0017u, 1u, 1u, 3u},
    {0x02000025u, 0x0017u, 1u, 1u, 3u},
    {0x06000000u, 0x0017u, 2u, 1u, 3u},
    {0x0a000031u, 0x0000u, 0u, 1u, 2u},
};
}  // namespace

// Uber compile thread. D3DCompile is thread-safe; the blobs are published
// once, with release ordering.
void PipelineCache::compile_uber_blobs() noexcept {
    try {
        const auto start = std::chrono::steady_clock::now();
        const std::string vs_source(uber_vs_source());
        const std::string ps_source(uber_ps_source());
        uber_source_hash_ = combine64(
            fnv1a64(vs_source.data(), vs_source.size()),
            fnv1a64(ps_source.data(), ps_source.size()));
        // uber.bin: u64 source hash, then 1 + kUberPsVariants records of
        // (u32 size, DXBC bytes). A different hash is a different build.
        const std::filesystem::path cache_path = cache_dir_ / "uber.bin";
        {
            std::ifstream in(cache_path, std::ios::binary);
            std::uint64_t hash = 0u;
            if (in && in.read(reinterpret_cast<char*>(&hash), sizeof(hash)) &&
                hash == uber_source_hash_) {
                std::array<Microsoft::WRL::ComPtr<ID3DBlob>, 1u + kUberPsVariants> blobs{};
                bool loaded = true;
                for (auto& blob : blobs) {
                    std::uint32_t size = 0u;
                    if (!in.read(reinterpret_cast<char*>(&size), sizeof(size)) ||
                        size == 0u || size > (8u << 20u) ||
                        FAILED(D3DCreateBlob(size, &blob)) ||
                        !in.read(static_cast<char*>(blob->GetBufferPointer()), size)) {
                        loaded = false;
                        break;
                    }
                }
                if (loaded) {
                    uber_vs_blob_ = blobs[0];
                    for (unsigned variant = 0; variant < kUberPsVariants; ++variant) {
                        uber_ps_blobs_[variant] = blobs[1u + variant];
                    }
                    uber_blobs_ready_.store(true, std::memory_order_release);
                    std::cerr << "[uber-pipeline] shaders loaded from cache\n";
                    return;
                }
            }
        }
        Microsoft::WRL::ComPtr<ID3DBlob> vs = compile_shader(vs_source, "vs_5_1", "gx_uber_vs");
        std::array<Microsoft::WRL::ComPtr<ID3DBlob>, kUberPsVariants> ps{};
        bool complete = static_cast<bool>(vs);
        for (unsigned variant = 0; complete && variant < kUberPsVariants; ++variant) {
            std::string source;
            source.reserve(ps_source.size() + 96u);
            source += "#define UBER_DUAL_SRC ";
            source += uber_ps_variant_dual_source(variant) ? "1\n" : "0\n";
            source += "#define UBER_EXPORT_DEPTH ";
            source += uber_ps_variant_export_depth(variant) ? "1\n" : "0\n";
            source += "#define UBER_EARLY_DEPTH ";
            source += uber_ps_variant_early_depth(variant) ? "1\n" : "0\n";
            source += ps_source;
            ps[variant] = compile_shader(source, "ps_5_1", "gx_uber_ps");
            complete = static_cast<bool>(ps[variant]);
        }
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (!complete) {
            std::cerr << "[uber-pipeline] shader compile failed\n";
            return;
        }
        uber_vs_blob_ = std::move(vs);
        uber_ps_blobs_ = std::move(ps);
        uber_blobs_ready_.store(true, std::memory_order_release);
        {
            // Written beside the cache and renamed, so a reader never sees a
            // partial file. Optional: a failure only costs the next compile.
            const std::filesystem::path temp_path = cache_dir_ / "uber.bin.tmp";
            {
                std::ofstream out(temp_path, std::ios::binary | std::ios::trunc);
                out.write(reinterpret_cast<const char*>(&uber_source_hash_), sizeof(uber_source_hash_));
                const auto write_blob = [&out](ID3DBlob* blob) {
                    const auto size = static_cast<std::uint32_t>(blob->GetBufferSize());
                    out.write(reinterpret_cast<const char*>(&size), sizeof(size));
                    out.write(static_cast<const char*>(blob->GetBufferPointer()), size);
                };
                write_blob(uber_vs_blob_.Get());
                for (const auto& blob : uber_ps_blobs_) write_blob(blob.Get());
            }
            std::error_code ec;
            std::filesystem::rename(temp_path, cache_path, ec);
        }
        std::cerr << "[uber-pipeline] shaders ready compile-ms=" << elapsed_ms
                  << " vs-bytes=" << uber_vs_blob_->GetBufferSize()
                  << " ps-bytes=" << uber_ps_blobs_[0]->GetBufferSize() << '\n';
    } catch (...) {
        // Optional: without uber blobs every miss keeps the synchronous path.
    }
}

// Render thread. Returns the uber pipeline for this render state and output
// variant when it exists. Otherwise queues its build once the uber DXBC is ready.
ID3D12PipelineState* PipelineCache::uber_pipeline(
    const RenderStateKey& render_state, unsigned variant) {
    if (!uber_blobs_ready_.load(std::memory_order_acquire)) {
        return nullptr;
    }
    prewarm_uber_for_seen_samples();
    UberPsoKey key{};
    // Uber stand-ins stay single-target: a motion writer drawing with one
    // leaves its pixels' motion at the cleared zero for those frames.
    key.render_state.blend_bits =
        render_state.blend_bits & ~(kRenderStateMotionTarget | kRenderStateReactiveTarget);
    key.render_state.zmode_bits = render_state.zmode_bits;
    key.render_state.cull = render_state.cull;
    key.render_state.pixfmt = render_state.pixfmt;
    key.render_state.primitive_topology = render_state.primitive_topology;
    key.render_state.samples_log2 = render_state.samples_log2;
    key.variant = variant;
    if (const auto it = uber_psos_.find(key); it != uber_psos_.end()) {
        return it->second.Get();  // null: its build failed, never retried
    }
    enqueue_uber_pso(key);
    return nullptr;
}

void PipelineCache::enqueue_uber_pso(const UberPsoKey& key) {
    if (uber_pending_.contains(key) || stopping_.load(std::memory_order_relaxed)) {
        return;
    }
    CompileJob job{};
    job.key.render_state = key.render_state;
    job.uber = true;
    job.uber_variant = key.variant;
    ensure_compile_workers(2u);
    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        jobs_.push_back(std::move(job));
    }
    uber_pending_.insert(key);
    job_cv_.notify_one();
}

// Queues the uber prewarm (enqueue_uber_prewarm) once for each sample count draws have used.
void PipelineCache::prewarm_uber_for_seen_samples() {
    const std::uint8_t pending = seen_samples_mask_ & ~uber_prewarmed_samples_mask_;
    if (pending == 0u || !uber_blobs_ready_.load(std::memory_order_acquire)) {
        return;
    }
    uber_prewarmed_samples_mask_ |= pending;
    for (std::uint8_t samples_log2 = 0u; samples_log2 < 8u; ++samples_log2) {
        if ((pending & (1u << samples_log2)) != 0u) {
            enqueue_uber_prewarm(samples_log2);
        }
    }
}

void PipelineCache::enqueue_uber_prewarm(std::uint8_t samples_log2) {
    std::unordered_set<UberPsoKey, UberPsoKeyHasher> seen;
    // Early-Z first (most materials), then late-Z (alpha-tested materials,
    // about as common in RMGE01), then the Z-texture depth export. A blend
    // state reading SRC1 alpha needs the dual-source variant of each.
    const auto add = [&](unsigned pass, std::uint32_t blend_bits, std::uint16_t zmode_bits,
                         std::uint8_t cull, std::uint8_t pixfmt, std::uint8_t topology) {
        UberPsoKey key{};
        key.render_state.blend_bits =
            blend_bits & ~(kRenderStateMotionTarget | kRenderStateReactiveTarget);
        key.render_state.zmode_bits = zmode_bits;
        key.render_state.cull = cull;
        key.render_state.pixfmt = pixfmt;
        key.render_state.primitive_topology = topology;
        key.render_state.samples_log2 = samples_log2;
        const unsigned dual = (blend_bits & (1u << 27u)) != 0u ? 1u : 0u;
        key.variant = (pass == 0u ? 4u : pass == 1u ? 0u : 2u) + dual;
        if (seen.insert(key).second && !uber_psos_.contains(key)) {
            enqueue_uber_pso(key);
        }
    };
    for (unsigned pass = 0u; pass < 3u; ++pass) {
        for (const PsoKey& known : warm_pso_keys_) {
            add(pass, known.render_state.blend_bits, known.render_state.zmode_bits,
                known.render_state.cull, known.render_state.pixfmt,
                known.render_state.primitive_topology);
        }
        for (const KnownRenderState& known : kKnownRenderStates) {
            add(pass, known.blend_bits, known.zmode_bits, known.cull, known.pixfmt,
                known.topology);
        }
    }
}

// Compile workers.
Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineCache::create_uber_pso(
    const RenderStateKey& render_state, unsigned variant) {
    if (!uber_blobs_ready_.load(std::memory_order_acquire) || variant >= kUberPsVariants) {
        return nullptr;
    }
    ID3DBlob* vs = uber_vs_blob_.Get();
    ID3DBlob* ps = uber_ps_blobs_[variant].Get();
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = make_gx_pso_desc(
        root_signature_.Get(), render_state, vs, ps,
        line_geometry_shader_.Get(), point_geometry_shader_.Get(), nullptr, nullptr);
    wchar_t name[160]{};
    swprintf_s(name, L"uber_%016llx_%08x_%08x_%02x_%02x_%02x_%u",
        static_cast<unsigned long long>(uber_source_hash_),
        render_state.blend_bits, render_state.zmode_bits,
        static_cast<unsigned>(render_state.cull),
        static_cast<unsigned>(render_state.pixfmt),
        static_cast<unsigned>(render_state.primitive_topology), variant);
    if (render_state.samples_log2 != 0u) {
        const std::size_t length = wcslen(name);
        swprintf_s(name + length, std::size(name) - length, L"_m%u",
            static_cast<unsigned>(render_state.samples_log2));
    }
    {
        std::lock_guard<std::mutex> lock(pipeline_library_mutex_);
        if (pipeline_library_) {
            Microsoft::WRL::ComPtr<ID3D12PipelineState> cached;
            if (SUCCEEDED(pipeline_library_->LoadGraphicsPipeline(name, &pd, IID_PPV_ARGS(&cached)))) {
                return cached;
            }
        }
    }
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso = create_gx_pso(
        device_.Get(), root_signature_.Get(), render_state, vs, ps,
        line_geometry_shader_.Get(), point_geometry_shader_.Get(), nullptr, nullptr);
    if (pso) {
        std::lock_guard<std::mutex> lock(pipeline_library_mutex_);
        if (pipeline_library_ && SUCCEEDED(pipeline_library_->StorePipeline(name, pso.Get()))) {
            pipeline_library_dirty_ = true;
        }
    }
    return pso;
}

// Render thread: the specialization of a key now drawing with an uber
// pipeline. Cached DXBC is reused; a new pair is compiled and then cached.
void PipelineCache::enqueue_async_specialization(
    const MemoizedPsoKey& memoized,
    const PixelShaderKey& ps_key,
    const VertexShaderKey& vs_key) {
    if (in_flight_.contains(memoized) || stopping_.load(std::memory_order_relaxed)) {
        return;
    }
    CompileJob job{};
    job.key = memoized.key;
    job.ps_key = ps_key;
    job.vs_key = vs_key;
    ShaderPair stages{};
    if (find_cached_shader_stages(job.key, stages) && stages.vs && stages.ps) {
        job.use_cached_blobs = true;
        job.vs_blob = stages.vs;
        job.ps_blob = stages.ps;
    } else {
        job.remember_pair = true;
    }
    ensure_compile_workers(2u);
    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        jobs_.push_back(std::move(job));
    }
    in_flight_.insert(memoized);
    ++uber_async_specializations_;
    job_cv_.notify_one();
}

void PipelineCache::ensure_compile_workers(unsigned count) {
    if (!workers_.empty() || stopping_.load(std::memory_order_relaxed)) {
        return;
    }
    const unsigned threads = std::clamp(count, 1u, 2u);
    workers_.reserve(threads);
    for (unsigned i = 0; i < threads; ++i) {
        workers_.emplace_back([this] {
            // Below the render thread, like the prewarm workers.
            galaxy::host::name_current_thread(L"Nebula PSO Compile");
            (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            worker_main();
        });
    }
}

// Render thread: the optimized pair of a fast first-use compile enters the
// stage and pair caches and the disk cache, exactly as a synchronous level-1
// compile would have.
void PipelineCache::remember_reoptimized_pair(
    const PsoKey& key,
    const Microsoft::WRL::ComPtr<ID3DBlob>& vs,
    const Microsoft::WRL::ComPtr<ID3DBlob>& ps) {
    const ShaderPairKey ph{key.vs_hash, key.ps_hash};
    vertex_shader_blobs_.try_emplace(key.vs_hash, vs);
    pixel_shader_blobs_.try_emplace(key.ps_hash, ps);
    if (shader_blobs_.contains(ph)) {
        return;
    }
    ShaderPair pair{};
    pair.vs = vs;
    pair.ps = ps;
    pair.vs_hash = key.vs_hash;
    pair.ps_hash = key.ps_hash;
    if (!shader_cache_at_capacity_) unsaved_blobs_.push_back(ph);
    try {
        shader_blobs_.emplace(ph, std::move(pair));
    } catch (...) {
        if (!shader_cache_at_capacity_) unsaved_blobs_.pop_back();
        throw;
    }
}

void PipelineCache::worker_main() {
    for (;;) {
        CompileJob job{};
        {
            std::unique_lock<std::mutex> lock(job_mutex_);
            job_cv_.wait(lock, [this] {
                return !jobs_.empty() ||
                       stopping_.load(std::memory_order_relaxed);
            });
            if (jobs_.empty()) {
                break;
            }
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }

        try {
        if (job.uber) {
            PsoCompletionQueue::Item completion;
            completion.key = job.key;
            completion.uber = true;
            completion.uber_variant = job.uber_variant;
            completion.pipeline = create_uber_pso(job.key.render_state, job.uber_variant);
            completions_.push(std::move(completion));
            {
                std::lock_guard<std::mutex> lock(job_mutex_);
                ++completion_sequence_;
            }
            completion_cv_.notify_all();
            continue;
        }
        Microsoft::WRL::ComPtr<ID3DBlob> vs = job.vs_blob;
        Microsoft::WRL::ComPtr<ID3DBlob> ps = job.ps_blob;
        if (!job.use_cached_blobs) {
            const std::string vs_src = generator_.generate_vs(job.vs_key);
            const std::string ps_src = generator_.generate_ps(job.ps_key);
            vs = compile_shader(vs_src, "vs_5_1", "gx_vs");
            ps = compile_shader(ps_src, "ps_5_1", "gx_ps");
        }

        PsoCompletionQueue::Item completion;
        completion.key = job.key;
        if (vs && ps) {
            completion.pipeline = create_or_load_pso(job.key, vs.Get(), ps.Get());
        }
        if (job.reoptimize) {
            completion.reoptimize = true;
            completion.vs_blob = vs;
            completion.ps_blob = ps;
        } else if (job.remember_pair) {
            completion.remember_pair = true;
            completion.vs_blob = vs;
            completion.ps_blob = ps;
        }

        completions_.push(std::move(completion));
        {
            std::lock_guard<std::mutex> lock(job_mutex_);
            ++completion_sequence_;
        }
        completion_cv_.notify_all();
        } catch (...) {
            {
                std::lock_guard<std::mutex> lock(job_mutex_);
                if (!worker_error_) worker_error_ = std::current_exception();
                worker_failed_.store(true, std::memory_order_release);
                stopping_.store(true, std::memory_order_relaxed);
                jobs_.clear();
            }
            completion_cv_.notify_all();
            job_cv_.notify_all();
            return;
        }
    }
}

}  // namespace galaxy::gx
