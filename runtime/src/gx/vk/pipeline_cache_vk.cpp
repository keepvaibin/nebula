// SPDX-License-Identifier: GPL-3.0-only
// GX pipelines on Vulkan (pipeline_cache_vk.h).

#include "galaxy/gx/vk/pipeline_cache_vk.h"

#include "galaxy/gx/host_window.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/windows_power.h"
#include "galaxy/gx/vk/shader_compile_vk.h"
#include "galaxy/gx/vk/vk_context.h"
#include "galaxy/gx/vertex_loader.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>

#if !defined(_WIN32)
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace galaxy::gx {

namespace {

// vk_shaders.bin: magic, version, the compiler's identity (compiler_identity), then records of (u64 HLSL hash, u32
// words, SPIR-V). vk_seed_shaders.bin (build_shader_seed) has the same form; either is read only when its compiler is
// this build's.
// vk_keys.bin: magic, version, then Entry records (the keys the shaders are generated from).
// vk_pipelines.bin: the driver's VkPipelineCache data (it checks its own header).
constexpr std::uint32_t kShaderFileMagic = 0x53565847u;  // "GXVS"
constexpr std::uint32_t kKeyFileMagic = 0x4b565847u;     // "GXVK"
constexpr std::uint32_t kShaderFileVersion = 2u;
constexpr std::uint32_t kKeyFileVersion = 2u;
constexpr std::uint32_t kMaxRecords = 65536u;
constexpr std::uint32_t kMaxSpirvWords = 1u << 20u;

// CompileJournal kinds.
constexpr std::uint64_t kJournalSpecialised = 1u;
constexpr std::uint64_t kJournalUber = 2u;

const char* compat_name(std::uint32_t bit) {
    switch (bit) {
    case 1u << 0u: return "depth-clip control";
    case 1u << 1u: return "logic ops";
    case 1u << 2u: return "dual-source blending";
    case 1u << 3u: return "depth clamp";
    case 1u << 4u: return "geometry shaders";
    default: return "several features";
    }
}

std::uint64_t uber_hash(const RenderStateKey& render_state, unsigned variant) {
    PsoKey pso{};
    pso.render_state = render_state;
    return combine64(pso.hash(), variant);
}

std::string describe_render_state(const RenderStateKey& rs) {
    char text[128];
    std::snprintf(text, sizeof(text), "blend 0x%08x z 0x%04x cull %u format %u topology %u", rs.blend_bits,
                  rs.zmode_bits, rs.cull, rs.pixfmt, rs.primitive_topology);
    return text;
}

// GALAXY_VK_TEST_REFUSE=<names, comma-separated>: pipelines using those features (dual-source, depth-clip,
// depth-clamp, logic-op, geometry) or of that kind (specialised, uber, uber-vs: with the uber vertex shader) are
// refused as a driver would, so the fallbacks run on any GPU. GALAXY_VK_TEST_CRASH_AT=<n>: the process aborts inside
// the n-th specialised build (the compile journal's test).
struct TestRefusals {
    bool dual_source = false;
    bool depth_clip = false;
    bool depth_clamp = false;
    bool logic_op = false;
    bool geometry = false;
    bool specialised = false;
    bool uber = false;
    bool uber_vs = false;  // the Adreno 840 case: the uber vertex shader alone
    unsigned crash_at = 0;
};

const TestRefusals& test_refusals() {
    static const TestRefusals refusals = [] {
        TestRefusals r;
        const char* names = std::getenv("GALAXY_VK_TEST_REFUSE");
        const std::string list = names != nullptr ? std::string(",") + names + "," : "";
        const auto has = [&](const char* name) { return list.find(std::string(",") + name + ",") != std::string::npos; };
        r.dual_source = has("dual-source");
        r.depth_clip = has("depth-clip");
        r.depth_clamp = has("depth-clamp");
        r.logic_op = has("logic-op");
        r.geometry = has("geometry");
        r.specialised = has("specialised");
        r.uber = has("uber");
        r.uber_vs = has("uber-vs");
        const char* crash = std::getenv("GALAXY_VK_TEST_CRASH_AT");
        r.crash_at = crash != nullptr ? static_cast<unsigned>(std::strtoul(crash, nullptr, 10)) : 0u;
        return r;
    }();
    return refusals;
}

std::uint64_t elapsed_us(std::chrono::steady_clock::time_point start) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count());
}

// The HLSL a module comes from, with what changes its SPIR-V for the same text.
std::uint64_t module_hash(const std::string& hlsl, vk::ShaderStage stage, bool dual_source) {
    std::uint64_t hash = fnv1a64(hlsl.data(), hlsl.size());
    hash = combine64(hash, static_cast<std::uint64_t>(stage));
    hash = combine64(hash, vk::kCompilerRevision);
    return combine64(hash, dual_source ? 1u : 0u);
}

std::vector<std::uint32_t> compile_module(const std::string& hlsl, vk::ShaderStage stage, bool dual_source,
                                          std::string& error) {
    return vk::compile_hlsl(vk::annotate(hlsl, vk::gx_draw_bindings()), stage, "main", {}, error, {false, dual_source});
}

// diagnose_uber's simple vertex shader: it writes every output the uber pixel shader reads (a pixel shader input no
// vertex output writes is invalid, VUID-RuntimeSpirv-OpEntryPoint-08743).
VertexShaderKey diagnosis_vs_key() {
    VertexShaderKey key{};
    key.num_texgens = 8u;
    key.num_chans = 2u;
    return key;
}

// Cut-down forms of the uber shaders for diagnose_uber, as edits of their text (the shipped shaders keep their hashes).
struct UberProbe {
    const char* name;
    std::vector<std::pair<const char*, const char*>> edits;
};

const std::vector<UberProbe>& uber_ps_probes() {
    static const std::vector<UberProbe> probes = [] {
        const std::pair<const char*, const char*> no_sampling{
            "float4 sample_map(uint map, float2 uv) {",
            "float4 sample_map(uint map, float2 uv) {\n    return float4(0.5, 0.5, 0.5, 0.5);\n}\n"
            "float4 sample_map_unused(uint map, float2 uv) {"};
        const std::pair<const char*, const char*> no_indirect{"const uint num_ind = ind_misc.w;",
                                                              "const uint num_ind = 0u;"};
        const std::pair<const char*, const char*> one_stage{
            "[loop] for (uint st = 0u; st < n_stages && st < 16u; ++st) {",
            "[unroll] for (uint st = 0u; st < 1u; ++st) {"};
        return std::vector<UberProbe>{{"no-sampling", {no_sampling}},
                                      {"no-indirect", {no_indirect}},
                                      {"one-stage", {one_stage}},
                                      {"minimal", {no_sampling, no_indirect, one_stage}}};
    }();
    return probes;
}

const std::vector<UberProbe>& uber_vs_probes() {
    static const std::vector<UberProbe> probes = [] {
        const std::pair<const char*, const char*> no_texgen{"const uint key_texgens = uber_vs_header.x;",
                                                            "const uint key_texgens = 0u;"};
        const std::pair<const char*, const char*> no_lighting{"const uint key_chans = uber_vs_header.y;",
                                                              "const uint key_chans = 0u;"};
        // Within the texture coordinates (the part the Adreno 840 refused): the loop unrolled, and each kind of
        // coordinate left out in turn.
        const std::pair<const char*, const char*> unrolled{
            "[loop] for (uint i = 0u; i < key_texgens && i < 8u; ++i) {",
            "[unroll] for (uint i = 0u; i < 8u && i < key_texgens; ++i) {"};
        const std::pair<const char*, const char*> no_emboss{"} else if (type == 1u) {", "} else if (false) {"};
        const std::pair<const char*, const char*> no_texmtx{"if (((key_texmtx_mask >> i) & 1u) != 0u) {",
                                                            "if (false) {"};
        const std::pair<const char*, const char*> no_post{"if ((key_flags & 0x04u) != 0u) {", "if (false) {"};
        return std::vector<UberProbe>{{"no-texgen", {no_texgen}},
                                      {"no-lighting", {no_lighting}},
                                      {"minimal", {no_texgen, no_lighting}},
                                      {"texgen-unrolled", {unrolled}},
                                      {"texgen-no-emboss", {no_emboss}},
                                      {"texgen-no-texmtx", {no_texmtx}},
                                      {"texgen-no-post", {no_post}}};
    }();
    return probes;
}

// The probe's source, or empty when an edit's anchor is not in it exactly once (the shader changed).
std::string apply_probe(std::string source, const UberProbe& probe) {
    for (const auto& [from, to] : probe.edits) {
        const std::size_t at = source.find(from);
        if (at == std::string::npos || source.find(from, at + 1u) != std::string::npos) {
            return {};
        }
        source.replace(at, std::strlen(from), to);
    }
    return source;
}

// The SPIR-V this build's compiler makes of two fixed shaders, hashed. SPIR-V on disk and the driver refusals recorded
// with it belong to the compiler that made them: two builds of the same glslang version can make different SPIR-V of
// the same HLSL, which one driver refuses and accepts, so files of another compiler are dropped.
std::uint64_t compiler_identity() {
    static const std::uint64_t identity = [] {
        ShaderGenOptions options;
        options.scalar_byte_masks = true;
        ShaderGenerator generator;
        generator.set_options(options);
        VertexShaderKey vs_key{};
        vs_key.num_texgens = 1u;
        vs_key.num_chans = 1u;
        PixelShaderKey ps_key{};
        std::string error;
        const std::vector<std::uint32_t> vs =
            compile_module(generator.generate_vs(vs_key), vk::ShaderStage::Vertex, false, error);
        const std::vector<std::uint32_t> ps =
            compile_module(generator.generate_ps(ps_key), vk::ShaderStage::Fragment, false, error);
        std::uint64_t hash = fnv1a64(vs.data(), vs.size() * sizeof(std::uint32_t));
        hash = combine64(hash, fnv1a64(ps.data(), ps.size() * sizeof(std::uint32_t)));
        hash = combine64(hash, vk::kCompilerRevision);
        return combine64(hash, kShaderFileVersion);
    }();
    return identity;
}

std::string uber_ps_hlsl(unsigned variant) {
    std::string ps;
    ps.reserve(uber_ps_source().size() + 96u);
    ps += uber_ps_variant_dual_source(variant) ? "#define UBER_DUAL_SRC 1\n" : "#define UBER_DUAL_SRC 0\n";
    ps += uber_ps_variant_export_depth(variant) ? "#define UBER_EXPORT_DEPTH 1\n" : "#define UBER_EXPORT_DEPTH 0\n";
    ps += uber_ps_variant_early_depth(variant) ? "#define UBER_EARLY_DEPTH 1\n" : "#define UBER_EARLY_DEPTH 0\n";
    ps += "#define UBER_LAYER 0\n";
    ps += uber_ps_source();
    return ps;
}

