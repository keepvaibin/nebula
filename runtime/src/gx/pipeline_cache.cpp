// pipeline_cache.cpp: lock-free PSO completion queue (Vyukov MPSC) and
// PipelineCache with background shader specialization.
//
// Threading contract:
//   - get(), drain_completions(): render thread only.
//   - push(): any compile worker thread, lock-free.
//   - try_pop(): render thread only.
//
// published_ and in_flight_ are render-thread-only and need no locks. The job intake
// (jobs_, job_mutex_, job_cv_) uses a plain mutex and condvar. The path back to the
// render thread (completion queue) stays lock-free.

#include "galaxy/gx/pipeline_cache.h"

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
// Required directly by the `std::is_trivially_copyable_v<PsoKey>` assert at the
// `psos.bin` parse site. It is not taken transitively: `shader_keys.h` includes
// <type_traits>, but this translation unit should not depend on a sibling header's
// include list for a trait it uses itself.
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

bool trace_gx_stalls_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_GX_STALLS") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

std::uint64_t read_env_u64(const char* name, std::uint64_t fallback) {
    char value[32]{};
    std::size_t length = 0;
    if (getenv_s(&length, value, sizeof(value), name) != 0 || length == 0) {
        return fallback;
    }
    char* end = nullptr;
    const auto parsed = std::strtoull(value, &end, 10);
    return end == value ? fallback : parsed;
}

std::uint64_t trace_gx_stall_threshold_us() {
    static const std::uint64_t threshold =
        read_env_u64("GALAXY_TRACE_GX_STALL_US", 1000u);
    return threshold;
}

bool pso_prewarm_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, value, sizeof(value), "GALAXY_PSO_PREWARM") != 0 ||
            length <= 1) {
            return true;
        }
        return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
}

bool pipeline_library_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length,
                value,
                sizeof(value),
                "GALAXY_D3D12_PIPELINE_LIBRARY") != 0 ||
            length <= 1) {
            return true;
        }
        return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
}

std::uint64_t pso_prewarm_limit() {
    static const std::uint64_t limit =
        read_env_u64("GALAXY_PSO_PREWARM_LIMIT", 4096u);
    return limit == 0u ? 4096u : limit;
}

bool pso_prewarm_blocking_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length,
                value,
                sizeof(value),
                "GALAXY_PSO_PREWARM_BLOCKING") != 0 ||
            length <= 1) {
            return true;
        }
        return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
}

std::uint64_t pso_prewarm_blocking_timeout_ms() {
    static const std::uint64_t timeout =
        read_env_u64("GALAXY_PSO_PREWARM_BLOCKING_MS", 30000u);
    return timeout;
}

std::uint64_t pso_inflight_wait_ms() {
    static const std::uint64_t timeout =
        read_env_u64("GALAXY_PSO_INFLIGHT_WAIT_MS", 4u);
    return timeout;
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
    {"BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT, 0, 120, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
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

bool force_depth_always_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length, value, sizeof(value),
                   "GALAXY_GX_FORCE_DEPTH_ALWAYS") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
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
    // level 3 only lengthens the synchronous first-use miss on the render
    // thread (a level-3 build is slower per shader pair).
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

bool pso_fast_first_compile_enabled() {
    static const bool enabled = [] {
        char value[8]{};
        std::size_t length = 0;
        if (getenv_s(&length, value, sizeof(value), "GALAXY_PSO_FAST_FIRST_COMPILE") != 0 ||
            length == 0) {
            return true;
        }
        return value[0] != '0';
    }();
    return enabled;
}

// Translate a RenderStateKey + compiled shaders into a full PSO desc.
D3D12_GRAPHICS_PIPELINE_STATE_DESC make_gx_pso_desc_impl(
    ID3D12RootSignature* root_signature,
    const RenderStateKey& rs,
    ID3DBlob* vs,
    ID3DBlob* ps,
    ID3DBlob* line_geometry_shader,
    ID3DBlob* point_geometry_shader) {
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
    pd.pRootSignature = root_signature;
    pd.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};
    pd.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    pd.InputLayout = {kGxInputLayout,
                      static_cast<UINT>(std::size(kGxInputLayout))};

    // ---- Blend state from blend_bits -------------------------------------
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

    // ---- Rasterizer -------------------------------------------------------
    pd.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    // GX cull -> D3D12. GX screen space is Y-down relative to D3D NDC (the EFB viewport does
    // not flip), so GX "front" lands as a D3D back face and vice versa. Dolphin's D3D
    // backends use the same swap.
    // CullMode::All (rasterize nothing) cannot be one PSO state, so it maps to None. An empty
    // scissor would be the exact equivalent.
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

    // Depth compare table. Depth is reversed-Z relative to the GX z24 values (near=1, far=0;
    // see the projection negation in gx_backend.cpp fill_projection), so GX LESS maps to D3D
    // GREATER, GX LEQUAL to GREATER_EQUAL, and so on. Dolphin's D3D backends flip it the same way.
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
        pd.DepthStencilState.DepthFunc = force_depth_always_enabled()
            ? D3D12_COMPARISON_FUNC_ALWAYS
            : kGxDepthFunc[(zb >> 1) & 0x7u];
        pd.DepthStencilState.DepthWriteMask = (zb & (1u << 4))
            ? D3D12_DEPTH_WRITE_MASK_ALL
            : D3D12_DEPTH_WRITE_MASK_ZERO;
    }

    pd.SampleMask            = UINT_MAX;
    switch (static_cast<D3D12_PRIMITIVE_TOPOLOGY_TYPE>(
        rs.primitive_topology)) {
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
        pd.PrimitiveTopologyType =
            static_cast<D3D12_PRIMITIVE_TOPOLOGY_TYPE>(
                rs.primitive_topology);
        break;
    default:
        pd.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        break;
    }
    pd.NumRenderTargets      = 1;
    pd.RTVFormats[0]         = kEfbColorFormat;
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
    pd.SampleDesc.Count      = 1;

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
    ID3DBlob* point_geometry_shader) {
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC pd =
        make_gx_pso_desc_impl(
            root_signature,
            rs,
            vs,
            ps,
            line_geometry_shader,
            point_geometry_shader);

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
    return name;
}

}  // anonymous namespace

D3D12_GRAPHICS_PIPELINE_STATE_DESC make_gx_pso_desc(
    ID3D12RootSignature* root_signature,
    const RenderStateKey& render_state,
    ID3DBlob* vertex_shader,
    ID3DBlob* pixel_shader,
    ID3DBlob* line_geometry_shader,
    ID3DBlob* point_geometry_shader) {
    return make_gx_pso_desc_impl(
        root_signature,
        render_state,
        vertex_shader,
        pixel_shader,
        line_geometry_shader,
        point_geometry_shader);
}

