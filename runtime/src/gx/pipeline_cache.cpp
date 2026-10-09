// pipeline_cache.cpp — Lock-free PSO completion queue (Vyukov MPSC) and
// PipelineCache with background shader specialization.
//
// Threading contract:
//   - get(), drain_completions() — render thread only.
//   - push() — any compile worker thread, lock-free.
//   - try_pop() — render thread only.
//
// The published_ map and in_flight_ set are render-thread-only; no locks
// are required for them.  The job intake (jobs_, job_mutex_, job_cv_) uses a
// plain mutex/condvar — the hot path back to the render thread (completion
// queue) remains lock-free.

#include "galaxy/gx/pipeline_cache.h"

#include "galaxy/gx/renderer_d3d12.h"
#include "galaxy/gx/shader_gen.h"
#include "galaxy/gx/shader_keys.h"
#include "galaxy/gx/shader_pair_compile.h"
#include "galaxy/scope_exit.h"

#pragma warning(push, 0)
#include <d3dcompiler.h>
#include <d3d12shader.h>
#include <Windows.h>
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
Microsoft::WRL::ComPtr<ID3DBlob> compile_shader(
    const std::string& src,
    const char* target,
    const char* label) {
    Microsoft::WRL::ComPtr<ID3DBlob> code, errors;
    // Level 1: the driver re-optimizes DXBC when it builds the PSO, so FXC's
    // level 3 only lengthens the synchronous first-use miss on the render
    // thread (measured 12-21 ms per shader pair at level 3).
    const HRESULT hr = D3DCompile(
        src.data(), src.size(), label, nullptr, nullptr, "main", target,
        D3DCOMPILE_ENABLE_STRICTNESS |
            D3DCOMPILE_PACK_MATRIX_ROW_MAJOR |
            D3DCOMPILE_OPTIMIZATION_LEVEL1,
        0, &code, &errors);
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
    // GX cull → D3D12.  GX screen space is Y-down relative to D3D NDC (the
    // EFB viewport does not flip), so GX "front" lands as a D3D back face
    // and vice versa — the same front/back swap Dolphin's D3D backends use.
    // CullMode::All (rasterize nothing) cannot be expressed in one PSO; GX
    // titles use it to suppress geometry, so an empty scissor would be the
    // faithful path — not observed in the SMG boot, mapped to None.
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

    // ---- Depth from zmode_bits -------------------------------------------
    // FLIPPED inequality table (Dolphin D3D backends): our depth buffer is
    // REVERSED-Z relative to the GX z24 values (near=1, far=0; see the
    // projection negation in gx_backend.cpp fill_projection), so GX LESS
    // must become D3D GREATER, LEQUAL → GREATER_EQUAL, etc.  The old
    // straight n→n+1 mapping was only valid for a non-inverted buffer.
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
// PsoCompletionQueue — Vyukov MPSC intrusive list
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
    // Allocated on the heap — the consumer frees it after popping.
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

    // worker_count retained in the signature for the M8 hybrid-uber plan;
    // the current policy compiles synchronously on first miss (a one-time
    // hitch per material instead of invisible geometry — see get()).
    stopping_.store(false, std::memory_order_relaxed);

    // Create the cache directory if it does not exist.
    std::error_code ec;
    std::filesystem::create_directories(cache_dir_, ec);
    // A failure here is non-fatal — we simply won't have disk persistence.

    // Opt-in (GALAXY_SHADER_SEED_DIRECTORY): populate a FRESH user cache from a
    // validated seed pair before the loaders below read it. A no-op otherwise.
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

    // One bounded startup record is useful even when gameplay diagnostics
    // are off. It reports preparation, not an assertion of future coverage.
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
    drain_completions();
    flush_disk();

    published_.clear();
    uber_.clear();
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
// get — render thread only
// ---------------------------------------------------------------------------

ID3D12PipelineState* PipelineCache::get(
    const PsoKey& key,
    const PixelShaderKey& ps_key,
    const VertexShaderKey& vs_key) {

    if (worker_failed_.load(std::memory_order_acquire)) [[unlikely]] {
        std::lock_guard<std::mutex> lock(job_mutex_);
        if (worker_error_) std::rethrow_exception(worker_error_);
    }
    // 0. One-entry memo of the last successful resolution. This is in front of
    // the map probe because a busy frame alternates between a small number of
    // PsoKeys (a material and its blend/depth variants), so the same key comes
    // back within a few draws. This is the slice the frame timings blame for most
    // of the parse cost. published_ never loses an entry during a session, so a
    // memo hit returns exactly what the probe below would have returned.
    //
    // The comparison order is deliberate: hash first, key second. `PsoKey::hash()`
    // is 32 bytes mixed into a 64-bit value, so the old form — compare all 32
    // bytes of the key, and on a miss hash those same 32 bytes again — did the
    // expensive work twice on every miss. A mismatching hash already proves the
    // keys differ, because hash is a pure function of the key, so the byte
    // comparison only has to run on a hash match. That is the same rule
    // std::unordered_map uses, and it is safe for the reason the map's own
    // equality is: a hash collision falls through to the exact comparison rather
    // than returning a wrong pipeline.
    const std::uint64_t requested_hash = key.hash();
    if (last_hit_pipeline_ != nullptr &&
        last_hit_key_.hash == requested_hash &&
        last_hit_key_.key == key) {
        ++specialized_hits_;
        return last_hit_pipeline_;
    }
    // Hash the key once for the whole request, reusing the value the memo above
    // already computed. The renderer issues this call whenever a shader/render
    // state input changed, which on a busy frame is tens of thousands of times;
    // the memo keeps the map probes from re-deriving the same hash for each of
    // published_ and in_flight_, and this construction keeps the memo itself from
    // hashing the same 32 bytes a second time on every miss.
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
        // A worker that finished after begin_frame()'s drain left its result on
        // the lock-free completion queue, where the probe above cannot see it.
        // Drain here — get() is render-thread-only, exactly like the frame
        // drain — so an already-finished job is taken instead of being waited
        // on. Without this the render thread pays a full wait quantum for every
        // render-state change that touches a key whose compile is already done.
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

    // 2. Compile SYNCHRONOUSLY on first sight.  Rationale (review note): the
    // previous policy returned the not-yet-built uber fallback — effectively
    // no pipeline — so every first-seen material (and every new blend/zmode
    // variant of a known material) was SKIPPED for the frames its async
    // compile spent in flight: structural pop-in/flicker at every scene
    // change.  Until a real uber pipeline exists (M8), a one-time per-shader
    // hitch is strictly better than invisible geometry, and the DXBC cache
    // below (in-memory by shader hash + shaders.bin on disk) limits FXC to
    // the FIRST run that ever sees the shader; render-state variants reuse
    // the blobs and pay only the driver PSO build.  This mirrors the
    // trade-off space described in Dolphin's hybrid-ubershader design.
    ++uber_fallbacks_;

    const auto miss_start = std::chrono::steady_clock::now();
    const ShaderPairKey shader_pair_hash{key.vs_hash, key.ps_hash};
    const bool blob_cache_hit =
        shader_blobs_.find(shader_pair_hash) != shader_blobs_.end();
    const bool vs_cache_hit = vertex_shader_blobs_.contains(key.vs_hash);
    const bool ps_cache_hit = pixel_shader_blobs_.contains(key.ps_hash);
    const bool known_key = persisted_pso_keys_.contains(key);
    const ShaderPair* blobs = get_or_compile_blobs(key, ps_key, vs_key);
    const auto blobs_end = std::chrono::steady_clock::now();

    Microsoft::WRL::ComPtr<ID3D12PipelineState> pso;
    const auto pso_start = std::chrono::steady_clock::now();
    if (blobs != nullptr && blobs->vs && blobs->ps) {
        pso = create_or_load_pso(key, blobs->vs.Get(), blobs->ps.Get());
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
                  // Both recordings show FXC, not the driver build, dominating
                  // this stall: on all 17 RTX misses blob-us exceeded pso-us,
                  // and blob-cache-hit was 0 on 84 of 85 misses across the two
                  // recordings. This field is the split, so a reader does not
                  // have to divide the two by hand (or, worse, guess).
                  << " blob-share-pct="
                  << blob_share_percent(
                         elapsed_us(miss_start, blobs_end), total_us)
                  // Which stage was missing is what decides whether the pair
                  // cache is answering or only the per-stage maps are: both
                  // stages missing is a genuinely new material, one stage
                  // missing means the pair record was not found for a shader
                  // already resident.
                  << " stages-missing="
                  << ((blob_cache_hit ? 0u : 1u) +
                      (vs_cache_hit ? 0u : 1u) + (ps_cache_hit ? 0u : 1u))
                  << " success=" << (raw != nullptr ? 1 : 0)
                  << '\n';
        std::cerr << message.str();
    }
    return raw;
}

// ---------------------------------------------------------------------------
// drain_completions — render thread only, called from begin_frame()
// ---------------------------------------------------------------------------

void PipelineCache::drain_completions() {
    // The last-hit memo in get() asserts that `published_` cannot gain an entry
    // between the memo being written and it being read. This is the only place
    // that can add one outside get(), so it is the only place that has to drop
    // the memo. Doing it unconditionally keeps the assert true even when the
    // queue turns out to be empty: the cost is one null store on a path that
    // already runs once per frame.
    clear_last_hit_memo();
    PsoCompletionQueue::Item item;
    while (completions_.try_pop(item)) {
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
            }
            remember_pso_key(item.key);
        }
    }
}