VkBlendFactor gx_src_blend(std::uint32_t f) {
    switch (f) {
    case 0: return VK_BLEND_FACTOR_ZERO;
    case 1: return VK_BLEND_FACTOR_ONE;
    case 2: return VK_BLEND_FACTOR_DST_COLOR;
    case 3: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case 4: return VK_BLEND_FACTOR_SRC_ALPHA;
    case 5: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 6: return VK_BLEND_FACTOR_DST_ALPHA;
    case 7: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    default: return VK_BLEND_FACTOR_ONE;
    }
}

VkBlendFactor gx_dst_blend(std::uint32_t f) {
    switch (f) {
    case 0: return VK_BLEND_FACTOR_ZERO;
    case 1: return VK_BLEND_FACTOR_ONE;
    case 2: return VK_BLEND_FACTOR_SRC_COLOR;
    case 3: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case 4: return VK_BLEND_FACTOR_SRC_ALPHA;
    case 5: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case 6: return VK_BLEND_FACTOR_DST_ALPHA;
    case 7: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    default: return VK_BLEND_FACTOR_ZERO;
    }
}

// Colour-only factors are invalid for the alpha channel; the alpha equation takes their alpha forms.
VkBlendFactor to_alpha_blend(VkBlendFactor factor) {
    switch (factor) {
    case VK_BLEND_FACTOR_SRC_COLOR: return VK_BLEND_FACTOR_SRC_ALPHA;
    case VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case VK_BLEND_FACTOR_DST_COLOR: return VK_BLEND_FACTOR_DST_ALPHA;
    case VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    default: return factor;
    }
}

// GX LogicOp 0-15.
VkLogicOp gx_logic_op(std::uint32_t op) {
    static constexpr VkLogicOp kOps[16] = {
        VK_LOGIC_OP_CLEAR,        VK_LOGIC_OP_AND,          VK_LOGIC_OP_AND_REVERSE, VK_LOGIC_OP_COPY,
        VK_LOGIC_OP_AND_INVERTED, VK_LOGIC_OP_NO_OP,        VK_LOGIC_OP_XOR,         VK_LOGIC_OP_OR,
        VK_LOGIC_OP_NOR,          VK_LOGIC_OP_EQUIVALENT,   VK_LOGIC_OP_INVERT,      VK_LOGIC_OP_OR_REVERSE,
        VK_LOGIC_OP_COPY_INVERTED, VK_LOGIC_OP_OR_INVERTED, VK_LOGIC_OP_NAND,        VK_LOGIC_OP_SET,
    };
    return kOps[op & 0xfu];
}

// The locations of a module's stage inputs, as a mask.
std::uint32_t input_location_mask(const std::vector<std::uint32_t>& spirv) {
    constexpr std::uint32_t kOpVariable = 59;
    constexpr std::uint32_t kOpDecorate = 71;
    constexpr std::uint32_t kDecorationLocation = 30;
    constexpr std::uint32_t kStorageInput = 1;
    std::unordered_map<std::uint32_t, std::uint32_t> locations;
    std::vector<std::uint32_t> inputs;
    for (std::size_t at = 5; at < spirv.size() && (spirv[at] >> 16u) != 0u; at += spirv[at] >> 16u) {
        const std::uint32_t op = spirv[at] & 0xffffu;
        if (op == kOpDecorate && spirv[at + 2] == kDecorationLocation) {
            locations[spirv[at + 1]] = spirv[at + 3];
        } else if (op == kOpVariable && spirv[at + 3] == kStorageInput) {
            inputs.push_back(spirv[at + 2]);
        }
    }
    std::uint32_t mask = 0;
    for (const std::uint32_t input : inputs) {
        if (const auto it = locations.find(input); it != locations.end() && it->second < 32u) {
            mask |= 1u << it->second;
        }
    }
    return mask;
}

// GxVertexOut (vertex_loader.h) as the shaders' VSIn, at its semantics' locations (compile_hlsl): the eight texture
// coordinates one each.
constexpr VkVertexInputAttributeDescription kVertexAttributes[] = {
    {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},    {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12},
    {2, 0, VK_FORMAT_R32G32B32_SFLOAT, 24},   {3, 0, VK_FORMAT_R32G32B32_SFLOAT, 36},
    {4, 0, VK_FORMAT_R32_UINT, 48},           {5, 0, VK_FORMAT_R32_UINT, 52},
    {6, 0, VK_FORMAT_R32G32_SFLOAT, 56},      {7, 0, VK_FORMAT_R32G32_SFLOAT, 64},
    {8, 0, VK_FORMAT_R32G32_SFLOAT, 72},      {9, 0, VK_FORMAT_R32G32_SFLOAT, 80},
    {10, 0, VK_FORMAT_R32G32_SFLOAT, 88},     {11, 0, VK_FORMAT_R32G32_SFLOAT, 96},
    {12, 0, VK_FORMAT_R32G32_SFLOAT, 104},    {13, 0, VK_FORMAT_R32G32_SFLOAT, 112},
    {14, 0, VK_FORMAT_R32G32B32_UINT, 120},    {15, 0, VK_FORMAT_R32G32B32_UINT, 132},
};
static_assert(sizeof(GxVertexOut) == 144u);

template <typename T>
bool read_pod(std::ifstream& in, T& value) {
    return static_cast<bool>(in.read(reinterpret_cast<char*>(&value), sizeof(T)));
}

template <typename T>
void write_pod(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

// Written beside the target and renamed, so a reader never sees a partial file.
template <typename Write>
bool replace_file(const std::filesystem::path& path, Write&& write) {
    std::filesystem::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        write(out);
        if (!out) {
            return false;
        }
    }
    std::error_code error;
    std::filesystem::rename(temp, path, error);
    return !error;
}

}  // namespace

std::size_t PipelineCacheVk::UberKeyHasher::operator()(const UberKey& key) const noexcept {
    PsoKey pso{};
    pso.render_state = key.render_state;
    return static_cast<std::size_t>(combine64(pso.hash(), key.variant));
}

bool PipelineCacheVk::initialize(VkDevice device, const vk::GxLayout* layout, const std::filesystem::path& cache_dir,
                                 unsigned worker_count) {
    device_ = device;
    layout_ = layout;
    cache_dir_ = cache_dir;
    std::error_code error;
    std::filesystem::create_directories(cache_dir_, error);
    {
        const vk::DeviceQuirks& quirks = vk::Context::get().quirks();
        generator_defaults_.scalar_byte_masks = true;
        generator_defaults_.tev_zero_from_constant = quirks.tev_zero_from_constant;
        driver_key_ = vk::driver_key(vk::Context::get().identity());
    }
    load_compat();
    // Pipelines a crashed process was building: never again on this driver build. The last session ran this one when
    // its compat state loaded (each session saves it at start); another's crash (a hybrid laptop's other GPU) says
    // nothing about this one.
    for (const vk::CompileJournal::Left& left : journal_.open(cache_dir_ / "vk_compiling.bin")) {
        if (!compat_known_) {
            continue;
        }
        std::fprintf(stderr, "[vk-compat] %s pipeline %016llx crashed the driver last launch; skipped\n",
                     left.kind == kJournalSpecialised ? "specialised" : "uber",
                     static_cast<unsigned long long>(left.key));
        std::lock_guard<std::mutex> lock(compat_mutex_);
        (left.kind == kJournalSpecialised ? crashed_keys_ : crashed_uber_).insert(left.key);
    }
    save_compat();
    load_disk();
    uber_thread_ = std::thread([this] { compile_uber_shaders(); });
#if defined(__ANDROID__)
    // One thread builds the known pipelines at start on Android: Dolphin's DriverDetails
    // BUG_BROKEN_MULTITHREADED_SHADER_PRECOMPILATION (Android drivers crash on parallel precompiles).
    worker_count = 1u;
#endif
    const unsigned workers = std::max(1u, worker_count);
    for (unsigned i = 0; i < workers; ++i) {
        workers_.emplace_back([this] { worker_main(); });
    }
    // Every pipeline a previous session used is built again up front.
    {
        std::lock_guard<std::mutex> lock(entries_mutex_);
        std::lock_guard<std::mutex> compat_lock(compat_mutex_);
        for (const Entry& entry : entries_) {
            const std::uint64_t hash = entry.key.hash();
            if (refused_keys_.contains(hash) || crashed_keys_.contains(hash)) {
                published_[entry.key] = VK_NULL_HANDLE;
                continue;
            }
            in_flight_.insert(entry.key);
            enqueue(entry);
        }
    }
    prewarm_enqueued_.store(true, std::memory_order_release);
    std::fprintf(stderr, "[pipeline-cache-vk] shaders=%zu seed-shaders=%zu keys=%zu workers=%u\n", spirv_.size(),
                 seed_index_.size(), entries_.size(), workers);
    return true;
}

VkShaderModule PipelineCacheVk::module_for(const std::string& hlsl, vk::ShaderStage stage, bool dual_source) {
    const std::uint64_t hash = module_hash(hlsl, stage, dual_source);
    std::vector<std::uint32_t> spirv;
    {
        std::lock_guard<std::mutex> lock(modules_mutex_);
        if (const auto it = modules_.find(hash); it != modules_.end()) {
            return it->second;
        }
        if (const auto it = spirv_.find(hash); it != spirv_.end()) {
            spirv = it->second;
            shaders_from_cache_.fetch_add(1u, std::memory_order_relaxed);
        }
    }
    if (spirv.empty()) {
        if (const auto it = seed_index_.find(hash); it != seed_index_.end()) {
            spirv.resize(it->second.second);
            std::ifstream in(seed_shaders_path_, std::ios::binary);
            in.seekg(static_cast<std::streamoff>(it->second.first));
            if (in.read(reinterpret_cast<char*>(spirv.data()),
                        static_cast<std::streamsize>(spirv.size() * sizeof(std::uint32_t))) &&
                spirv[0] == 0x07230203u) {
                shaders_from_cache_.fetch_add(1u, std::memory_order_relaxed);
            } else {
                spirv.clear();
            }
        }
    }
    if (spirv.empty()) {
        const auto start = std::chrono::steady_clock::now();
        std::string error;
        spirv = compile_module(hlsl, stage, dual_source, error);
        compile_us_.fetch_add(elapsed_us(start), std::memory_order_relaxed);
        shader_compiles_.fetch_add(1u, std::memory_order_relaxed);
        if (spirv.empty()) {
            static std::atomic<int> reported{0};
            if (reported.fetch_add(1, std::memory_order_relaxed) < 4) {
                std::fprintf(stderr, "[pipeline-cache-vk] shader compile failed: %s\n", error.c_str());
            }
            return VK_NULL_HANDLE;
        }
    }
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = spirv.size() * sizeof(std::uint32_t);
    info.pCode = spirv.data();
    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device_, &info, nullptr, &module) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    const std::uint32_t inputs = stage == vk::ShaderStage::Vertex ? input_location_mask(spirv) : 0u;
    std::lock_guard<std::mutex> lock(modules_mutex_);
    if (const auto [it, inserted] = modules_.emplace(hash, module); !inserted) {
        vkDestroyShaderModule(device_, module, nullptr);  // another thread made it first
        return it->second;
    }
    if (stage == vk::ShaderStage::Vertex) {
        input_masks_.emplace(module, inputs);
    }
    if (spirv_.emplace(hash, std::move(spirv)).second) {
        unsaved_.store(true, std::memory_order_release);
    }
    return module;
}