// ===========================================================================
// PsoCompletionQueue: Vyukov MPSC intrusive list
// ===========================================================================
//
// The queue uses a sentinel node so the consumer never needs to call
// new/delete in the steady-state pop path.
//
// Producer (push, any thread):
//   1. Allocate a Node on the heap.
//   2. Set node->next = nullptr.
//   3. Swing head_ to node (atomic exchange), get prev.
//   4. Store node into prev->next (the only store the consumer waits on).
//
// Consumer (try_pop, render thread only):
//   Tail cursor points to the sentinel.  If sentinel->next is null the queue is
//   empty.  Otherwise advance tail_ = sentinel->next, making the new tail the
//   new sentinel, and return the old tail's item.

struct PsoCompletionQueue::Node {
    PsoCompletionQueue::Item item;
    std::atomic<Node*> next{nullptr};
};

PsoCompletionQueue::PsoCompletionQueue() {
    // Allocate the sentinel on the heap.
    // tail_ always points to the current sentinel node (heap-owned).
    Node* sentinel = new Node{};
    head_.store(sentinel, std::memory_order_relaxed);
    tail_ = sentinel;
}

PsoCompletionQueue::~PsoCompletionQueue() {
    // Drain any remaining nodes (should be none after shutdown joins workers).
    Item discard;
    while (try_pop(discard)) {}
    // tail_ is the last sentinel; always heap-allocated, always delete it.
    delete tail_;
}

void PsoCompletionQueue::push(Item item) {
    // Allocated on the heap. The consumer frees it after popping.
    Node* node = new Node{std::move(item), nullptr};
    // Swing head_ to the new node; prev is whoever was there before.
    Node* prev = head_.exchange(node, std::memory_order_acq_rel);
    // Link: store into prev->next.  The consumer spins on this store if it
    // races the exchange, but that window is tiny.
    prev->next.store(node, std::memory_order_release);
}

bool PsoCompletionQueue::try_pop(Item& out) {
    // tail_ is the sentinel (or the last consumed node acting as sentinel).
    Node* next = tail_->next.load(std::memory_order_acquire);
    if (next == nullptr) {
        return false;  // queue is empty
    }
    // Advance the sentinel: next becomes the new sentinel; delete the old one.
    out = std::move(next->item);
    delete tail_;
    tail_ = next;
    return true;
}