bool PipelineCache::wait_for_in_flight_pso(
    const MemoizedPsoKey& memoized,
    ID3D12PipelineState*& out) {
    out = nullptr;
    const PsoKey& key = memoized.key;
    const std::uint64_t key_hash = memoized.hash;
    // A queued job can be claimed by first use. A running job has one owner;
    // timing out must never cause a duplicate driver creation for the same key.
    //
    // Distinguish the two cases, because only one of them needs to wait. A job
    // still sitting in `jobs_` has no owner yet, so the render thread may take it
    // and build the PSO itself — see the inline attempt below. A job a worker has
    // already removed from the queue is genuinely in progress and must be waited
    // on, or two threads would create the same pipeline concurrently.
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
// get_or_compile_blobs — DXBC pair lookup / synchronous FXC
// ---------------------------------------------------------------------------

const PipelineCache::ShaderPair* PipelineCache::get_or_compile_blobs(
    const PsoKey& key,
    const PixelShaderKey& ps_key,
    const VertexShaderKey& vs_key) {
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
    // A pair reachable here with both stages already resident came out of
    // shaders.bin (or an earlier compile) for this exact (vs_hash, ps_hash);
    // that is the coverage the disk cache is supposed to provide and is the
    // denominator for "did the cache actually grow".
    if (!need_vs && !need_ps) {
        ++blobs_from_cache_;
    } else {
        ++shader_pair_compiles_;
    }
    // Time the FXC work only, not the map probes above: this is the figure that
    // says what shader compilation cost the session, and it is the term the
    // stall attribution needs.
    const auto compile_start = std::chrono::steady_clock::now();
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
// Version GATES GENERATOR OUTPUT, not just the file format: bump whenever
// generate_vs/generate_ps emit different HLSL for the same key, or stale
// DXBC with old semantics is served for hash-identical keys.
    // v17 keeps TEV registers as Dolphin-style signed ints. v16 matches
    // Dolphin's diffuse-light color multiplication order. v15 wraps
    // dual-tex post-matrix rows like XF hardware. v13 includes integer XF
    // lighting and the TEV alpha=1 blend quirk. v18 widens GxVertexOut from
    // 64 to 112 bytes, carries TEXCOORD2-7, and changes VS BLENDINDICES to
    // uint4. v19 changes PS texture sampling to use BP texcoord scale in
    // tex_dims.zw instead of inverse image size. v20 copies the final TEV
    // stage's REG0-2 destination into PREV before PE output. v21 applies an
    // active indirect-coordinate op even when the stage's regular texture
    // lookup is disabled. v22 widens GxVertexOut to carry NBT
    // tangent/binormal and implements emboss texgens from those attributes.
    // v23 reverts emboss texgen to a clean source-coord passthrough (the v22
    // NBT-based offset scrambled texcoords into fabric noise on emboss-mapped
    // surfaces because RMGE01 vertices lack authored tangent/binormal).
constexpr std::uint32_t kShaderCacheVersion = 24u;
constexpr std::uint32_t kPsoKeyCacheMagic = 0x43504B47u;  // "GKPC"
// These keys are only meaningful while the shader cache that defines their
// `vs_hash`/`ps_hash` stages is also current: a PSO key whose stages are absent
// from `shaders.bin` cannot be prewarmed, because enqueue_cached_pso_prewarm
// skips any key find_cached_shader_stages rejects. Keeping the two versions
// independent therefore lets a kShaderCacheVersion bump invalidate `shaders.bin`
// and `pipelines.bin` (which already gates on shader_version, see the header
// read in load_disk_cache) while leaving `psos.bin` behind, orphaning its keys
// until they are forgotten. Folding the shader version into this one makes the
// dependency a foreign key that cannot be forgotten: the file is rejected and
// rebuilt whenever the shader semantics it references change. Rejection is
// self-healing and cheap -- these keys are a pure cache, and losing them costs
// only the prewarm of keys the game has not met yet in this session.
//
// This is PREVENTIVE hardening, not a repair. An earlier version of this comment
// claimed "36 of 72 keys unusable in one real cache directory"; that measurement
// was withdrawn as an artefact of parsing `psos.bin` with an 80-byte stride when
// PsoKey is 32 bytes. Re-measured at the correct stride, a real cache directory
// held 180 keys and every one was fully covered -- no orphans. The dependency is
// real and worth making explicit; it simply was not being violated at the time.
constexpr std::uint32_t kPsoKeyCacheVersion = 0x100u + kShaderCacheVersion;
constexpr std::uint32_t kPipelineLibraryMagic = 0x4C505847u; // "GXPL"
constexpr std::uint32_t kPipelineLibraryVersion = 6u;
constexpr std::uint64_t kMaxPipelineLibraryBytes = 64ull << 20;
constexpr std::uint64_t kMaxShaderCacheBytes = 128ull << 20;
constexpr std::uint32_t kMaxCacheRecords = 65536u;
// Both disk caches are append-only running totals, so a session that meets a new
// configuration pays for it again on every later launch unless the file itself
// grows to cover it. Append in bounded batches and then pay one rewrite that
// brings the file up to the coverage the process already holds in memory.
// 64 amortizes that rewrite (a few hundred KB to a few MB, once per 64 newly
// learned configurations) instead of rewriting the whole shader cache per flush.
constexpr std::size_t kCacheCoverageAppendBatch = 64u;

bool valid_pso_key(const PsoKey& key) {
    const auto& state = key.render_state;
    return state.primitive_topology >= 1u && state.primitive_topology <= 3u &&
        state.cull <= 3u && state.pixfmt <= 7u &&
        (state.blend_bits & 0xe0000000u) == 0u && (state.zmode_bits & ~0x1fu) == 0u &&
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

// A failed allocation is not evidence that the cached bytes are corrupt.
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
        if (WaitForSingleObject(persistence_test_write_release, 5000u) != WAIT_OBJECT_0)
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
    // The copy loop above left `source` at the END of the prefix, which is EOF
    // whenever the prefix is the whole file - the normal case for a repair. The
    // records `write_pending` needs are BEHIND that position, so seek back to the
    // first one. Offset 8 is always the first record when the prefix is
    // non-empty: `find_cache_prefix` sets `prefix_bytes` to exactly 8u for the
    // header and only ever adds whole records, so the header is 8 bytes and both
    // formats put their first record immediately after it. When the prefix is
    // empty the callback is given zero records and never touches `source`, so the
    // seek is a harmless no-op. Established here rather than in either lambda
    // because both callbacks share this position.
    if (written && retained_records != 0u) {
        written = source != nullptr && _fseeki64(source, 8, SEEK_SET) == 0;
    }
    // An allocation failure inside the writer must abort this save, not the
    // caller and not the process: `PipelineCache` is an optional cache, so the
    // correct response is a false return with the pending data still pending,
    // which the guards above then clean up. Only allocation and length errors are
    // caught - anything else propagates rather than being silently swallowed.
    if (written) {
        try {
            written = write_pending(output, source, retained_records);
        } catch (const std::bad_alloc&) {
            written = false;
        } catch (const std::length_error&) {
            written = false;
        }
    }
    // Close BOTH handles before the replacement. `MoveFileExW` must not run while
    // the CRT still holds the source open, and the guard above would skip a
    // handle that had been nulled without being closed - so close explicitly,
    // check the result, and only then null. The guards remain for exits that
    // bypass this tail.
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
// Optional fresh-cache seed import (GALAXY_SHADER_SEED_DIRECTORY=<dir>)
//
// <dir> holds a shaders.bin + psos.bin pair written by a run that used the
// CURRENT cache versions. It is imported only into a FRESH user cache (neither
// user file present), byte for byte, and only after every record has been
// validated with the same checks the loaders apply. The seed is never read
// through load_disk_cache()/load_pso_key_cache(): those set repair state for the
// user's files and the writers later rewrite them, neither of which applies to
// a read-only source. The regular loaders validate the imported copy again.
//
// The checksums prove the bytes are intact; they do not prove the DXBC derives
// from the hash it is filed under. Seed provenance (generator fingerprint,
// release version) is a packaging gate, not something checked here.
//
// Commit protocol (every step non-fatal; any failure leaves the ordinary
// compile-on-miss path):
//   1. Open both seed files with write sharing denied, so validation and the
//      copy see the same bytes.
//   2. Take the writer lease of BOTH user files (non-blocking; busy = skip).
//   3. Re-check under the leases that neither user file exists.
//   4. Copy each seed file into its own CREATE_NEW temp file beside the target,
//      flush, close.
//   5. Publish shaders.bin first, then psos.bin, with MoveFileExW and no
//      REPLACE_EXISTING: it fails if the target appeared, so a concurrent winner
//      is never overwritten and never deleted.
// A failure between the two publishes leaves a valid shaders.bin and no
// psos.bin, which is exactly the state of an ordinary run that has learned
// shaders but not yet saved PSO keys: load_disk_cache() uses it, prewarm has no
// keys, flush_disk() creates psos.bin later. The committed shaders.bin is never
// deleted. Only temp files this call created are removed; a crash can strand a
// "<name>.seed-<pid>-<tick>-<n>.tmp", which nothing reads and which is safe to
// delete.
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
    // failure; the unconsumed tail is not evidence of corruption.
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
    // The record size is part of the on-disk format: `psos.bin` is a bare,
    // unpadded run of `PsoKey` records, so changing `PsoKey` silently makes an
    // existing file misparse unless the version moves too.
    //
    // The size guard lives next to the type, not here. `shader_keys.h` already
    // asserts `sizeof(PsoKey) == 32` and that the type has no padding, which is
    // the real invariant; a second literal comparison here can only contradict it
    // — the previous `== 80u` did exactly that and failed to compile every
    // translation unit that included this file. What is worth guarding *here* is
    // that the record really is one bare `PsoKey`, i.e. that the type is
    // trivially copyable and therefore safe to `fread`/`fwrite` as a raw run.
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
    ID3DBlob* ps_blob) {
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

    {
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
                // PSO prewarm is pure background work, but it is FXC plus a
                // driver PSO build: many milliseconds of saturated CPU per job,
                // several jobs deep, on up to four threads. At default priority
                // those threads compete with the render thread, which the
                // runtime raises to ABOVE_NORMAL precisely because it is the
                // critical path. Priority is the tie-breaker Windows uses when
                // both are runnable, so a prewarm worker that happens to be
                // runnable when the render thread wakes can take the core and
                // stretch a pipeline-cache probe from ~100 ns into hundreds of
                // microseconds of wall clock. That is indistinguishable from a
                // slow lookup in a wall-clock instrumented slice, and it lands
                // exactly in the per-draw `flush_draw_state` path. BELOW_NORMAL
                // makes the render thread always win that race; the only cost is
                // that prewarm finishes later, and prewarm is idempotent
                // background work whose late or missed job is retried by the
                // synchronous first-use path. This does not serialize anything
                // or change which pipeline is returned.
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
                // Identities were collected during the validated scan under the
                // same writer lease; do not allocate, hash and reflect every
                // shader a second time just to recover those identities.
                // Emit resident pairs, then pending ones the resident pass did not
                // already cover. Membership in the prefix and in `emitted` both
                // suppress a duplicate: the prefix because its bytes are already in
                // the output, `emitted` because a pair can be BOTH resident and
                // pending - `shutdown` retains `shader_blobs_` when `unsaved_blobs_`
                // is non-empty - and emitting it in both passes would append a
                // duplicate record every flush.
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
                // `PsoKey::operator==` is the identity test here, not a hash
                // compare and not a byte walk: it compares exactly the fields
                // `hash()` mixes, so two keys colliding under PsoKeyHasher stay
                // distinct records, while the deliberately excluded `reserved`
                // bytes cannot split one key in two - and `valid_pso_key` has
                // already forced those bytes to zero.
                std::unordered_set<PsoKey, PsoKeyHasher> in_prefix;
                in_prefix.reserve(records);
                for (std::uint32_t i = 0; i < records; ++i) {
                    PsoKey key{};
                    if (std::fread(&key, sizeof(key), 1, src) != 1) return false;
                    in_prefix.insert(key);
                }
                // Merge RESIDENT and PENDING keys before emitting. `shutdown`
                // calls flush_disk and then clears `persisted_pso_keys_`, so a
                // retry arrives with resident empty and only `unsaved_pso_keys_`
                // populated - the branch below must therefore walk BOTH, or a
                // failed shutdown's pending keys are lost. Membership is a hash
                // lookup rather than a scan per record, and `emitted` dedupes a
                // key that is both resident and pending.
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
// get_or_create_uber — render thread only
// ---------------------------------------------------------------------------

ID3D12PipelineState* PipelineCache::get_or_create_uber(
    const RenderStateKey& render_state) {

    const std::uint64_t rs_hash =
        fnv1a64(&render_state, sizeof(render_state));

    const auto it = uber_.find(rs_hash);
    if (it != uber_.end()) {
        return it->second.Get();
    }

    // Future native-feature work can compile an uber PSO from embedded DXBC
    // here. For now insert nullptr so subsequent lookups hit the map and do
    // not repeatedly reach this path.
    uber_.emplace(rs_hash, Microsoft::WRL::ComPtr<ID3D12PipelineState>{});
    return nullptr;
}

// ---------------------------------------------------------------------------
// worker_main — compile thread
// ---------------------------------------------------------------------------

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