VkPipeline PipelineCacheVk::create_pipeline(const RenderStateKey& rs, VkShaderModule vs, VkShaderModule ps,
                                            bool dual_source, bool export_depth, std::uint32_t compat,
                                            VkResult& result) const {
    result = VK_ERROR_INITIALIZATION_FAILED;
    if (vs == VK_NULL_HANDLE || ps == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }
    const bool geometry = (compat & kCompatNoGeometry) == 0u;
    VkPipelineShaderStageCreateInfo stages[4]{};
    std::uint32_t stage_count = 0;
    const auto add_stage = [&](VkShaderStageFlagBits stage, VkShaderModule module) {
        VkPipelineShaderStageCreateInfo& info = stages[stage_count++];
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        info.stage = stage;
        info.module = module;
        info.pName = "main";
    };
    add_stage(VK_SHADER_STAGE_VERTEX_BIT, vs);
    // Lines and points are expanded to quads in the geometry stage (GX widths), as on D3D12. A device without
    // geometry shaders draws them one pixel wide.
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    bool expanded = false;
    if (rs.primitive_topology == gpu::kTopologyTypePointValue) {
        topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        if (point_gs_ != VK_NULL_HANDLE && geometry) {
            add_stage(VK_SHADER_STAGE_GEOMETRY_BIT, point_gs_);
            expanded = true;
        }
    } else if (rs.primitive_topology == gpu::kTopologyTypeLineValue) {
        topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        if (line_gs_ != VK_NULL_HANDLE && geometry) {
            add_stage(VK_SHADER_STAGE_GEOMETRY_BIT, line_gs_);
            expanded = true;
        }
    }
    // A curved surface's patches (kVsKeyCurved): its vertex output variant's hull and domain stages. In the default
    // domain origin (upper left) the hull stage's triangle_cw, VertexOrderCw as glslang writes it, winds each patch's
    // triangles as the patch is wound, as on D3D12, so they cull as the triangles they replace.
    VkPipelineTessellationStateCreateInfo tessellation{VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO};
    tessellation.patchControlPoints = 3u;
    const bool curved = (rs.primitive_topology & kTopologyTypeMask) == kTopologyPatch;
    if (curved) {
        const unsigned variant = curved_variant(rs.primitive_topology);
        if (curved_hs_[variant] == VK_NULL_HANDLE || curved_ds_[variant] == VK_NULL_HANDLE) {
            return VK_NULL_HANDLE;
        }
        topology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        add_stage(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, curved_hs_[variant]);
        add_stage(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, curved_ds_[variant]);
    }
    add_stage(VK_SHADER_STAGE_FRAGMENT_BIT, ps);

    VkVertexInputBindingDescription binding{0, static_cast<std::uint32_t>(sizeof(GxVertexOut)),
                                            VK_VERTEX_INPUT_RATE_VERTEX};
    VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertex_input.vertexBindingDescriptionCount = 1;
    vertex_input.pVertexBindingDescriptions = &binding;
    // Only the attributes the vertex shader reads (the others would be fetched for nothing).
    std::uint32_t inputs = ~0u;
    {
        std::lock_guard<std::mutex> lock(modules_mutex_);
        if (const auto it = input_masks_.find(vs); it != input_masks_.end()) {
            inputs = it->second;
        }
    }
    std::array<VkVertexInputAttributeDescription, std::size(kVertexAttributes)> attributes{};
    std::uint32_t attribute_count = 0;
    for (const VkVertexInputAttributeDescription& attribute : kVertexAttributes) {
        if ((inputs >> attribute.location) & 1u) {
            attributes[attribute_count++] = attribute;
        }
    }
    vertex_input.vertexAttributeDescriptionCount = attribute_count;
    vertex_input.pVertexAttributeDescriptions = attributes.data();
    VkPipelineInputAssemblyStateCreateInfo input_assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    input_assembly.topology = topology;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    // GX screen space is y-down against D3D-style NDC, so GX front faces are the rasteriser's back faces (the D3D12
    // pipelines make the same swap); the viewport flips y as D3D's does, so clockwise is front as there.
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;
    // D3D clamps fragment depth, a shader-written one too, to the viewport's range and clips primitives to it (GX
    // depth clears rely on the clamp: near == far). Without VK_EXT_depth_clip_enable only depth-writing shaders clamp,
    // and their primitives go unclipped in depth.
    VkPipelineRasterizationDepthClipStateCreateInfoEXT depth_clip{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT};
    const bool clamp = layout_->depth_clamp && (compat & kCompatNoDepthClamp) == 0u;
    if (clamp && layout_->depth_clip_enable && (compat & kCompatNoDepthClip) == 0u) {
        raster.depthClampEnable = VK_TRUE;
        depth_clip.depthClipEnable = VK_TRUE;
        raster.pNext = &depth_clip;
    } else if (clamp && export_depth) {
        raster.depthClampEnable = VK_TRUE;
    }
    switch (static_cast<CullMode>(rs.cull)) {
    case CullMode::Front: raster.cullMode = VK_CULL_MODE_BACK_BIT; break;
    case CullMode::Back: raster.cullMode = VK_CULL_MODE_FRONT_BIT; break;
    default: raster.cullMode = VK_CULL_MODE_NONE; break;
    }
    if ((topology != VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST && topology != VK_PRIMITIVE_TOPOLOGY_PATCH_LIST) || expanded) {
        raster.cullMode = VK_CULL_MODE_NONE;  // GX culls polygons (a curved surface's patches too), not lines or points
    }
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = static_cast<VkSampleCountFlagBits>(1u << rs.samples_log2);

    // Reversed-Z against GX's z24 (the projection negates z; RendererVk's viewport maps the GX depth range), so the
    // compare directions flip, as on D3D12.
    static constexpr VkCompareOp kDepthCompare[8] = {
        VK_COMPARE_OP_NEVER,   VK_COMPARE_OP_GREATER,   VK_COMPARE_OP_EQUAL,         VK_COMPARE_OP_GREATER_OR_EQUAL,
        VK_COMPARE_OP_LESS,    VK_COMPARE_OP_NOT_EQUAL, VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_ALWAYS,
    };
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    const std::uint32_t zb = rs.zmode_bits;
    if ((zb & 1u) != 0u) {
        depth.depthTestEnable = VK_TRUE;
        depth.depthCompareOp = kDepthCompare[(zb >> 1u) & 0x7u];
        depth.depthWriteEnable = (zb & (1u << 4u)) != 0u ? VK_TRUE : VK_FALSE;
    }

    const std::uint32_t bb = rs.blend_bits;
    VkPipelineColorBlendAttachmentState target{};
    target.colorWriteMask =
        ((bb & (1u << 25u)) != 0u ? VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT
                                  : 0u) |
        ((bb & (1u << 26u)) != 0u ? VK_COLOR_COMPONENT_A_BIT : 0u);
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    if ((bb & 1u) != 0u) {
        target.blendEnable = VK_TRUE;
        if ((bb & 2u) != 0u) {
            // GX subtract: dst - src, factors one.
            target.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            target.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            target.colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;
        } else {
            target.srcColorBlendFactor = gx_src_blend((bb >> 2u) & 0x7u);
            target.dstColorBlendFactor = gx_dst_blend((bb >> 5u) & 0x7u);
            target.colorBlendOp = VK_BLEND_OP_ADD;
            if (rs.pixfmt != 1u) {
                // No destination alpha in this pixel format: it reads as one.
                const auto no_dst_alpha = [](VkBlendFactor factor) {
                    return factor == VK_BLEND_FACTOR_DST_ALPHA ? VK_BLEND_FACTOR_ONE
                        : factor == VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA ? VK_BLEND_FACTOR_ZERO : factor;
                };
                target.srcColorBlendFactor = no_dst_alpha(target.srcColorBlendFactor);
                target.dstColorBlendFactor = no_dst_alpha(target.dstColorBlendFactor);
            }
        }
        target.srcAlphaBlendFactor = to_alpha_blend(target.srcColorBlendFactor);
        target.dstAlphaBlendFactor = to_alpha_blend(target.dstColorBlendFactor);
        target.alphaBlendOp = target.colorBlendOp;
        if ((bb & (1u << 27u)) != 0u && dual_source && layout_->dual_source_blend &&
            (compat & kCompatNoDualSource) == 0u) {
            // The pixel shader's second output carries the TEV alpha the colour blends against.
            const auto second_source = [](VkBlendFactor factor) {
                return factor == VK_BLEND_FACTOR_SRC_ALPHA ? VK_BLEND_FACTOR_SRC1_ALPHA
                    : factor == VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA ? VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA : factor;
            };
            target.srcColorBlendFactor = second_source(target.srcColorBlendFactor);
            target.dstColorBlendFactor = second_source(target.dstColorBlendFactor);
        }
        if ((bb & (1u << 28u)) != 0u) {  // constant destination alpha is independent
            target.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            target.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            target.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        if ((bb & kRenderStateZeroAlpha) != 0u) {  // constant destination alpha 0, drawn single-source
            target.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            target.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            target.alphaBlendOp = VK_BLEND_OP_ADD;
        }
    } else if ((bb & (1u << 8u)) != 0u && layout_->logic_op && (compat & kCompatNoLogicOp) == 0u) {
        blend.logicOpEnable = VK_TRUE;
        blend.logicOp = gx_logic_op((bb >> 9u) & 0xfu);
    }
    blend.attachmentCount = 1;
    blend.pAttachments = &target;

    const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = static_cast<std::uint32_t>(std::size(dynamic_states));
    dynamic.pDynamicStates = dynamic_states;

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.stageCount = stage_count;
    info.pStages = stages;
    info.pVertexInputState = &vertex_input;
    info.pInputAssemblyState = &input_assembly;
    info.pTessellationState = curved ? &tessellation : nullptr;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout_->layout;
    info.renderPass = layout_->pass_for(rs.samples_log2, false);
    const TestRefusals& refuse = test_refusals();
    const bool uber = std::find(uber_ps_.begin(), uber_ps_.end(), ps) != uber_ps_.end();
    const bool uber_vertex = vs != VK_NULL_HANDLE && vs == uber_vs_;
    if ((refuse.dual_source && dual_source && layout_->dual_source_blend) || (refuse.depth_clip && raster.pNext) ||
        (refuse.depth_clamp && raster.depthClampEnable) || (refuse.logic_op && blend.logicOpEnable) ||
        (refuse.geometry && expanded) || (refuse.specialised && !uber) || (refuse.uber && uber) ||
        (refuse.uber_vs && uber_vertex)) {
        result = VK_ERROR_INITIALIZATION_FAILED;
        return VK_NULL_HANDLE;
    }
    VkPipeline pipeline = VK_NULL_HANDLE;
    result = vkCreateGraphicsPipelines(device_, driver_cache_, 1, &info, nullptr, &pipeline);
    return result == VK_SUCCESS ? pipeline : VK_NULL_HANDLE;
}

std::uint32_t PipelineCacheVk::applicable_compat(const RenderStateKey& rs, bool dual_source, bool export_depth) const {
    std::uint32_t bits = 0;
    if (layout_->depth_clamp && layout_->depth_clip_enable) {
        bits |= kCompatNoDepthClip;
    }
    if (layout_->depth_clamp && (layout_->depth_clip_enable || export_depth)) {
        bits |= kCompatNoDepthClamp;
    }
    if ((rs.blend_bits & 1u) == 0u && (rs.blend_bits & (1u << 8u)) != 0u && layout_->logic_op) {
        bits |= kCompatNoLogicOp;
    }
    if (dual_source && layout_->dual_source_blend) {
        bits |= kCompatNoDualSource;
    }
    if ((rs.primitive_topology == gpu::kTopologyTypePointValue && point_gs_ != VK_NULL_HANDLE) ||
        (rs.primitive_topology == gpu::kTopologyTypeLineValue && line_gs_ != VK_NULL_HANDLE)) {
        bits |= kCompatNoGeometry;
    }
    return bits;
}

template <typename Make>
VkPipeline PipelineCacheVk::with_fallbacks(Make&& make, std::uint32_t applicable, const char* what,
                                           const std::string& detail, bool* transient) {
    const std::uint32_t base = device_compat_.load(std::memory_order_acquire) & applicable;
    VkResult result = VK_SUCCESS;
    VkPipeline pipeline = make(base, result);
    if (pipeline != VK_NULL_HANDLE) {
        return pipeline;
    }
    refusals_.fetch_add(1u, std::memory_order_relaxed);
    report_refusal(what, detail, result, base);
    if (result == VK_ERROR_OUT_OF_HOST_MEMORY || result == VK_ERROR_OUT_OF_DEVICE_MEMORY ||
        result == VK_ERROR_DEVICE_LOST) {
        if (transient != nullptr) {
            *transient = true;
        }
        return VK_NULL_HANDLE;  // not this pipeline's form
    }
    // Without each optional feature it uses, cheapest loss first, then without all of them.
    for (std::uint32_t bit = 0; bit < kCompatBits; ++bit) {
        const std::uint32_t flag = 1u << bit;
        if ((applicable & flag) == 0u || (base & flag) != 0u) {
            continue;
        }
        VkResult retry = VK_SUCCESS;
        pipeline = make(base | flag, retry);
        if (pipeline != VK_NULL_HANDLE) {
            std::fprintf(stderr, "[vk-compat] %s accepted without %s (%s)\n", what, compat_name(flag), detail.c_str());
            learn(flag, 0u);
            return pipeline;
        }
    }
    if (std::popcount(applicable & ~base) > 1) {
        VkResult retry = VK_SUCCESS;
        pipeline = make(applicable, retry);
        if (pipeline != VK_NULL_HANDLE) {
            std::fprintf(stderr, "[vk-compat] %s accepted without its optional features (%s)\n", what,
                         detail.c_str());
            return pipeline;
        }
    }
    return VK_NULL_HANDLE;
}

void PipelineCacheVk::report_refusal(const char* what, const std::string& detail, VkResult result,
                                     std::uint32_t compat) {
    const std::uint32_t count = refusals_.load(std::memory_order_relaxed);
    if (count <= 16u) {
        std::fprintf(stderr, "[vk-compat] driver refused %s: VkResult %d, without 0x%x (%s)\n", what,
                     static_cast<int>(result), compat, detail.c_str());
    }
    if (refusal_reported_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    // The first refusal: everything needed to find out why, in the game's log.
    std::fprintf(stderr, "[vk-compat] device: %s\n", vk::describe(vk::Context::get().identity()).c_str());
    const std::string driver = host_window::driver_log();
    if (!driver.empty()) {
        std::fprintf(stderr, "[vk-compat] driver log:\n%s[vk-compat] end of driver log\n", driver.c_str());
    }
    std::fflush(stderr);
}

void PipelineCacheVk::learn(std::uint32_t compat_bit, std::uint32_t generator_bit) {
    const std::size_t index = compat_bit != 0u ? static_cast<std::size_t>(std::countr_zero(compat_bit)) : kCompatBits;
    // Two pipelines fixed the same way: leave it out of every later one (and the next launches').
    if (fixes_[index].fetch_add(1u, std::memory_order_acq_rel) + 1u != 2u) {
        return;
    }
    if (compat_bit != 0u) {
        device_compat_.fetch_or(compat_bit, std::memory_order_acq_rel);
        std::fprintf(stderr, "[vk-compat] building every pipeline without %s on this driver\n",
                     compat_name(compat_bit));
    }
    if (generator_bit != 0u) {
        generator_bits_.fetch_xor(generator_bit, std::memory_order_acq_rel);
        std::fprintf(stderr, "[vk-compat] the other TEV zero form for every pixel shader on this driver\n");
    }
    save_compat();
}

void PipelineCacheVk::load_compat() {
    std::ifstream in(cache_dir_ / "vk_compat.txt");
    std::string line;
    char compiler[40];
    std::snprintf(compiler, sizeof(compiler), "compiler %016llx",
                  static_cast<unsigned long long>(compiler_identity()));
    if (!std::getline(in, line) || line != "nebula-vk-compat 2" || !std::getline(in, line) ||
        line != "driver " + driver_key_ || !std::getline(in, line) || line != compiler) {
        return;  // another driver build or compiler (or none yet): the self-test runs, earlier refusals are dropped
    }
    compat_known_ = true;
    std::lock_guard<std::mutex> lock(compat_mutex_);
    while (std::getline(in, line)) {
        std::istringstream fields(line);
        std::string name;
        std::uint64_t value = 0;
        if (!(fields >> name >> std::hex >> value)) {
            continue;
        }
        if (name == "compat") {
            device_compat_.store(static_cast<std::uint32_t>(value), std::memory_order_release);
        } else if (name == "generator") {
            generator_bits_.store(static_cast<std::uint32_t>(value), std::memory_order_release);
        } else if (name == "refused") {
            refused_keys_.insert(value);
        } else if (name == "crashed") {
            crashed_keys_.insert(value);
        } else if (name == "uber-crashed") {
            crashed_uber_.insert(value);
        } else if (name == "uber-refused") {
            uber_refused_.store(static_cast<std::uint32_t>(value), std::memory_order_release);
        }
    }
}

void PipelineCacheVk::save_compat() {
    std::ostringstream out;
    out << "nebula-vk-compat 2\ndriver " << driver_key_ << "\n" << std::hex;
    out << "compiler " << std::setw(16) << std::setfill('0') << compiler_identity() << std::setfill(' ') << "\n";
    out << "compat " << device_compat_.load(std::memory_order_acquire) << "\n";
    out << "generator " << generator_bits_.load(std::memory_order_acquire) << "\n";
    out << "uber-refused " << uber_refused_.load(std::memory_order_acquire) << "\n";
    {
        std::lock_guard<std::mutex> lock(compat_mutex_);
        for (const std::uint64_t key : refused_keys_) out << "refused " << key << "\n";
        for (const std::uint64_t key : crashed_keys_) out << "crashed " << key << "\n";
        for (const std::uint64_t key : crashed_uber_) out << "uber-crashed " << key << "\n";
    }
    const std::string text = out.str();
    replace_file(cache_dir_ / "vk_compat.txt", [&](std::ofstream& file) {
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
    });
}

bool PipelineCacheVk::unexpanded_points(const RenderStateKey& rs, std::uint32_t compat) const noexcept {
    return rs.primitive_topology == gpu::kTopologyTypePointValue &&
        (point_gs_ == VK_NULL_HANDLE || (compat & kCompatNoGeometry) != 0u);
}

VkShaderModule PipelineCacheVk::point_size_variant(const std::string& vertex_hlsl) {
    return module_for(vk::with_point_size(vertex_hlsl), vk::ShaderStage::Vertex, false);
}

ShaderGenerator PipelineCacheVk::generator_for(std::uint32_t generator_bits) const {
    ShaderGenOptions options = generator_defaults_;
    if ((generator_bits & kGeneratorTevZeroFromConstant) != 0u) {
        options.tev_zero_from_constant = !options.tev_zero_from_constant;
    }
    ShaderGenerator generator;
    generator.set_options(options);
    return generator;
}

VkPipeline PipelineCacheVk::build(const Entry& entry) {
    const bool dual_source = (entry.ps_key.flags & 0x80u) != 0u;
    const bool export_depth = (entry.ps_key.flags & 0x02u) != 0u && (entry.ps_key.flags & 0x01u) != 0u;
    const RenderStateKey& rs = entry.key.render_state;
    const std::uint32_t generator_bits = generator_bits_.load(std::memory_order_acquire);
    const ShaderGenerator generator = generator_for(generator_bits);
    const std::string vs_hlsl = generator.generate_vs(entry.vs_key);
    const std::string ps_hlsl = generator.generate_ps(entry.ps_key);
    const VkShaderModule vs = module_for(vs_hlsl, vk::ShaderStage::Vertex, false);
    const VkShaderModule ps = module_for(ps_hlsl, vk::ShaderStage::Fragment, dual_source);
    if (vs == VK_NULL_HANDLE || ps == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;  // the HLSL compile failed (module_for logged it)
    }
    {
        std::lock_guard<std::mutex> lock(compat_mutex_);
        if (rejected_modules_.contains(module_hash(vs_hlsl, vk::ShaderStage::Vertex, false)) ||
            rejected_modules_.contains(module_hash(ps_hlsl, vk::ShaderStage::Fragment, dual_source))) {
            refused_keys_.insert(entry.key.hash());
            return VK_NULL_HANDLE;  // a shader this driver rejects on its own: no form of the pipeline can work
        }
    }
    VkShaderModule ps_single = VK_NULL_HANDLE;
    const auto make = [&](std::uint32_t compat, VkResult& result) {
        VkShaderModule pixel = ps;
        const bool dual = dual_source && (compat & kCompatNoDualSource) == 0u;
        if (dual_source && !dual) {
            if (ps_single == VK_NULL_HANDLE) {
                ps_single = module_for(ps_hlsl, vk::ShaderStage::Fragment, false);
            }
            pixel = ps_single;
        }
        const VkShaderModule vertex = unexpanded_points(rs, compat) ? point_size_variant(vs_hlsl) : vs;
        return create_pipeline(rs, vertex, pixel, dual, export_depth, compat, result);
    };
    const std::uint64_t key_hash = entry.key.hash();
    const vk::CompileJournal::Guard marked(journal_, kJournalSpecialised, key_hash);
    if (const unsigned crash_at = test_refusals().crash_at; crash_at != 0u) {
        static std::atomic<unsigned> builds{0};
        if (builds.fetch_add(1u, std::memory_order_relaxed) + 1u == crash_at) {
            std::fprintf(stderr, "[vk-compat] test crash inside specialised pipeline %016llx\n",
                         static_cast<unsigned long long>(key_hash));
            std::fflush(stderr);
            std::abort();
        }
    }
    char detail[96];
    std::snprintf(detail, sizeof(detail), "key %016llx, ", static_cast<unsigned long long>(key_hash));
    const std::uint32_t applicable = applicable_compat(rs, dual_source, export_depth);
    bool transient = false;
    VkPipeline pipeline =
        with_fallbacks(make, applicable, "a specialised pipeline", detail + describe_render_state(rs), &transient);
    if (pipeline == VK_NULL_HANDLE && transient) {
        return VK_NULL_HANDLE;  // out of memory: the uber pipeline draws it this session, nothing remembered
    }
    if (pipeline == VK_NULL_HANDLE) {
        // The other form of the TEV stages' zero inputs (DeviceQuirks::tev_zero_from_constant).
        const std::string other_ps = generator_for(generator_bits ^ kGeneratorTevZeroFromConstant)
                                         .generate_ps(entry.ps_key);
        if (other_ps != ps_hlsl) {
            VkResult result = VK_SUCCESS;
            pipeline = create_pipeline(rs, vs, module_for(other_ps, vk::ShaderStage::Fragment, dual_source),
                                       dual_source, export_depth,
                                       device_compat_.load(std::memory_order_acquire) & applicable, result);
            if (pipeline != VK_NULL_HANDLE) {
                std::fprintf(stderr, "[vk-compat] specialised pipeline %016llx accepted with the other TEV zero form\n",
                             static_cast<unsigned long long>(key_hash));
                learn(0u, kGeneratorTevZeroFromConstant);
            }
        }
    }
    if (pipeline == VK_NULL_HANDLE) {
        classify_refusal(rs, vs_hlsl, ps_hlsl, dual_source, export_depth,
                         device_compat_.load(std::memory_order_acquire) & applicable);
        {
            std::lock_guard<std::mutex> lock(compat_mutex_);
            refused_keys_.insert(key_hash);
        }
        static std::atomic<bool> dumped{false};
        if (!dumped.exchange(true, std::memory_order_acq_rel)) {
            std::fprintf(stderr, "[vk-compat] refused pipeline's vertex shader:\n%s\n[vk-compat] its pixel shader:\n%s\n"
                         "[vk-compat] end of shaders\n", vs_hlsl.c_str(), ps_hlsl.c_str());
        }
        save_compat();
        return VK_NULL_HANDLE;
    }
    pso_compiles_.fetch_add(1u, std::memory_order_relaxed);
    return pipeline;
}

void PipelineCacheVk::classify_refusal(const RenderStateKey& rs, const std::string& vs_hlsl,
                                       const std::string& ps_hlsl, bool dual_source, bool export_depth,
                                       std::uint32_t compat) {
    const std::uint64_t vs_hash = module_hash(vs_hlsl, vk::ShaderStage::Vertex, false);
    const std::uint64_t ps_hash = module_hash(ps_hlsl, vk::ShaderStage::Fragment, dual_source);
    {
        std::lock_guard<std::mutex> lock(compat_mutex_);
        if (rejected_modules_.contains(vs_hash) || rejected_modules_.contains(ps_hash)) {
            return;
        }
    }
    // diagnose_uber's simple pair: each shader with a partner every driver takes.
    const ShaderGenerator generator = generator_for(generator_bits_.load(std::memory_order_acquire));
    PixelShaderKey simple_key{};
    if (dual_source) {
        simple_key.flags |= 0x80u;
    }
    const VkShaderModule simple_vs = module_for(generator.generate_vs(diagnosis_vs_key()), vk::ShaderStage::Vertex, false);
    const VkShaderModule simple_ps = module_for(generator.generate_ps(simple_key), vk::ShaderStage::Fragment, dual_source);
    const VkShaderModule vs = module_for(vs_hlsl, vk::ShaderStage::Vertex, false);
    const VkShaderModule ps = module_for(ps_hlsl, vk::ShaderStage::Fragment, dual_source);
    VkResult vs_result = VK_SUCCESS;
    VkResult ps_result = VK_SUCCESS;
    if (const VkPipeline p = create_pipeline(rs, vs, simple_ps, dual_source, false, compat, vs_result)) {
        vkDestroyPipeline(device_, p, nullptr);
    }
    if (const VkPipeline p = create_pipeline(rs, simple_vs, ps, dual_source, export_depth, compat, ps_result)) {
        vkDestroyPipeline(device_, p, nullptr);
    }
    // Out of memory is no verdict on a shader.
    const auto rejected = [](VkResult r) {
        return r != VK_SUCCESS && r != VK_ERROR_OUT_OF_HOST_MEMORY && r != VK_ERROR_OUT_OF_DEVICE_MEMORY;
    };
    std::lock_guard<std::mutex> lock(compat_mutex_);
    if (rejected(vs_result)) {
        rejected_modules_.insert(vs_hash);
    }
    if (rejected(ps_result)) {
        rejected_modules_.insert(ps_hash);
    }
    static std::atomic<int> reported{0};
    if (reported.fetch_add(1, std::memory_order_relaxed) < 8) {
        std::fprintf(stderr, "[vk-compat] refused pipeline: VS with simple PS %s (%d), PS with simple VS %s (%d)\n",
                     rejected(vs_result) ? "refused" : "accepted", static_cast<int>(vs_result),
                     rejected(ps_result) ? "refused" : "accepted", static_cast<int>(ps_result));
    }
}

void PipelineCacheVk::compile_uber_shaders() noexcept {
    try {
        const auto start = std::chrono::steady_clock::now();
        const std::string vs(uber_vs_source());
        uber_vs_ = module_for(vs, vk::ShaderStage::Vertex, false);
        // The effects layer's variants are the ray tracer's (Direct3D 12 only).
        for (unsigned variant = 0; variant < kUberPsVariants; ++variant) {
            if (!uber_ps_variant_layer(variant)) {
                uber_ps_[variant] = module_for(uber_ps_hlsl(variant), vk::ShaderStage::Fragment,
                                               uber_ps_variant_dual_source(variant));
            }
        }
        if (layout_->geometry_shaders) {
            line_gs_ = module_for(std::string(ShaderGenerator::line_geometry_shader_source()),
                                  vk::ShaderStage::Geometry, false);
            point_gs_ = module_for(std::string(ShaderGenerator::point_geometry_shader_source()),
                                   vk::ShaderStage::Geometry, false);
        }
        // Curved surfaces, where the device tessellates: offered once every variant's stages compiled.
        bool curved = layout_->tessellation;
        for (unsigned variant = 0; variant < curved_hs_.size() && curved; ++variant) {
            curved_hs_[variant] =
                module_for(ShaderGenerator::curved_stage_source(variant, false), vk::ShaderStage::Hull, false);
            curved_ds_[variant] =
                module_for(ShaderGenerator::curved_stage_source(variant, true), vk::ShaderStage::Domain, false);
            curved = curved_hs_[variant] != VK_NULL_HANDLE && curved_ds_[variant] != VK_NULL_HANDLE;
        }
        set_curved_surfaces_offered(curved);
        // Every variant Vulkan builds (the effects layer's are not).
        bool complete = uber_vs_ != VK_NULL_HANDLE;
        for (unsigned variant = 0; variant < kUberPsVariants; ++variant) {
            complete = complete && (uber_ps_variant_layer(variant) || uber_ps_[variant] != VK_NULL_HANDLE);
        }
        std::fprintf(stderr, "[uber-pipeline-vk] %s in %llu ms\n", complete ? "shaders ready" : "shader compile failed",
                     static_cast<unsigned long long>(elapsed_us(start) / 1000u));
        // A new driver build's self-test before any stand-in is used: a variant the driver refuses is then never tried
        // on the render thread (each refusal costs the driver's compile and the fallbacks', a stall per render state).
        if (complete && !compat_known_) {
            self_test();
        }
        uber_ready_.store(complete, std::memory_order_release);
    } catch (...) {
        // Without the uber shaders every first use builds its specialised pipeline on the render thread.
    }
}

std::size_t PipelineCacheVk::build_shader_seed(const std::filesystem::path& keys, const std::filesystem::path& out,
                                               unsigned threads, std::string& error) {
    std::vector<Entry> entries;
    {
        std::ifstream in(keys, std::ios::binary);
        std::uint32_t magic = 0, version = 0, count = 0;
        if (!in || !read_pod(in, magic) || !read_pod(in, version) || !read_pod(in, count) || magic != kKeyFileMagic ||
            version != kKeyFileVersion || count > kMaxRecords) {
            error = "not a key file of this build: " + keys.string();
            return 0;
        }
        for (std::uint32_t i = 0; i < count; ++i) {
            Entry entry;
            if (!read_pod(in, entry)) {
                break;
            }
            if (entry.key.vs_hash == entry.vs_key.hash() && entry.key.ps_hash == entry.ps_key.hash()) {
                entries.push_back(entry);
            }
        }
    }
    struct Source {
        std::string hlsl;
        vk::ShaderStage stage;
        bool dual_source;
        std::vector<std::uint32_t> spirv;
    };
    std::map<std::uint64_t, Source> sources;  // ordered: the same keys make the same file
    const auto add = [&](std::string hlsl, vk::ShaderStage stage, bool dual_source) {
        const std::uint64_t hash = module_hash(hlsl, stage, dual_source);
        sources.try_emplace(hash, Source{std::move(hlsl), stage, dual_source, {}});
    };
    // Every module module_for can be asked for with these keys on any Vulkan device: both TEV zero forms (DeviceQuirks
    // and the fallback's flip), the point-size vertex shaders (no geometry shaders), the single-source form of a
    // dual-source pixel shader (kCompatNoDualSource), diagnose_uber's simple pair, the uber and geometry shaders.
    for (const bool zero_from_constant : {false, true}) {
        ShaderGenOptions options;
        options.scalar_byte_masks = true;
        options.tev_zero_from_constant = zero_from_constant;
        ShaderGenerator generator;
        generator.set_options(options);
        const auto add_pair = [&](const VertexShaderKey& vs_key, const PixelShaderKey& ps_key, bool points) {
            std::string vs = generator.generate_vs(vs_key);
            if (points) {
                add(vk::with_point_size(vs), vk::ShaderStage::Vertex, false);
            }
            add(std::move(vs), vk::ShaderStage::Vertex, false);
            const bool dual = (ps_key.flags & 0x80u) != 0u;
            std::string ps = generator.generate_ps(ps_key);
            if (dual) {
                add(ps, vk::ShaderStage::Fragment, false);
            }
            add(std::move(ps), vk::ShaderStage::Fragment, dual);
        };
        for (const Entry& entry : entries) {
            add_pair(entry.vs_key, entry.ps_key,
                     entry.key.render_state.primitive_topology == gpu::kTopologyTypePointValue);
        }
        PixelShaderKey dual_key{};
        dual_key.flags |= 0x80u;
        add_pair(diagnosis_vs_key(), PixelShaderKey{}, false);
        add_pair(diagnosis_vs_key(), dual_key, false);
    }
    add(std::string(uber_vs_source()), vk::ShaderStage::Vertex, false);
    add(vk::with_point_size(std::string(uber_vs_source())), vk::ShaderStage::Vertex, false);
    for (unsigned variant = 0; variant < kUberPsVariants; ++variant) {
        if (uber_ps_variant_layer(variant)) {
            continue;
        }
        const bool dual = uber_ps_variant_dual_source(variant);
        add(uber_ps_hlsl(variant), vk::ShaderStage::Fragment, dual);
        if (dual) {
            add(uber_ps_hlsl(variant), vk::ShaderStage::Fragment, false);
        }
    }
    add(std::string(ShaderGenerator::line_geometry_shader_source()), vk::ShaderStage::Geometry, false);
    add(std::string(ShaderGenerator::point_geometry_shader_source()), vk::ShaderStage::Geometry, false);

    std::vector<Source*> work;
    work.reserve(sources.size());
    for (auto& [hash, source] : sources) {
        work.push_back(&source);
    }
    std::atomic<std::size_t> next{0};
    std::mutex error_mutex;
    const auto compile_all = [&] {
        for (std::size_t i; (i = next.fetch_add(1u, std::memory_order_relaxed)) < work.size();) {
            std::string failure;
            work[i]->spirv = compile_module(work[i]->hlsl, work[i]->stage, work[i]->dual_source, failure);
            if (work[i]->spirv.empty()) {
                std::lock_guard<std::mutex> lock(error_mutex);
                error += failure + "\n";
            }
        }
    };
    std::vector<std::thread> pool;
    for (unsigned t = 1; t < std::max(threads, 1u); ++t) {
        pool.emplace_back(compile_all);
    }
    compile_all();
    for (std::thread& thread : pool) {
        thread.join();
    }
    if (!error.empty()) {
        return 0;
    }
    const auto count = static_cast<std::uint32_t>(std::min<std::size_t>(sources.size(), kMaxRecords));
    replace_file(out, [&](std::ofstream& file) {
        write_pod(file, kShaderFileMagic);
        write_pod(file, kShaderFileVersion);
        write_pod(file, compiler_identity());
        write_pod(file, count);
        std::uint32_t written = 0;
        for (const auto& [hash, source] : sources) {
            if (written++ == count) {
                break;
            }
            write_pod(file, hash);
            write_pod(file, static_cast<std::uint32_t>(source.spirv.size()));
            file.write(reinterpret_cast<const char*>(source.spirv.data()),
                       static_cast<std::streamsize>(source.spirv.size() * sizeof(std::uint32_t)));
        }
    });
    return count;
}

void PipelineCacheVk::diagnose_uber(const RenderStateKey& render_state, unsigned variant) {
    // The simplest specialised shaders (default keys) with each uber shader: the half that still fails is the culprit.
    const ShaderGenerator generator = generator_for(generator_bits_.load(std::memory_order_acquire));
    const bool dual = uber_ps_variant_dual_source(variant);
    const bool export_depth = uber_ps_variant_export_depth(variant);
    const VkShaderModule simple_vs = module_for(generator.generate_vs(diagnosis_vs_key()), vk::ShaderStage::Vertex, false);
    PixelShaderKey ps_key{};
    if (dual) {
        ps_key.flags |= 0x80u;
    }
    const VkShaderModule simple_ps = module_for(generator.generate_ps(ps_key), vk::ShaderStage::Fragment, dual);
    VkResult uber_vs_result = VK_SUCCESS;
    VkResult uber_ps_result = VK_SUCCESS;
    const std::uint32_t compat = device_compat_.load(std::memory_order_acquire);
    if (const VkPipeline p = create_pipeline(render_state, uber_vs_, simple_ps, dual, false, compat, uber_vs_result)) {
        vkDestroyPipeline(device_, p, nullptr);
    }
    if (const VkPipeline p =
            create_pipeline(render_state, simple_vs, uber_ps_[variant], dual, export_depth, compat, uber_ps_result)) {
        vkDestroyPipeline(device_, p, nullptr);
    }
    std::fprintf(stderr, "[vk-compat] uber variant %u diagnosis: uber VS with a simple PS %s (%d); a simple VS with the "
                 "uber PS %s (%d)\n", variant, uber_vs_result == VK_SUCCESS ? "accepted" : "refused",
                 static_cast<int>(uber_vs_result), uber_ps_result == VK_SUCCESS ? "accepted" : "refused",
                 static_cast<int>(uber_ps_result));
    // Once per launch, the refused half cut down, each form with the simple other half: the feature the driver rejects.
    if (uber_probed_) {
        return;
    }
    uber_probed_ = true;
    const auto run = [&](bool pixel, const std::string& source, const std::vector<UberProbe>& probes) {
        for (const UberProbe& probe : probes) {
            const std::string text = apply_probe(source, probe);
            VkResult result = VK_ERROR_INITIALIZATION_FAILED;
            if (!text.empty()) {
                const VkShaderModule module =
                    module_for(text, pixel ? vk::ShaderStage::Fragment : vk::ShaderStage::Vertex, pixel && dual);
                const VkPipeline p = pixel
                    ? create_pipeline(render_state, simple_vs, module, dual, export_depth, compat, result)
                    : create_pipeline(render_state, module, simple_ps, dual, false, compat, result);
                if (p != VK_NULL_HANDLE) {
                    vkDestroyPipeline(device_, p, nullptr);
                }
            }
            std::fprintf(stderr, "[vk-compat] uber %s probe %s: %s (%d)\n", pixel ? "PS" : "VS", probe.name,
                         text.empty() ? "not run, the source changed" : result == VK_SUCCESS ? "accepted" : "refused",
                         static_cast<int>(result));
        }
    };
    if (uber_ps_result != VK_SUCCESS) {
        run(true, uber_ps_hlsl(variant), uber_ps_probes());
    }
    if (uber_vs_result != VK_SUCCESS) {
        run(false, std::string(uber_vs_source()), uber_vs_probes());
    }
}

// A new driver build's first launch: the uber pipeline of every pixel-shader variant, and of each optional feature,
// built and dropped (the driver's cache keeps them), so a feature this driver refuses is found before a draw needs it.
void PipelineCacheVk::self_test() {
    const auto start = std::chrono::steady_clock::now();
    RenderStateKey opaque{};
    opaque.blend_bits = (1u << 25u) | (1u << 26u);
    opaque.zmode_bits = 1u | (3u << 1u) | (1u << 4u);
    opaque.cull = static_cast<std::uint8_t>(CullMode::Back);
    opaque.pixfmt = 1u;
    opaque.primitive_topology = gpu::kTopologyTypeTriangleValue;
    std::vector<std::pair<RenderStateKey, unsigned>> tests;
    for (unsigned variant = 0; variant < kUberPsVariants; ++variant) {
        if (!uber_ps_variant_layer(variant)) {
            tests.emplace_back(opaque, variant);
        }
    }
    RenderStateKey blended = opaque;  // alpha blend against the second output
    blended.blend_bits |= 1u | (4u << 2u) | (5u << 5u) | (1u << 27u);
    tests.emplace_back(blended, 1u);
    RenderStateKey logic = opaque;
    logic.blend_bits |= (1u << 8u) | (6u << 9u);
    tests.emplace_back(logic, 0u);
    RenderStateKey lines = opaque;
    lines.primitive_topology = gpu::kTopologyTypeLineValue;
    tests.emplace_back(lines, 0u);
    RenderStateKey points = opaque;
    points.primitive_topology = gpu::kTopologyTypePointValue;
    tests.emplace_back(points, 0u);
    unsigned accepted = 0;
    for (const auto& [render_state, variant] : tests) {
        const std::uint64_t hash = uber_hash(render_state, variant);
        {
            std::lock_guard<std::mutex> lock(compat_mutex_);
            if (crashed_uber_.contains(hash)) {
                continue;
            }
        }
        const vk::CompileJournal::Guard marked(journal_, kJournalUber, hash);
        const bool dual = uber_ps_variant_dual_source(variant);
        const bool export_depth = uber_ps_variant_export_depth(variant);
        char what[48];
        std::snprintf(what, sizeof(what), "the self-test's uber pipeline %u", variant);
        const VkPipeline pipeline = with_fallbacks(
            [&](std::uint32_t compat, VkResult& result) {
                const unsigned v = (compat & kCompatNoDualSource) != 0u ? variant & ~1u : variant;
                const VkShaderModule vertex = unexpanded_points(render_state, compat)
                    ? point_size_variant(std::string(uber_vs_source())) : uber_vs_;
                return create_pipeline(render_state, vertex, uber_ps_[v], uber_ps_variant_dual_source(v),
                                       export_depth, compat, result);
            },
            applicable_compat(render_state, dual, export_depth), what, describe_render_state(render_state));
        if (pipeline != VK_NULL_HANDLE) {
            ++accepted;
            vkDestroyPipeline(device_, pipeline, nullptr);
        } else if (render_state == opaque) {
            // The variant's shaders themselves: never used for a stand-in on this driver, and which of them fails.
            uber_refused_.fetch_or(1u << variant, std::memory_order_acq_rel);
            diagnose_uber(render_state, variant);
        }
    }
    std::fprintf(stderr, "[vk-compat] self-test: %u of %zu pipelines accepted in %llu ms, without 0x%x\n", accepted,
                 tests.size(), static_cast<unsigned long long>(elapsed_us(start) / 1000u),
                 device_compat_.load(std::memory_order_acquire));
    compat_known_ = true;
    save_compat();
}

VkPipeline PipelineCacheVk::uber_pipeline(const RenderStateKey& render_state, unsigned variant) {
    UberKey key{};
    key.render_state = render_state;
    key.render_state.blend_bits &= ~(kRenderStateMotionTarget | kRenderStateReactiveTarget);
    key.variant = variant;
    if (const auto it = uber_pipelines_.find(key); it != uber_pipelines_.end()) {
        return it->second;
    }
    const std::uint64_t hash = uber_hash(key.render_state, variant);
    if ((uber_refused_.load(std::memory_order_acquire) & (1u << variant)) != 0u) {
        uber_pipelines_.emplace(key, VK_NULL_HANDLE);
        return VK_NULL_HANDLE;
    }
    {
        std::lock_guard<std::mutex> lock(compat_mutex_);
        if (crashed_uber_.contains(hash)) {
            uber_pipelines_.emplace(key, VK_NULL_HANDLE);
            return VK_NULL_HANDLE;
        }
    }
    const vk::CompileJournal::Guard marked(journal_, kJournalUber, hash);
    const bool export_depth = uber_ps_variant_export_depth(variant);
    bool transient = false;
    char what[40];
    std::snprintf(what, sizeof(what), "uber pipeline %u", variant);
    const VkPipeline pipeline = with_fallbacks(
        [&](std::uint32_t compat, VkResult& result) {
            const unsigned v = (compat & kCompatNoDualSource) != 0u ? variant & ~1u : variant;
            const VkShaderModule vertex = unexpanded_points(key.render_state, compat)
                ? point_size_variant(std::string(uber_vs_source())) : uber_vs_;
            return create_pipeline(key.render_state, vertex, uber_ps_[v], uber_ps_variant_dual_source(v),
                                   export_depth, compat, result);
        },
        applicable_compat(key.render_state, uber_ps_variant_dual_source(variant), export_depth), what,
        describe_render_state(key.render_state), &transient);
    if (pipeline == VK_NULL_HANDLE && !transient) {
        // Accepted in no form: the variant's shaders themselves (every render state fails alike).
        uber_refused_.fetch_or(1u << variant, std::memory_order_acq_rel);
        std::fprintf(stderr, "[vk-compat] uber variant %u refused; its draws build specialised pipelines\n", variant);
        save_compat();
    }
    if (!transient) {
        uber_pipelines_.emplace(key, pipeline);  // a refused one stays refused
    }
    return pipeline;
}

bool PipelineCacheVk::uber_usable(const PixelShaderKey& ps_key) const noexcept {
    return (uber_refused_.load(std::memory_order_acquire) & (1u << uber_ps_variant(ps_key))) == 0u;
}

bool PipelineCacheVk::unqueue(const PsoKey& key) {
    std::lock_guard<std::mutex> lock(jobs_mutex_);
    const auto it = std::find_if(jobs_.begin(), jobs_.end(), [&](const Entry& job) { return job.key == key; });
    if (it == jobs_.end()) {
        return false;
    }
    jobs_.erase(it);
    return true;
}

VkPipeline PipelineCacheVk::stand_in(const PsoKey& key, const PixelShaderKey& ps_key) {
    if (!uber_ready_.load(std::memory_order_acquire)) {
        // The uber shaders are compiling (a second at most): wait for them rather than drop the draw.
        for (int i = 0; i < 2000 && !uber_ready_.load(std::memory_order_acquire) && uber_thread_.joinable(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    if (!uber_ready_.load(std::memory_order_acquire)) {
        return VK_NULL_HANDLE;  // the draw is skipped
    }
    ++uber_fallbacks_;
    last_get_used_uber_ = true;
    return uber_pipeline(flat_render_state(key.render_state), uber_ps_variant(ps_key));
}

void PipelineCacheVk::enqueue(const Entry& entry) {
    {
        std::lock_guard<std::mutex> lock(jobs_mutex_);
        jobs_.push_back(entry);
    }
    jobs_cv_.notify_one();
}

void PipelineCacheVk::worker_main() {
    galaxy::host::name_current_thread(L"Nebula PSO Build");
#if !defined(_WIN32)
    // Below the game and render threads: background builds never take a frame's CPU time.
    (void)setpriority(PRIO_PROCESS, static_cast<id_t>(gettid()), 10);
#endif
    for (;;) {
        Entry entry;
        {
            std::unique_lock<std::mutex> lock(jobs_mutex_);
            jobs_cv_.wait(lock, [this] { return !jobs_.empty(); });
            entry = jobs_.front();
            jobs_.pop_front();
        }
        const VkPipeline pipeline = build(entry);
        std::lock_guard<std::mutex> lock(jobs_mutex_);
        completions_.push_back({entry.key, pipeline});
        // Every seeded pipeline built: their modules are in vk_shaders.bin at the next save (this device's forms only),
        // which then deletes the seeds (the app installs them again with its next update).
        if (jobs_.empty() && !seed_index_.empty() && prewarm_enqueued_.load(std::memory_order_acquire) &&
            !seed_drained_.exchange(true, std::memory_order_acq_rel)) {
            unsaved_.store(true, std::memory_order_release);
        }
    }
}

VkPipeline PipelineCacheVk::get_if_ready(const PsoKey& key, const PixelShaderKey& ps_key,
                                         const VertexShaderKey& vs_key) {
    if (const auto it = published_.find(key); it != published_.end()) {
        return it->second;
    }
    if (in_flight_.contains(key)) {
        return VK_NULL_HANDLE;
    }
    const Entry entry{key, vs_key, ps_key};
    {
        std::lock_guard<std::mutex> lock(entries_mutex_);
        if (entry_keys_.insert(key).second) {
            entries_.push_back(entry);
            unsaved_.store(true, std::memory_order_release);
        }
    }
    {
        std::lock_guard<std::mutex> lock(compat_mutex_);
        const std::uint64_t hash = key.hash();
        if (refused_keys_.contains(hash) || crashed_keys_.contains(hash)) {
            published_[key] = VK_NULL_HANDLE;
            return VK_NULL_HANDLE;
        }
    }
    in_flight_.insert(key);
    enqueue(entry);
    return VK_NULL_HANDLE;
}

VkPipeline PipelineCacheVk::get(const PsoKey& key, const PixelShaderKey& ps_key, const VertexShaderKey& vs_key) {
    last_get_used_uber_ = false;
    if (memo_pipeline_ != VK_NULL_HANDLE && memo_key_ == key) {
        ++specialized_hits_;
        return memo_pipeline_;
    }
    if (const auto it = published_.find(key); it != published_.end()) {
        if (it->second != VK_NULL_HANDLE) {
            ++specialized_hits_;
            memo_key_ = key;
            memo_pipeline_ = it->second;
            return it->second;
        }
        return stand_in(key, ps_key);  // refused by the driver in every form: never built again
    }
    if (!in_flight_.contains(key)) {
        const Entry entry{key, vs_key, ps_key};
        {
            std::lock_guard<std::mutex> lock(entries_mutex_);
            if (entry_keys_.insert(key).second) {
                entries_.push_back(entry);
                unsaved_.store(true, std::memory_order_release);
            }
        }
        bool refused = false;
        {
            std::lock_guard<std::mutex> lock(compat_mutex_);
            const std::uint64_t hash = key.hash();
            refused = refused_keys_.contains(hash) || crashed_keys_.contains(hash);
        }
        if (refused) {
            published_[key] = VK_NULL_HANDLE;
            return stand_in(key, ps_key);
        }
        if (uber_ready_.load(std::memory_order_acquire) && uber_usable(ps_key)) {
            // The uber pipeline draws it while a worker builds its own.
            in_flight_.insert(key);
            enqueue(entry);
            if (const VkPipeline uber = stand_in(key, ps_key); uber != VK_NULL_HANDLE) {
                return uber;
            }
        }
    } else {
        drain_completions();
        if (const auto it = published_.find(key); it != published_.end() && it->second != VK_NULL_HANDLE) {
            return it->second;
        }
        if (uber_ready_.load(std::memory_order_acquire) && uber_usable(ps_key)) {
            if (const VkPipeline uber = stand_in(key, ps_key); uber != VK_NULL_HANDLE) {
                return uber;
            }
        }
    }
    // Nothing draws it yet (the uber shaders still compiling, or refused by this driver): its own pipeline is built
    // here, now, rather than the draw dropped; a queued prewarm job for it is taken back.
    if (unqueue(key)) {
        in_flight_.erase(key);
    }
    const VkPipeline pipeline = build(Entry{key, vs_key, ps_key});
    published_[key] = pipeline;
    if (pipeline != VK_NULL_HANDLE) {
        memo_key_ = key;
        memo_pipeline_ = pipeline;
    }
    return pipeline;
}

void PipelineCacheVk::drain_completions() {
    std::vector<Completion> done;
    {
        std::lock_guard<std::mutex> lock(jobs_mutex_);
        done.swap(completions_);
    }
    for (const Completion& completion : done) {
        in_flight_.erase(completion.key);
        auto [it, inserted] = published_.emplace(completion.key, completion.pipeline);
        if (!inserted) {
            // Built on the render thread meanwhile: keep that one.
            if (completion.pipeline != VK_NULL_HANDLE && completion.pipeline != it->second) {
                vkDestroyPipeline(device_, completion.pipeline, nullptr);
            }
        }
    }
}

void PipelineCacheVk::load_disk() {
    // SPIR-V of another compiler (or file version) is dropped, and so is the driver's cache of the pipelines made from
    // it: none of them can be asked for again, and the driver would keep them in every later save.
    bool other_compiler = false;
    {
        std::ifstream in(cache_dir_ / "vk_shaders.bin", std::ios::binary);
        std::uint32_t magic = 0, version = 0, count = 0;
        std::uint64_t identity = 0;
        other_compiler = in.good();
        if (in && read_pod(in, magic) && read_pod(in, version) && magic == kShaderFileMagic &&
            version == kShaderFileVersion && read_pod(in, identity) && identity == compiler_identity() &&
            read_pod(in, count) && count <= kMaxRecords) {
            other_compiler = false;
            for (std::uint32_t i = 0; i < count; ++i) {
                std::uint64_t hash = 0;
                std::uint32_t words = 0;
                if (!read_pod(in, hash) || !read_pod(in, words) || words == 0u || words > kMaxSpirvWords) {
                    break;
                }
                std::vector<std::uint32_t> spirv(words);
                if (!in.read(reinterpret_cast<char*>(spirv.data()),
                             static_cast<std::streamsize>(words * sizeof(std::uint32_t))) ||
                    spirv[0] != 0x07230203u) {
                    break;
                }
                spirv_.emplace(hash, std::move(spirv));
            }
        }
        spirv_on_disk_ = spirv_.size();
    }
    {
        std::ifstream in(cache_dir_ / "vk_keys.bin", std::ios::binary);
        std::uint32_t magic = 0, version = 0, count = 0;
        if (in && read_pod(in, magic) && read_pod(in, version) && read_pod(in, count) && magic == kKeyFileMagic &&
            version == kKeyFileVersion && count <= kMaxRecords) {
            for (std::uint32_t i = 0; i < count; ++i) {
                Entry entry;
                if (!read_pod(in, entry)) {
                    break;
                }
                // The key must be the one its shader keys make (a different build changes the hashes).
                if (entry.key.vs_hash != entry.vs_key.hash() || entry.key.ps_hash != entry.ps_key.hash()) {
                    continue;
                }
                if (entry_keys_.insert(entry.key).second) {
                    entries_.push_back(entry);
                }
            }
        }
        entries_on_disk_ = entries_.size();
    }
    // Their SPIR-V for every device (build_shader_seed), indexed here and read as modules are made.
    {
        seed_shaders_path_ = cache_dir_ / "vk_seed_shaders.bin";
        std::ifstream in(seed_shaders_path_, std::ios::binary);
        std::uint32_t magic = 0, version = 0, count = 0;
        std::uint64_t identity = 0;
        if (in && read_pod(in, magic) && read_pod(in, version) && magic == kShaderFileMagic &&
            version == kShaderFileVersion && read_pod(in, identity) && identity == compiler_identity() &&
            read_pod(in, count) && count <= kMaxRecords) {
            for (std::uint32_t i = 0; i < count; ++i) {
                std::uint64_t hash = 0;
                std::uint32_t words = 0;
                if (!read_pod(in, hash) || !read_pod(in, words) || words == 0u || words > kMaxSpirvWords) {
                    break;
                }
                const auto offset = static_cast<std::uint64_t>(in.tellg());
                if (!in.seekg(static_cast<std::streamoff>(words) * 4, std::ios::cur)) {
                    break;
                }
                seed_index_.emplace(hash, std::make_pair(offset, words));
            }
        }
    }
    // The pipelines the whole game uses (captured on a PC over every galaxy; the Android app installs the file): built
    // by the workers from the first launch, in the order the game first needs them.
    {
        std::ifstream in(cache_dir_ / "vk_seed_keys.bin", std::ios::binary);
        std::uint32_t magic = 0, version = 0, count = 0;
        std::size_t seeded = 0;
        if (in && read_pod(in, magic) && read_pod(in, version) && read_pod(in, count) && magic == kKeyFileMagic &&
            version == kKeyFileVersion && count <= kMaxRecords) {
            for (std::uint32_t i = 0; i < count; ++i) {
                Entry entry;
                if (!read_pod(in, entry)) {
                    break;
                }
                if (entry.key.vs_hash != entry.vs_key.hash() || entry.key.ps_hash != entry.ps_key.hash()) {
                    continue;
                }
                if (entry_keys_.insert(entry.key).second) {
                    entries_.push_back(entry);
                    ++seeded;
                }
            }
            std::fprintf(stderr, "[pipeline-cache-vk] %zu seed keys added (%u in the seed)\n", seeded, count);
        }
    }
    std::vector<char> data;
    if (!other_compiler) {
        std::ifstream in(cache_dir_ / "vk_pipelines.bin", std::ios::binary | std::ios::ate);
        if (in) {
            const std::streamsize size = in.tellg();
            if (size > 0 && size < (256 << 20)) {
                data.resize(static_cast<std::size_t>(size));
                in.seekg(0);
                if (!in.read(data.data(), size)) {
                    data.clear();
                }
            }
        }
    }
    VkPipelineCacheCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    info.initialDataSize = data.size();
    info.pInitialData = data.empty() ? nullptr : data.data();
    if (vkCreatePipelineCache(device_, &info, nullptr, &driver_cache_) != VK_SUCCESS) {
        // The driver rejected the data (another driver or device): start empty.
        info.initialDataSize = 0;
        info.pInitialData = nullptr;
        (void)vkCreatePipelineCache(device_, &info, nullptr, &driver_cache_);
    }
}

void PipelineCacheVk::save_disk() noexcept {
    try {
        std::unordered_map<std::uint64_t, std::vector<std::uint32_t>> spirv;
        {
            std::lock_guard<std::mutex> lock(modules_mutex_);
            spirv = spirv_;
        }
        std::vector<Entry> entries;
        {
            std::lock_guard<std::mutex> lock(entries_mutex_);
            entries = entries_;
        }
        unsaved_.store(false, std::memory_order_release);
        const bool seed_drained = seed_drained_.load(std::memory_order_acquire);
        const auto count = static_cast<std::uint32_t>(std::min<std::size_t>(spirv.size(), kMaxRecords));
        const bool shaders_saved = replace_file(cache_dir_ / "vk_shaders.bin", [&](std::ofstream& out) {
            write_pod(out, kShaderFileMagic);
            write_pod(out, kShaderFileVersion);
            write_pod(out, compiler_identity());
            write_pod(out, count);
            std::uint32_t written = 0;
            for (const auto& [hash, words] : spirv) {
                if (written++ == count) {
                    break;
                }
                write_pod(out, hash);
                write_pod(out, static_cast<std::uint32_t>(words.size()));
                out.write(reinterpret_cast<const char*>(words.data()),
                          static_cast<std::streamsize>(words.size() * sizeof(std::uint32_t)));
            }
        });
        const auto key_count = static_cast<std::uint32_t>(std::min<std::size_t>(entries.size(), kMaxRecords));
        const bool keys_saved = replace_file(cache_dir_ / "vk_keys.bin", [&](std::ofstream& out) {
            write_pod(out, kKeyFileMagic);
            write_pod(out, kKeyFileVersion);
            write_pod(out, key_count);
            for (std::uint32_t i = 0; i < key_count; ++i) {
                write_pod(out, entries[i]);
            }
        });
        if (driver_cache_ != VK_NULL_HANDLE) {
            std::size_t size = 0;
            if (vkGetPipelineCacheData(device_, driver_cache_, &size, nullptr) == VK_SUCCESS && size != 0u) {
                std::vector<char> data(size);
                if (vkGetPipelineCacheData(device_, driver_cache_, &size, data.data()) == VK_SUCCESS) {
                    replace_file(cache_dir_ / "vk_pipelines.bin", [&](std::ofstream& out) {
                        out.write(data.data(), static_cast<std::streamsize>(size));
                    });
                }
            }
        }
        if (seed_drained && shaders_saved && keys_saved) {
            std::error_code ignored;
            std::filesystem::remove(cache_dir_ / "vk_seed_shaders.bin", ignored);
            std::filesystem::remove(cache_dir_ / "vk_seed_keys.bin", ignored);
        }
        spirv_on_disk_ = count;
        entries_on_disk_ = key_count;
    } catch (...) {
        unsaved_.store(true, std::memory_order_release);  // retried at the next flush
    }
}

void PipelineCacheVk::flush_disk() {
    if (flush_thread_.joinable()) {
        flush_thread_.join();
    }
    save_disk();
}

void PipelineCacheVk::flush_disk_async() {
    if (flush_thread_.joinable() || !has_unsaved_records()) {
        return;
    }
    flush_done_.store(false, std::memory_order_release);
    flush_thread_ = std::thread([this] {
        save_disk();
        flush_done_.store(true, std::memory_order_release);
    });
}

void PipelineCacheVk::poll_disk_flush() {
    // Joined once it has finished, so the render thread never waits for the disk.
    if (flush_thread_.joinable() && flush_done_.load(std::memory_order_acquire)) {
        flush_thread_.join();
    }
}

PipelineCacheVk::CompileStats PipelineCacheVk::compile_stats() const {
    CompileStats stats;
    stats.pso_compiles = pso_compiles_.load(std::memory_order_relaxed);
    stats.shader_pair_compiles = shader_compiles_.load(std::memory_order_relaxed);
    stats.blobs_from_cache = shaders_from_cache_.load(std::memory_order_relaxed);
    stats.blob_compile_us = compile_us_.load(std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(modules_mutex_);
        stats.shader_records = spirv_.size();
    }
    stats.shader_records_on_disk = spirv_on_disk_;
    {
        std::lock_guard<std::mutex> lock(entries_mutex_);
        stats.pso_keys = entries_.size();
    }
    stats.pso_keys_on_disk = entries_on_disk_;
    stats.shader_cache_at_capacity = stats.shader_records >= kMaxRecords;
    stats.pso_cache_at_capacity = stats.pso_keys >= kMaxRecords;
    return stats;
}

}  // namespace galaxy::gx