// ===========================================================================
// PipelineCache
// ===========================================================================

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

    stopping_.store(false, std::memory_order_relaxed);


    // Create the cache directory if it does not exist.
    std::error_code ec;
    std::filesystem::create_directories(cache_dir_, ec);
    // A failure here is non-fatal. Disk persistence is simply unavailable.

    // Uber pipelines (GALAXY_GX_UBER): 1 = hybrid (default), force = every
    // draw, 0 = off (first-use misses compile synchronously, see get()).
    {
        char value[16]{};
        std::size_t length = 0;
        const bool present = getenv_s(&length, value, sizeof(value), "GALAXY_GX_UBER") == 0 &&
            length > 1u;
        const std::string mode = present ? std::string(value) : std::string("1");
        uber_mode_ = mode == "force" ? UberMode::Force
                   : (mode == "0" || mode == "off") ? UberMode::Off
                   : UberMode::Hybrid;
    }
    uber_blobs_ready_.store(false, std::memory_order_relaxed);
    if (uber_mode_ == UberMode::Force) {
        compile_uber_blobs();
    } else if (uber_mode_ == UberMode::Hybrid) {
        uber_compile_thread_ = std::thread([this] {
            galaxy::host::name_current_thread(L"Nebula Uber Compile");
            (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            compile_uber_blobs();
        });
    }

    // Opt-in (GALAXY_SHADER_SEED_DIRECTORY): fills a fresh user cache from a validated seed
    // pair before the loaders below read it. Otherwise a no-op.
    import_fresh_cache_seed();

    // Warm valid saved stages. This cannot prepare genuinely new shader
    // configurations or recover stages that never persisted successfully.
    load_disk_cache();
    load_pso_key_cache();
    if (pipeline_library_enabled()) {
        load_pipeline_library();
    }
    const std::uint64_t prewarm_jobs = enqueue_cached_pso_prewarm(worker_count);
    if (prewarm_jobs != 0 && pso_prewarm_blocking_enabled()) {
        wait_for_cached_pso_prewarm(prewarm_jobs);
    }

    // The bounded startup record reports preparation only, not future coverage.
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
    // Join the independent disk owner even if later completion draining throws.
    finish_disk_flush(true);
    // Signal workers to stop.
    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        stopping_.store(true, std::memory_order_relaxed);
        // Queued prewarm is optional. Only already running work must retire.
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
    if (uber_mode_ != UberMode::Off) {
        std::cerr << "[uber-pipeline] mode="
                  << (uber_mode_ == UberMode::Force ? "force" : "hybrid")
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

// Share of a first-use stall that was FXC shader compilation rather than the
// driver's pipeline build, so the split is readable without dividing two fields
// by hand. Integer percent; 0 when nothing was timed.
[[nodiscard]] std::uint64_t blob_share_percent(
    std::uint64_t blob_us,
    std::uint64_t total_us) noexcept {
    return total_us == 0u ? 0u : (blob_us * 100u) / total_us;
}

// ---------------------------------------------------------------------------
// get: render thread only
// ---------------------------------------------------------------------------

ID3D12PipelineState* PipelineCache::get(
    const PsoKey& key,
    const PixelShaderKey& ps_key,
    const VertexShaderKey& vs_key) {

    if (worker_failed_.load(std::memory_order_acquire)) [[unlikely]] {
        std::lock_guard<std::mutex> lock(job_mutex_);
        if (worker_error_) std::rethrow_exception(worker_error_);
    }
    last_get_used_uber_ = false;
    if (uber_mode_ == UberMode::Force) [[unlikely]] {
        if (ID3D12PipelineState* uber = uber_pipeline(
                key.render_state, uber_ps_variant(ps_key), /*create_now=*/true)) {
            last_get_used_uber_ = true;
            ++uber_resolutions_;
            return uber;
        }
    }
    // 0. One-entry memo of the last successful resolution, checked before the map probe.
    // A busy frame alternates between a few PsoKeys (a material and its blend/depth
    // variants), so the same key recurs within a few draws. published_ never loses an entry
    // during a session, so a memo hit returns what the probe would return.
    //
    // The memo compares the hash first, then the key. PsoKey::hash() mixes 32 bytes into
    // 64 bits, and a mismatching hash proves the keys differ, so the byte comparison runs
    // only on a hash match. A hash collision falls through to the exact comparison and
    // cannot return the wrong pipeline.
    const std::uint64_t requested_hash = key.hash();
    if (last_hit_pipeline_ != nullptr &&
        last_hit_key_.hash == requested_hash &&
        last_hit_key_.key == key) {
        ++specialized_hits_;
        return last_hit_pipeline_;
    }
    // Hash the key once per request and reuse it for the memo and both map probes (published_
    // and in_flight_). The renderer calls this whenever a shader or render-state input
    // changes, which can be many times per frame.
    const MemoizedPsoKey memoized{key, requested_hash};
    // 1. Check the published specialized map (render-thread-only, no lock).
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
    const std::uint64_t key_hash = memoized.hash;
    if (in_flight_.find(memoized) != in_flight_.end()) {
        // A worker that finished after begin_frame()'s drain left its result on the lock-free
        // completion queue, which the probe above cannot see. Drain here. get() is render-thread
        // only, like the frame drain, so an already-finished job is taken instead of waited on.
        // Otherwise the render thread waits a full quantum for every state change whose compile
        // has already finished.
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
            if (uber_mode_ == UberMode::Hybrid) {
                if (ID3D12PipelineState* uber = uber_pipeline(
                        key.render_state, uber_ps_variant(ps_key), /*create_now=*/false)) {
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

    // 1b. Hybrid uber: when this render state's uber pipeline exists, draw
    // with it and compile the specialization on a worker. Otherwise
    // uber_pipeline() queued that uber PSO for later misses and this one
    // compiles synchronously below.
    if (uber_mode_ == UberMode::Hybrid) {
        const unsigned variant = uber_ps_variant(ps_key);
        if (ID3D12PipelineState* uber = uber_pipeline(
                key.render_state, variant, /*create_now=*/false)) {
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

    // 2. Compile synchronously on first sight. Returning the uber fallback while an async
    // compile is in flight skips every new material for those frames, which shows as pop-in
    // at scene changes. A one-time hitch per shader is better than missing geometry. The DXBC
    // cache (in memory by shader hash, plus shaders.bin on disk) limits FXC to the first
    // compile of each shader. Render-state variants reuse those blobs and pay only for the
    // driver PSO build. The trade-off follows Dolphin's hybrid ubershader design.
    ++uber_fallbacks_;

    const auto miss_start = std::chrono::steady_clock::now();
    const ShaderPairKey shader_pair_hash{key.vs_hash, key.ps_hash};
    const bool blob_cache_hit =
        shader_blobs_.find(shader_pair_hash) != shader_blobs_.end();
    const bool vs_cache_hit = vertex_shader_blobs_.contains(key.vs_hash);
    const bool ps_cache_hit = pixel_shader_blobs_.contains(key.ps_hash);
    const bool known_key = persisted_pso_keys_.contains(key);
    // Fast first use when a worker can re-optimize afterwards. An unoptimized FXC build
    // publishes now and is faster than level 1. A worker then builds the level-1 pair and its
    // pipeline and replaces the fast one.
    bool fast_compiled = false;
    const bool fast_first = pso_fast_first_compile_enabled() &&
        !stopping_.load(std::memory_order_relaxed);
    const ShaderPair* blobs =
        get_or_compile_blobs(key, ps_key, vs_key, fast_first, &fast_compiled);
    const auto blobs_end = std::chrono::steady_clock::now();

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
    const auto pso_start = std::chrono::steady_clock::now();
    if (blobs != nullptr && blobs->vs && blobs->ps) {
        pso = create_or_load_pso(
            key, blobs->vs.Get(), blobs->ps.Get(), /*store_in_library=*/!fast_compiled);
    }
    const auto pso_end = std::chrono::steady_clock::now();
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
        // First-use path just resolved (or loaded) this key synchronously, so it
        // is the most likely key to be asked for again immediately.
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
    const std::uint64_t total_us = elapsed_us(miss_start, pso_end);
    if (trace_gx_stalls_enabled() &&
        (total_us >= trace_gx_stall_threshold_us() || raw == nullptr)) {
        std::ostringstream message;
        message << "[gx-pso-miss] key=0x" << std::hex << key_hash
                  << " vs=0x" << key.vs_hash
                  << " ps=0x" << key.ps_hash
                  << " blend=0x" << key.render_state.blend_bits
                  << " z=0x" << key.render_state.zmode_bits
                  << std::dec
                  << " topo="
                  << static_cast<unsigned>(
                         key.render_state.primitive_topology)
                  << " cull=" << static_cast<unsigned>(key.render_state.cull)
                  << " pixfmt=" << static_cast<unsigned>(key.render_state.pixfmt)
                  << " blob-cache-hit=" << (blob_cache_hit ? 1 : 0)
                  << " vs-cache-hit=" << (vs_cache_hit ? 1 : 0)
                  << " ps-cache-hit=" << (ps_cache_hit ? 1 : 0)
                  << " key-known=" << (known_key ? 1 : 0)
                  << " blob-us=" << elapsed_us(miss_start, blobs_end)
                  << " pso-us=" << elapsed_us(pso_start, pso_end)
                  << " total-us=" << total_us
                  << " blob-share-pct="
                  << blob_share_percent(
                         elapsed_us(miss_start, blobs_end), total_us)
                  // A miss with both stages absent is a new material. A miss with one stage absent means the
                  // pair record was not found for a shader that is already resident.
                  << " stages-missing="
                  << ((blob_cache_hit ? 0u : 1u) +
                      (vs_cache_hit ? 0u : 1u) + (ps_cache_hit ? 0u : 1u))
                  << " success=" << (raw != nullptr ? 1 : 0)
                  << " fast=" << (fast_compiled ? 1 : 0)
                  << '\n';
        std::cerr << message.str();
    }
    return raw;
}

// ---------------------------------------------------------------------------
// drain_completions: render thread only, called from begin_frame()
// ---------------------------------------------------------------------------

void PipelineCache::drain_completions() {
    // The last-hit memo in get() relies on published_ gaining no entry between the memo write
    // and its read. This is the only place outside get() that adds one, so it is the only place
    // that drops the memo. It runs unconditionally, so the assert holds even when the queue is
    // empty. The cost is one null store per frame.
    clear_last_hit_memo();
    if (uber_mode_ == UberMode::Hybrid && !uber_prewarm_enqueued_ &&
        uber_blobs_ready_.load(std::memory_order_acquire)) {
        uber_prewarm_enqueued_ = true;
        enqueue_uber_prewarm();
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
        // The completed job has no remaining worker/queue owner. Retire its
        // marker before publication or persistence allocations can throw,
        // otherwise a caught retry waits for a completion already consumed.
        // Both maps are render-thread-owned, so no get can observe this gap.
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
    const std::uint64_t key_hash = memoized.hash;
    // A queued job can be claimed by first use. A running job has one owner, so timing out must
    // never cause a duplicate driver creation for the same key.
    //
    // The two cases need different handling. A job still in `jobs_` has no owner yet, so the
    // render thread may take it and build the PSO itself (see the inline attempt below). A job a
    // worker has already removed from the queue is in progress and must be waited on, or two
    // threads would create the same pipeline concurrently.
    CompileJob inline_job{};
    bool claimed_queued_job = false;
    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        const auto queued = std::find_if(jobs_.begin(), jobs_.end(),
            [&](const CompileJob& job) { return job.key == key; });
        if (queued != jobs_.end()) {
            // Transfer ownership before releasing the worker's queue lock.
            // Merely observing the entry permits a worker to take it while
            // this thread is already creating the same pipeline.
            inline_job = std::move(*queued);
            jobs_.erase(queued);
            claimed_queued_job = true;
        }
    }
    // An owned queued job needs no worker wait. Cached DXBC avoids shader
    // translation here, but a driver-library miss can still create a PSO.
    // Running jobs are not claimed and retain the completion wait below.
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
            // No worker owns this removed job. Retire the marker and let
            // get() perform its full-key synchronous attempt; never wait for
            // a completion that cannot arrive or return an incorrect fallback.
            in_flight_.erase(memoized);
            return false;
        } catch (...) {
            // Creation, publication and persistence bookkeeping may throw.
            // The owned queue entry is gone on every exit, so its marker must
            // not leave a later request waiting on nonexistent work.
            in_flight_.erase(memoized);
            throw;
        }
    }
    const std::uint64_t timeout_ms = std::max<std::uint64_t>(
        30000u, std::min<std::uint64_t>(pso_inflight_wait_ms(), 300000u));
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

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now - start).count();
        if (static_cast<std::uint64_t>(elapsed_ms) >= timeout_ms) {
            if (trace_gx_stalls_enabled() &&
                elapsed_us(start, now) >= trace_gx_stall_threshold_us()) {
                std::cerr << "[gx-pso-inflight-wait] key=0x" << std::hex
                          << key_hash << std::dec
                          << " elapsed-us=" << elapsed_us(start, now)
                          << " timeout-ms=" << timeout_ms << '\n';
            }
            throw std::runtime_error("timed out waiting for a running GX pipeline job");
        }

        wait_for_worker_completion(observed, std::chrono::milliseconds(
            timeout_ms - static_cast<std::uint64_t>(elapsed_ms)));
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
    // The publisher advances the sequence under this same mutex after linking
    // the completed item. A notification before parking is therefore retained;
    // one after the predicate check cannot pass the atomic unlock-and-wait.
    (void)completion_cv_.wait_for(lock, timeout, ready);
    if (worker_error_) std::rethrow_exception(worker_error_);
    if (completion_sequence_ == observed &&
        stopping_.load(std::memory_order_relaxed)) {
        throw std::runtime_error("GX pipeline workers stopped before required completion");
    }
}

// ---------------------------------------------------------------------------
// get_or_compile_blobs: DXBC pair lookup and synchronous FXC
// ---------------------------------------------------------------------------

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
    // A pair reached here with both stages resident came from shaders.bin or an earlier compile
    // for this exact (vs_hash, ps_hash).
    if (!need_vs && !need_ps) {
        ++blobs_from_cache_;
    } else {
        ++shader_pair_compiles_;
    }
    // Times only the FXC work, not the map probes above. This is the term the stall attribution uses.
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
        // Only this render-thread owner mutates these containers. Remove the
        // identity just appended above; valid stages and earlier pending pairs
        // survive, and a retry can recombine those stages without compilation.
        if (!shader_cache_at_capacity_) unsaved_blobs_.pop_back();
        throw;
    }
}

// ---------------------------------------------------------------------------
// Disk cache: shaders.bin
//   header: u32 magic 'GXSC', u32 version
//   record: u64 vs_hash, u64 ps_hash, u32 vs_size, u32 ps_size,
//           vs DXBC bytes, ps DXBC bytes
// ---------------------------------------------------------------------------

namespace {
constexpr std::uint32_t kShaderCacheMagic   = 0x43535847u;  // "GXSC"
// The version gates generator output, not only the file format. Bump it whenever
// generate_vs or generate_ps emits different HLSL for the same key. Otherwise stale DXBC
// with old semantics would be served for hash-identical keys.
constexpr std::uint32_t kShaderCacheVersion = 35u;
constexpr std::uint32_t kPsoKeyCacheMagic = 0x43504B47u;  // "GKPC"
// Keys are valid only while the shader cache that defines their vs_hash/ps_hash stages
// is current. enqueue_cached_pso_prewarm skips any key whose stages find_cached_shader_stages
// rejects. psos.bin is rejected and rebuilt whenever the shader semantics it references
// change, so the dependency is explicit. Losing these keys costs only the prewarm of keys
// not yet seen in this session.
constexpr std::uint32_t kPsoKeyCacheVersion = 0x100u + kShaderCacheVersion;
constexpr std::uint32_t kPipelineLibraryMagic = 0x4C505847u; // "GXPL"
constexpr std::uint32_t kPipelineLibraryVersion = 8u;
constexpr std::uint64_t kMaxPipelineLibraryBytes = 64ull << 20;
constexpr std::uint64_t kMaxShaderCacheBytes = 128ull << 20;
constexpr std::uint32_t kMaxCacheRecords = 65536u;
// Both disk caches are append-only running totals. New configurations are appended in
// bounded batches, and the file is rewritten to cover the in-memory coverage once per 64
// newly learned configurations, not on every flush.
constexpr std::size_t kCacheCoverageAppendBatch = 64u;

bool valid_pso_key(const PsoKey& key) {
    const auto& state = key.render_state;
    return state.primitive_topology >= 1u && state.primitive_topology <= 3u &&
        state.cull <= 3u && state.pixfmt <= 7u &&
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


// The two append-record formats retain their valid byte prefix. Persistence
// writes a replacement beside the original so an interrupted/failed append
// cannot poison the next attempt, including a retry in this process.
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
    // Ownership rules, in one place:
    //   `source` is owned from here and closed on every exit.
    //   The temp file is owned only once this call has CREATED or TRUNCATED it,
    //     and is removed when it was not published - not merely when the write
    //     returned false, so an unexpected exception still cleans up.
    //   A temp file this call never opened is never deleted, so a concurrent
    //     writer's temp file survives.
    FILE* output = nullptr;
    bool output_owned = false;
    bool published = false;
    // Declared before the temp-file owner below, which removes it on failure and
    // therefore must outlive it.
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
    // Safe to start true: `source` is null only when no cache file existed, and
    // `find_cache_prefix` then leaves `prefix_bytes` and `retained_records` at
    // zero, so the copy loop and the seek below are both skipped.
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
    // The copy loop leaves `source` at the end of the prefix. That is EOF when the prefix is
    // the whole file, the normal repair case. The records write_pending needs lie behind that
    // position, so seek back to the first one. Offset 8 is the first record whenever the prefix
    // is non-empty: find_cache_prefix keeps only whole records after an 8-byte header. With an
    // empty prefix the callback gets no records and never reads `source`, so the seek is a no-op.
    // It is done here because both callbacks share this position.
    if (written && retained_records != 0u) {
        written = source != nullptr && _fseeki64(source, 8, SEEK_SET) == 0;
    }
    // An allocation failure aborts this save, not the caller or the process. PipelineCache is
    // optional, so the save returns false and the pending data stays pending. Only allocation
    // and length errors are caught.
    if (written) {
        try {
            written = write_pending(output, source, retained_records);
        } catch (const std::bad_alloc&) {
            written = false;
        } catch (const std::length_error&) {
            written = false;
        }
    }
    // Close both handles before the replacement. MoveFileExW must not run while the CRT still
    // holds the source open. The guard above skips a handle that was nulled without being
    // closed, so close explicitly, check the result, and only then null the handle. The guards
    // remain for exits that bypass this tail.
    const bool closed_output = close_cache_output(output);
    output = nullptr;
    bool closed_source = true;
    if (source != nullptr) {
        closed_source = std::fclose(source) == 0;
        source = nullptr;
    }
    // The two close results join `written`, so the return value keeps reporting
    // whether the record set was actually committed. The final return is false on
    // its own if the replacement fails, which leaves `published` false and so
    // still removes the temp file.
    if (!closed_output || !closed_source) written = false;
    if (written && MoveFileExW(
            tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        published = true;
        return true;
    }
    written = false;
    return false;
}

// ---------------------------------------------------------------------------
// Optional seed import (GALAXY_SHADER_SEED_DIRECTORY=<dir>).
//
// <dir> holds a shaders.bin and psos.bin pair written with the current cache versions.
// It is imported only into a fresh user cache (neither user file present), byte for byte,
// after every record passes the same checks the loaders apply. It is not read through
// load_disk_cache() or load_pso_key_cache(): those set repair state for the user's files,
// which does not apply to a read-only source. The regular loaders validate the copy again.
//
// The checksums prove the bytes are intact, not that the DXBC matches the hash it is filed
// under. Seed provenance (generator fingerprint, release version) is a packaging gate and is
// not checked here.
//
// Commit protocol. Every step is non-fatal; any failure falls back to compile-on-miss.
//   1. Open both seed files with write sharing denied, so validation and copy see the same bytes.
//   2. Take the writer lease on BOTH user files (non-blocking; busy means skip).
//   3. Re-check under the leases that neither user file exists.
//   4. Copy each seed file into its own CREATE_NEW temp file beside the target; flush; close.
//   5. Publish shaders.bin first, then psos.bin, with MoveFileExW and no REPLACE_EXISTING.
//      The move fails if the target appeared, so a concurrent winner is never overwritten.
// A failure between the two publishes leaves a valid shaders.bin and no psos.bin. An ordinary
// run is in that state after it learns shaders but has not saved PSO keys yet; flush_disk()
// creates psos.bin later. A committed shaders.bin is never deleted. Only temp files this call
// created are removed. A crash can strand a "<name>.seed-<pid>-<tick>-<n>.tmp", which nothing
// reads and which is safe to delete.
// ---------------------------------------------------------------------------

enum class SeedCheck : std::uint8_t { Ok, Invalid, Resource };

SeedCheck seed_file_size(FILE* f, std::uint64_t limit, std::uint64_t& size) {
    if (_fseeki64(f, 0, SEEK_END) != 0) return SeedCheck::Resource;
    const __int64 end = _ftelli64(f);
    if (end < 0 || _fseeki64(f, 0, SEEK_SET) != 0) return SeedCheck::Resource;
    size = static_cast<std::uint64_t>(end);
    return size <= limit ? SeedCheck::Ok : SeedCheck::Invalid;
}

// The whole file must be complete current-version records: no prefix is trusted
// when a later record or trailing bytes are bad.
SeedCheck validate_seed_shaders(
    FILE* f, std::uint64_t& size,
    std::unordered_set<std::uint64_t>& vs_hashes,
    std::unordered_set<std::uint64_t>& ps_hashes) {
    if (const auto sized = seed_file_size(f, kMaxShaderCacheBytes, size);
        sized != SeedCheck::Ok) {
        return sized;
    }
    std::uint32_t magic = 0u, version = 0u;
    if (size < 8u || std::fread(&magic, sizeof(magic), 1, f) != 1 ||
        std::fread(&version, sizeof(version), 1, f) != 1) {
        return std::ferror(f) != 0 ? SeedCheck::Resource : SeedCheck::Invalid;
    }
    if (magic != kShaderCacheMagic || version != kShaderCacheVersion) {
        return SeedCheck::Invalid;
    }
    std::uint32_t records = 0u;
    for (;;) {
        const __int64 position = _ftelli64(f);
        if (position < 0) return SeedCheck::Resource;
        if (static_cast<std::uint64_t>(position) == size) break;
        if (records >= kMaxCacheRecords) return SeedCheck::Invalid;
        std::uint64_t vs_hash = 0u, ps_hash = 0u;
        Microsoft::WRL::ComPtr<ID3DBlob> vs, ps;
        const auto read = read_shader_record(f, vs_hash, ps_hash, vs, ps);
        if (read == ShaderRecordRead::Resource) return SeedCheck::Resource;
        if (read != ShaderRecordRead::Ok) return SeedCheck::Invalid;
        const __int64 end = _ftelli64(f);
        if (end <= position || static_cast<std::uint64_t>(end) > size) {
            return SeedCheck::Invalid;
        }
        vs_hashes.insert(vs_hash);
        ps_hashes.insert(ps_hash);
        ++records;
    }
    return records != 0u ? SeedCheck::Ok : SeedCheck::Invalid;
}

// Every key must be a valid PsoKey whose two stages are present in the seed's
// shaders.bin; a key the prewarm would skip is not allowed to ride along.
SeedCheck validate_seed_keys(
    FILE* f, std::uint64_t& size,
    const std::unordered_set<std::uint64_t>& vs_hashes,
    const std::unordered_set<std::uint64_t>& ps_hashes) {
    const std::uint64_t limit =
        8u + static_cast<std::uint64_t>(kMaxCacheRecords) * sizeof(PsoKey);
    if (const auto sized = seed_file_size(f, limit, size);
        sized != SeedCheck::Ok) {
        return sized;
    }
    std::uint32_t magic = 0u, version = 0u;
    if (size < 8u || std::fread(&magic, sizeof(magic), 1, f) != 1 ||
        std::fread(&version, sizeof(version), 1, f) != 1) {
        return std::ferror(f) != 0 ? SeedCheck::Resource : SeedCheck::Invalid;
    }
    if (magic != kPsoKeyCacheMagic || version != kPsoKeyCacheVersion) {
        return SeedCheck::Invalid;
    }
    const std::uint64_t payload = size - 8u;
    if (payload == 0u || payload % sizeof(PsoKey) != 0u) {
        return SeedCheck::Invalid;
    }
    for (std::uint64_t index = 0; index < payload / sizeof(PsoKey); ++index) {
        PsoKey key{};
        if (std::fread(&key, sizeof(key), 1, f) != 1) {
            return std::ferror(f) != 0 ? SeedCheck::Resource : SeedCheck::Invalid;
        }
        if (!valid_pso_key(key) || !vs_hashes.contains(key.vs_hash) ||
            !ps_hashes.contains(key.ps_hash)) {
            return SeedCheck::Invalid;
        }
    }
    return SeedCheck::Ok;
}

// A temp file this object CREATED. Removed on destruction unless published, so a
// file this call never created (a concurrent importer's) is never touched.
class SeedTempFile {
public:
    SeedTempFile() = default;
    SeedTempFile(const SeedTempFile&) = delete;
    SeedTempFile& operator=(const SeedTempFile&) = delete;
    ~SeedTempFile() {
        close();
        if (created_ && !published_) {
            std::error_code ec;
            std::filesystem::remove(path_, ec);
        }
    }

    bool create(const std::filesystem::path& target) {
        for (unsigned attempt = 0; attempt < 8u; ++attempt) {
            path_ = target;
            path_ += L".seed-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(GetTickCount64()) + L"-" +
                std::to_wstring(attempt) + L".tmp";
            handle_ = CreateFileW(
                path_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle_ != INVALID_HANDLE_VALUE) {
                created_ = true;
                return true;
            }
            const DWORD error = GetLastError();
            if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS) {
                return false;
            }
        }
        return false;
    }

    bool fill_and_close(FILE* source, std::uint64_t size) {
        bool ok = handle_ != INVALID_HANDLE_VALUE && _fseeki64(source, 0, SEEK_SET) == 0;
        std::array<unsigned char, 64u << 10> buffer{};
        std::uint64_t remaining = size;
        while (ok && remaining != 0u) {
            const auto chunk = static_cast<std::size_t>(
                std::min<std::uint64_t>(remaining, buffer.size()));
            DWORD written = 0;
            ok = std::fread(buffer.data(), 1, chunk, source) == chunk &&
                WriteFile(handle_, buffer.data(), static_cast<DWORD>(chunk),
                          &written, nullptr) != FALSE &&
                written == chunk;
            remaining -= chunk;
        }
        ok = ok && FlushFileBuffers(handle_) != FALSE;
        const bool closed = close();
        return ok && closed;
    }

    // Create-new publish: fails if `target` exists.
    bool publish(const std::filesystem::path& target) {
        if (created_ && !published_ &&
            MoveFileExW(path_.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
            published_ = true;
        }
        return published_;
    }

private:
    bool close() {
        if (handle_ == INVALID_HANDLE_VALUE) return true;
        const bool closed = CloseHandle(handle_) != FALSE;
        handle_ = INVALID_HANDLE_VALUE;
        return closed;
    }

    std::filesystem::path path_;
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    bool created_ = false;
    bool published_ = false;
};

// Returns a short static outcome string for the single log line.
const char* import_seed_pair(
    const std::filesystem::path& seed_dir,
    const std::filesystem::path& cache_dir) {
    const std::filesystem::path user_shaders = cache_dir / L"shaders.bin";
    const std::filesystem::path user_psos = cache_dir / L"psos.bin";
    std::error_code ec;
    const bool shaders_exist = std::filesystem::exists(user_shaders, ec);
    if (ec) return "user-cache-unreadable";
    const bool psos_exist = std::filesystem::exists(user_psos, ec);
    if (ec) return "user-cache-unreadable";
    if (shaders_exist || psos_exist) return "not-fresh";

    FILE* seed_shaders = _wfsopen(
        (seed_dir / L"shaders.bin").c_str(), L"rb", _SH_DENYWR);
    FILE* seed_psos = _wfsopen(
        (seed_dir / L"psos.bin").c_str(), L"rb", _SH_DENYWR);
    const galaxy::ScopeExit close_seed([&]() noexcept {
        if (seed_shaders != nullptr) std::fclose(seed_shaders);
        if (seed_psos != nullptr) std::fclose(seed_psos);
    });
    if (seed_shaders == nullptr || seed_psos == nullptr) {
        return "seed-missing-or-busy";
    }

    std::unordered_set<std::uint64_t> vs_hashes, ps_hashes;
    std::uint64_t shaders_size = 0u, psos_size = 0u;
    SeedCheck checked =
        validate_seed_shaders(seed_shaders, shaders_size, vs_hashes, ps_hashes);
    if (checked == SeedCheck::Ok) {
        checked = validate_seed_keys(seed_psos, psos_size, vs_hashes, ps_hashes);
    }
    if (checked == SeedCheck::Invalid) return "seed-invalid";
    if (checked == SeedCheck::Resource) return "seed-unavailable";

    CacheWriterLease shaders_lease(user_shaders);
    CacheWriterLease psos_lease(user_psos);
    if (!shaders_lease || !psos_lease) return "cache-busy";
    const bool shaders_now = std::filesystem::exists(user_shaders, ec);
    if (ec) return "user-cache-unreadable";
    const bool psos_now = std::filesystem::exists(user_psos, ec);
    if (ec) return "user-cache-unreadable";
    if (shaders_now || psos_now) return "not-fresh-after-lock";

    SeedTempFile shaders_temp, psos_temp;
    if (!shaders_temp.create(user_shaders) || !psos_temp.create(user_psos)) {
        return "temp-create-failed";
    }
    if (!shaders_temp.fill_and_close(seed_shaders, shaders_size) ||
        !psos_temp.fill_and_close(seed_psos, psos_size)) {
        return "temp-write-failed";
    }
    if (!shaders_temp.publish(user_shaders)) return "publish-failed";
    if (!psos_temp.publish(user_psos)) return "imported-shaders-only";
    return "imported";
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

void PipelineCache::import_fresh_cache_seed() noexcept {
    // Absent variable: the old path, with no filesystem access at all.
    wchar_t* value = nullptr;
    std::size_t length = 0u;
    if (_wdupenv_s(&value, &length, L"GALAXY_SHADER_SEED_DIRECTORY") != 0 ||
        value == nullptr) {
        std::free(value);
        return;
    }
    const char* outcome = "exception";
    try {
        const std::filesystem::path seed_dir(value);
        std::free(value);
        value = nullptr;
        if (seed_dir.empty()) return;
        outcome = import_seed_pair(seed_dir, cache_dir_);
    } catch (...) {
        // Optional cache: any failure, including allocation, falls back to the
        // ordinary compile path with the user's files untouched or complete.
    }
    std::free(value);
    try {
        std::cerr << "[pipeline-cache-seed] outcome=" << outcome << '\n';
    } catch (...) {
    }
}

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
        // Exclude stale/foreign shader semantics immediately. The replacement
        // writer also validates a retained file prefix before persisting
        // new records. Invalid input is left untouched until replacement succeeds.
        shader_cache_repair_pending_ = true;
        if (trace_gx_stalls_enabled()) {
            std::ostringstream message;
            message << "[shader-cache-rejected] reason=header-or-version magic=" << magic
                    << " version=" << version << " expected-version=" << kShaderCacheVersion << '\n';
            std::cerr << message.str();
        }
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
    // Keep completed residents and valid independent stages after resource
    // failure; the unconsumed tail does not mean corruption.
    shader_cache_repair_pending_ = !resource_failed && cache_has_tail(f, last_complete);
    // Coverage the retained file prefix actually holds. Records the parse
    // rejected past a corrupt tail are not covered, so the next flush appends
    // them again; the retained prefix is still what gets copied forward.
    cached_shader_records_ = loaded;
    if (loaded != 0 && trace_gx_stalls_enabled()) {
        std::cerr << "[PipelineCache] loaded " << loaded
                  << " DXBC pairs from shader cache\n";
    }
}

// ---------------------------------------------------------------------------
// Disk cache: psos.bin
//   header: u32 magic 'GKPC', u32 version (= kPsoKeyCacheVersion)
//   record: PsoKey, sizeof(PsoKey) bytes, bare and unpadded
// The version is derived from kShaderCacheVersion; see the note there.
// ---------------------------------------------------------------------------

void PipelineCache::load_pso_key_cache() {
    const std::filesystem::path path = cache_dir_ / "psos.bin";
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || f == nullptr) {
        return;
    }
    const galaxy::ScopeExit close_input([&]() noexcept { std::fclose(f); });

    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    // The record size is part of the on-disk format: psos.bin is a bare, unpadded run of PsoKey
    // records, so changing PsoKey misparses an existing file unless the version moves too.
    //
    // The size is guarded next to the type. shader_keys.h asserts sizeof(PsoKey) == 32 and that
    // the type has no padding, which is the real invariant. A second literal comparison here
    // would only repeat it. This file guards that each record is one bare PsoKey, that is, the
    // type is trivially copyable and safe to fread/fwrite as a raw run.
    static_assert(std::is_trivially_copyable_v<PsoKey>,
        "psos.bin is a bare run of PsoKey records and is read/written with "
        "fread/fwrite, so PsoKey must stay trivially copyable; changing it "
        "requires bumping kPsoKeyCacheVersion so stale files are rejected rather "
        "than misparsed");
    // `load_pso_key_cache` / `find_cache_prefix` read exactly `magic` + `version`
    // before the records, so the header is two 32-bit words by construction.
    static_assert(sizeof(magic) == 4u && sizeof(version) == 4u,
        "psos.bin header is two 32-bit words");
    if (std::fread(&magic, sizeof(magic), 1, f) != 1 ||
        std::fread(&version, sizeof(version), 1, f) != 1 ||
        magic != kPsoKeyCacheMagic || version != kPsoKeyCacheVersion) {
        if (std::ferror(f) != 0) return;
        pso_cache_repair_pending_ = true;
        if (trace_gx_stalls_enabled()) {
            std::ostringstream message;
            message << "[pso-key-cache-rejected] reason=header-or-version magic="
                    << magic << " version=" << version
                    << " expected-version=" << kPsoKeyCacheVersion
                    << " shader-version=" << kShaderCacheVersion << '\n';
            std::cerr << message.str();
        }
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
    // Coverage the retained file prefix actually holds; see load_disk_cache.
    cached_pso_records_ = persisted_pso_keys_.size();

    if (!warm_pso_keys_.empty() && trace_gx_stalls_enabled()) {
        std::cerr << "[PipelineCache] loaded " << warm_pso_keys_.size()
                  << " PSO keys from cache\n";
    }
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
        if (trace_gx_stalls_enabled()) {
            std::cerr << "[PipelineCache] D3D12 pipeline library unsupported\n";
        }
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
        if (trace_gx_stalls_enabled()) {
            std::cerr << "[PipelineCache] discarded stale D3D12 pipeline library"
                         " HRESULT=0x"
                      << std::hex << static_cast<unsigned>(hr) << std::dec
                      << '\n';
        }
        pipeline_library_.Reset();
        pipeline_library_blob_.clear();
        hr = device1->CreatePipelineLibrary(
            nullptr,
            0,
            IID_PPV_ARGS(&pipeline_library_));
    }

    if (FAILED(hr)) {
        pipeline_library_.Reset();
        return;
    }

    if (!pipeline_library_blob_.empty() && trace_gx_stalls_enabled()) {
        std::cerr << "[PipelineCache] loaded D3D12 pipeline library ("
                  << pipeline_library_blob_.size() << " bytes)\n";
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
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = make_gx_pso_desc(
        root_signature_.Get(),
        key.render_state,
        vs_blob,
        ps_blob,
        line_geometry_shader_.Get(),
        point_geometry_shader_.Get());
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
        point_geometry_shader_.Get());
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
    // `pipeline_library_mutex_` is also taken by `create_or_load_pso` on the
    // render thread (and on prewarm workers) around every
    // `LoadGraphicsPipeline`/`StorePipeline`. Only `ID3D12PipelineLibrary`
    // itself needs that mutual exclusion; the disk write that follows does not.
    // Holding the lock across `GetSerializedSize`, the whole-library
    // `Serialize` into a heap vector, a temp-file create/write and a
    // write-through rename would queue every render-thread pipeline
    // acquisition behind a multi-megabyte file write. Serialize under the lock
    // (that is the only part that touches the borrowed library bytes), take a
    // local copy of the blob, then release it before touching the filesystem.
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
        // Rotate pending state under the lock so a concurrent StorePipeline
        // remains pending after this snapshot is saved. Any failure below must
        // also re-arm it: serialization alone has not persisted the snapshot.
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

    // Do not clear the shared flag here: workers may have stored new pipelines
    // since serialization. Only retire this successfully persisted snapshot.
    needs_retry = false;
    // Serialization output does not replace the bytes borrowed by the live library.
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
    if (!pso_prewarm_enabled() || warm_pso_keys_.empty()) {
        return 0;
    }

    published_.reserve(warm_pso_keys_.size());
    in_flight_.reserve(warm_pso_keys_.size());
    std::uint64_t enqueued = 0;
    const std::uint64_t limit = pso_prewarm_limit();
    {
        std::lock_guard<std::mutex> lock(job_mutex_);
        for (const PsoKey& key : warm_pso_keys_) {
            if (enqueued >= limit) {
                break;
            }
            // A failed/interrupted pair-file save can leave both component
            // stages under other pairs. Their hashes fully identify the same
            // DXBC needed here; exact-pair presence is not a prewarm condition.
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
    if (enqueued != 0 && trace_gx_stalls_enabled()) {
        std::cerr << "[PipelineCache] queued " << enqueued
                  << " cached PSO prewarm jobs\n";
    }
    if (enqueued != 0) {
        const unsigned threads = static_cast<unsigned>(
            std::min<std::uint64_t>(std::clamp(worker_count, 1u, 4u), enqueued));
        workers_.reserve(threads);
        for (unsigned i = 0; i < threads; ++i) {
            workers_.emplace_back([this] {
                // PSO prewarm is background work: FXC plus a driver PSO build, several jobs deep on up to
                // four threads. At default priority those threads compete with the render thread, which
                // runs at ABOVE_NORMAL. A runnable prewarm worker can take the core when the render thread
                // wakes and delay a pipeline-cache probe on the per-draw flush_draw_state path. BELOW_NORMAL
                // makes the render thread win that race. Prewarm finishes later, and a missed job is retried
                // by the synchronous first-use path. This does not change what is returned.
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

void PipelineCache::wait_for_cached_pso_prewarm(std::uint64_t enqueued) {
    const auto start = std::chrono::steady_clock::now();
    const std::uint64_t timeout_ms = pso_prewarm_blocking_timeout_ms();
    for (;;) {
        const auto observed = completion_snapshot();
        drain_completions();
        if (in_flight_.empty()) {
            break;
        }
        std::uint64_t remaining_ms = 30000u;
        if (timeout_ms != 0u) {
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - start).count();
            if (static_cast<std::uint64_t>(elapsed_ms) >= timeout_ms) {
                if (trace_gx_stalls_enabled()) {
                    std::cerr << "[PipelineCache] blocking prewarm timed out"
                              << " enqueued=" << enqueued
                              << " pending=" << in_flight_.size()
                              << " timeout-ms=" << timeout_ms << '\n';
                }
                return;
            }
            // Cap each relative wait so even an unbounded or very large user
            // timeout is representable in chrono's signed duration type.
            remaining_ms = std::min(remaining_ms,
                timeout_ms - static_cast<std::uint64_t>(elapsed_ms));
        }
        wait_for_worker_completion(observed, std::chrono::milliseconds(remaining_ms));
    }

    if (trace_gx_stalls_enabled()) {
        const auto end = std::chrono::steady_clock::now();
        std::cerr << "[PipelineCache] blocking prewarm completed"
                  << " enqueued=" << enqueued
                  << " elapsed-ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         end - start).count()
                  << '\n';
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
        // The file's validated prefix is copied forward and `records` arrives as
        // the number of records it already holds, so this emits only pairs the
        // prefix does not carry - which is what stops a rewrite of an
        // already-complete cache from appending a duplicate of itself.
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
                // Identities were collected during the validated scan under the same writer lease, so the
                // shaders are not hashed and reflected again. Emit resident pairs, then pending ones the
                // resident pass did not cover. A pair can be both resident and pending (shutdown keeps
                // shader_blobs_ when unsaved_blobs_ is non-empty). `emitted` suppresses that duplicate, and
                // the prefix suppresses pairs already in the output.
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
                // PsoKey::operator== is the identity test here. It compares the fields hash() mixes, so keys
                // that collide under PsoKeyHasher stay distinct. The excluded reserved bytes cannot split one
                // key, because valid_pso_key already forces them to zero.
                std::unordered_set<PsoKey, PsoKeyHasher> in_prefix;
                in_prefix.reserve(records);
                for (std::uint32_t i = 0; i < records; ++i) {
                    PsoKey key{};
                    if (std::fread(&key, sizeof(key), 1, src) != 1) return false;
                    in_prefix.insert(key);
                }
                // Merge RESIDENT and PENDING keys before emitting. shutdown calls flush_disk and then clears
                // persisted_pso_keys_, so a retry arrives with resident empty and only unsaved_pso_keys_
                // populated. The branch below walks both, or a failed shutdown's pending keys are lost.
                // Membership is a hash lookup, and `emitted` dedupes a key that is both resident and pending.
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
    // The done publication follows all worker access. Only explicit flush or
    // shutdown waits for unfinished I/O; normal frame polling cannot do so.
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

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------

std::uint64_t PipelineCache::specialized_hits() const {
    return specialized_hits_;
}

std::uint64_t PipelineCache::uber_fallbacks() const {
    return uber_fallbacks_;
}

PipelineCache::CompileStats PipelineCache::compile_stats() const {
    // Called on the render thread, from the frame reset and from
    // ReportPipelineCoverage(). `pso_compiles` / `pso_library_loads` are atomics
    // because prewarm workers increment them too; the rest are written only by
    // this thread (see the field comments in the header). Relaxed ordering is
    // sufficient: these are monotonic diagnostics, so a stale read can misreport
    // a per-frame delta but can never corrupt state.
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

// ---------------------------------------------------------------------------
// Uber pipelines
// ---------------------------------------------------------------------------

// Render states observed in play-session pipeline caches. Uber pipelines for these are built
// in the background as soon as the uber DXBC exists, so a first-use material in a fresh
// install finds one.
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

// Uber compile thread (or initialize() in force mode). D3DCompile is
// thread-safe; the blobs are published once, with release ordering.
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
            std::cerr << "[uber-pipeline] shader compile failed; first-use misses stay synchronous\n";
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
// variant when it exists. Otherwise queues its build (or, with create_now,
// builds it here) once the uber DXBC is ready.
ID3D12PipelineState* PipelineCache::uber_pipeline(
    const RenderStateKey& render_state, unsigned variant, bool create_now) {
    if (!uber_blobs_ready_.load(std::memory_order_acquire)) {
        return nullptr;
    }
    if (!uber_prewarm_enqueued_) {
        uber_prewarm_enqueued_ = true;
        enqueue_uber_prewarm();
    }
    UberPsoKey key{};
    // Uber stand-ins stay single-target: a motion writer drawing with one
    // leaves its pixels' motion at the cleared zero for those frames.
    key.render_state.blend_bits =
        render_state.blend_bits & ~(kRenderStateMotionTarget | kRenderStateReactiveTarget);
    key.render_state.zmode_bits = render_state.zmode_bits;
    key.render_state.cull = render_state.cull;
    key.render_state.pixfmt = render_state.pixfmt;
    key.render_state.primitive_topology = render_state.primitive_topology;
    key.variant = variant;
    if (const auto it = uber_psos_.find(key); it != uber_psos_.end()) {
        return it->second.Get();  // null: its build failed, never retried
    }
    if (create_now) {
        auto pso = create_uber_pso(key.render_state, variant);
        ID3D12PipelineState* raw = pso.Get();
        uber_psos_[key] = std::move(pso);
        return raw;
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

// Render states this cache has seen before, each with the early-depth
// variant (dual-source when the blend state reads SRC1 alpha): the common
// case, built in the background so first-use materials meet a ready uber
// pipeline. Other variants are built on their first miss.
void PipelineCache::enqueue_uber_prewarm() {
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

// Any thread (compile workers, or the render thread in force mode).
Microsoft::WRL::ComPtr<ID3D12PipelineState> PipelineCache::create_uber_pso(
    const RenderStateKey& render_state, unsigned variant) {
    if (!uber_blobs_ready_.load(std::memory_order_acquire) || variant >= kUberPsVariants) {
        return nullptr;
    }
    ID3DBlob* vs = uber_vs_blob_.Get();
    ID3DBlob* ps = uber_ps_blobs_[variant].Get();
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC pd = make_gx_pso_desc(
        root_signature_.Get(), render_state, vs, ps,
        line_geometry_shader_.Get(), point_geometry_shader_.Get());
    wchar_t name[160]{};
    swprintf_s(name, L"uber_%016llx_%08x_%08x_%02x_%02x_%02x_%u",
        static_cast<unsigned long long>(uber_source_hash_),
        render_state.blend_bits, render_state.zmode_bits,
        static_cast<unsigned>(render_state.cull),
        static_cast<unsigned>(render_state.pixfmt),
        static_cast<unsigned>(render_state.primitive_topology), variant);
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
        line_geometry_shader_.Get(), point_geometry_shader_.Get());
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

// ---------------------------------------------------------------------------
// worker_main: compile thread
// ---------------------------------------------------------------------------

void PipelineCache::ensure_compile_workers(unsigned count) {
    if (!workers_.empty() || stopping_.load(std::memory_order_relaxed)) {
        return;
    }
    const unsigned threads = std::clamp(count, 1u, 2u);
    workers_.reserve(threads);
    for (unsigned i = 0; i < threads; ++i) {
        workers_.emplace_back([this] {
            // Same priority rule as the prewarm workers: the render thread
            // must always win the core.
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
                // stopping_ is true and no remaining jobs.
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
