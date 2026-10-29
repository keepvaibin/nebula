// GxBackend: decodes the game's GX command stream and drives the renderer.
// gx_shim.cpp exposes it through the galaxy::gx API in gx_d3d12.h.

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#pragma warning(push, 0)
#include <Windows.h>
#if !defined(NEBULA_GPU_VULKAN)
#include <d3d12.h>
#endif
#pragma warning(pop)
#include <intrin.h>
#else
#include <time.h>
#if defined(__x86_64__)
#include <immintrin.h>
#endif
#endif
#include "galaxy/platform_time.h"
#include "galaxy/platform_paths.h"
#include "galaxy/perf_hint.h"

#include "galaxy/gx/gx_backend.h"
#include "galaxy/gx/dependency_draw_memo.h"
#include "galaxy/gx/dependency_range_memo.h"
#include "galaxy/gx/fifo_cache_fingerprint.h"
#include "galaxy/gx/rt_tev_eval.h"

#include "galaxy/frame_cadence_diagnostics.h"
#include "galaxy/gx/enhanced_lighting.h"
#include "galaxy/gx/line_point_raster.h"
#include "galaxy/gx/rt_materials.h"
#include "galaxy/gx/rt_surfaces.h"
#include "galaxy/gx/texture_sampling.h"
#include "galaxy/gx/curved_surfaces.h"
#include "galaxy/gx/shader_gen.h"
#include "galaxy/gx/uber_constants.h"
#include "galaxy/gx/uber_shaders.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/windows_power.h"
#include "galaxy/frame_timeline.h"
#include "galaxy/gx/sorted_dirty_ranges.h"
#include "galaxy/gx/snapshot_region_lookup.h"
#include "galaxy/motion_signals.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <cmath>
#include <limits>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace galaxy::gx {

std::uint32_t pack_sampler_key(
    const TexMode& mode,
    bool generated_mips,
    std::uint8_t mip_levels,
    std::uint8_t lod_shift_x16,
    bool arbitrary,
    std::uint8_t efb_scale_log2_x32) noexcept {
    std::uint32_t min_filter =
        static_cast<std::uint32_t>(mode.min_filter) & 0x7u;
    std::uint32_t min_lod =
        static_cast<std::uint32_t>(mode.min_lod_x16) & 0xFFu;
    std::uint32_t max_lod =
        static_cast<std::uint32_t>(mode.max_lod_x16) & 0xFFu;
    const bool guest_mips = (min_filter & 0x3u) != 0u;
    bool full_chain = false;
    if (generated_mips && mip_levels > 1u &&
        min_filter == static_cast<std::uint32_t>(TexMinFilter::Linear) &&
        mode.mag_filter == TexMagFilter::Linear) {
        min_filter =
            static_cast<std::uint32_t>(TexMinFilter::LinearMipLinear);
        max_lod = std::min<std::uint32_t>(
            0xFFu,
            static_cast<std::uint32_t>(mip_levels - 1u) * 16u);
        full_chain = true;
    }
    std::uint32_t lod_bias =
        static_cast<std::uint32_t>(static_cast<std::uint8_t>(mode.lod_bias_x32)) & 0xFFu;
    if (arbitrary && guest_mips) {
        const int bias = static_cast<int>(static_cast<std::int8_t>(mode.lod_bias_x32)) +
                         static_cast<int>(efb_scale_log2_x32) - 2 * static_cast<int>(lod_shift_x16);
        lod_bias = static_cast<std::uint32_t>(static_cast<std::uint8_t>(
            static_cast<std::int8_t>(std::clamp(bias, -128, 127))));
        max_lod = 0xFFu;
        min_filter = (min_filter & 0x4u) | 0x3u;
    } else if (lod_shift_x16 != 0u) {
        if (guest_mips) {
            min_lod = min_lod == 0u ? 0u : std::min<std::uint32_t>(0xFFu, min_lod + lod_shift_x16);
            max_lod = std::min<std::uint32_t>(0xFFu, max_lod + lod_shift_x16);
        } else if (!full_chain) {
            min_filter = static_cast<std::uint32_t>(
                min_filter >= static_cast<std::uint32_t>(TexMinFilter::Linear)
                    ? TexMinFilter::LinearMipLinear
                    : TexMinFilter::NearMipNear);
            min_lod = 0u;
            max_lod = lod_shift_x16;
        }
    }
    return
          (static_cast<std::uint32_t>(mode.wrap_s) & 0x3u)
        | ((static_cast<std::uint32_t>(mode.wrap_t) & 0x3u) << 2)
        | ((static_cast<std::uint32_t>(mode.mag_filter) & 0x1u) << 4)
        | (min_filter << 5)
        | (lod_bias << 8)
        | (min_lod << 16)
        | (max_lod << 24);
}

void boot_step(const char* step) noexcept;

namespace {

static_assert(kEfbWidth == 640u && kEfbHeight == 528u);

// Each texture coordinate's scale (texels per unit, s and t). A material sets them (BP 0x30-0x3F); after the game's own
// TEV order, indirect order or texture changes (`derive`) a coordinate the stages sample takes the size of the texture
// sampled with it - indirect stages, then TEV stages, the last one winning - as GX re-derived them then.
std::array<std::array<std::uint32_t, 2>, 8> coordinate_scales(const GxState& state, bool derive) {
    std::array<std::array<std::uint32_t, 2>, 8> scales{};
    for (unsigned c = 0; c < 8u; ++c) {
        scales[c] = {(state.bp(static_cast<std::uint8_t>(bp::kTexCoordSizeBase + c * 2u)) & 0xFFFFu) + 1u,
                     (state.bp(static_cast<std::uint8_t>(bp::kTexCoordSizeBase + c * 2u + 1u)) & 0xFFFFu) + 1u};
    }
    if (!derive) {
        return scales;
    }
    const GenMode gm = state.gen_mode();
    const std::uint32_t ind_ref = state.bp(bp::kIndRef);
    for (unsigned i = 0; i < gm.num_ind_stages && i < 4u; ++i) {
        const TexImage image = state.tex_image((ind_ref >> (6u * i)) & 7u);
        scales[(ind_ref >> (6u * i + 3u)) & 7u] = {std::max<std::uint32_t>(image.width, 1u),
                                                   std::max<std::uint32_t>(image.height, 1u)};
    }
    for (unsigned s = 0; s < gm.num_tev_stages && s < kMaxTevStages; ++s) {
        const TevOrder order = state.tev_order(s);
        if (order.tex_enable) {
            const TexImage image = state.tex_image(order.texmap);
            scales[order.texcoord & 7u] = {std::max<std::uint32_t>(image.width, 1u),
                                           std::max<std::uint32_t>(image.height, 1u)};
        }
    }
    return scales;
}

std::atomic<unsigned> s_layer_2d_scale{0u};
// The 3D image's scale as shown, at each scene's begin (scene_display_scale).
std::atomic<unsigned> s_scene_display_scale{0u};

// A TEV colour register (an 8-bit sRGB colour over 255) with `share` of its chroma taken, its luminance kept (linear
// light, Rec. 709): a clear ice or crystal draw's tint (rt_materials.h kRtIceTintKept). A signed register outside 0..1
// (a TEV bias) keeps its value.
void clear_register_tint(float (&c)[4], float share) noexcept {
    if (!(c[0] >= 0.0f && c[0] <= 1.0f && c[1] >= 0.0f && c[1] <= 1.0f && c[2] >= 0.0f && c[2] <= 1.0f)) {
        return;
    }
    float lin[3];
    for (unsigned i = 0; i < 3u; ++i) {
        lin[i] = c[i] <= 0.04045f ? c[i] / 12.92f : std::pow((c[i] + 0.055f) / 1.055f, 2.4f);
    }
    const float y = 0.2126f * lin[0] + 0.7152f * lin[1] + 0.0722f * lin[2];
    for (unsigned i = 0; i < 3u; ++i) {
        const float l = lin[i] + (y - lin[i]) * share;
        c[i] = l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f;
    }
}

// True when the alpha test passes every fragment.
bool alpha_test_keeps_all(const PixelShaderKey& key) noexcept {
    const auto always = static_cast<std::uint8_t>(CompareFunc::Always);
    const auto never = static_cast<std::uint8_t>(CompareFunc::Never);
    const bool a0 = key.alpha_comp0 == always;
    const bool a1 = key.alpha_comp1 == always;
    const bool n0 = key.alpha_comp0 == never;
    const bool n1 = key.alpha_comp1 == never;
    switch (static_cast<AlphaTestLogic>(key.alpha_logic)) {
    case AlphaTestLogic::And:
        return a0 && a1;
    case AlphaTestLogic::Or:
        return a0 || a1;
    case AlphaTestLogic::Xor:
        return (a0 && n1) || (n0 && a1);
    case AlphaTestLogic::Xnor:
        return (a0 && a1) || (n0 && n1);
    }
    return false;
}

// Test aid for runs without a display: every Nth swap-chain present is read
// back and written to GALAXY_PRESENT_SNAPSHOT_DIR\present_<n>.ppm, limited to
// presents GALAXY_PRESENT_SNAPSHOT_FIRST..GALAXY_PRESENT_SNAPSHOT_LAST.
std::uint64_t g_snapshot_presents = 0u;

std::string present_snapshot_path() {
    const auto read_u64 = [](const char* name, std::uint64_t fallback) {
        char value[32]{};
        std::size_t length = 0;
        return getenv_s(&length, value, sizeof(value), name) == 0 && length > 1u
            ? std::strtoull(value, nullptr, 10) : fallback;
    };
    static const std::uint64_t every = read_u64("GALAXY_PRESENT_SNAPSHOT_EVERY", 0u);
    static const std::uint64_t first = read_u64("GALAXY_PRESENT_SNAPSHOT_FIRST", 0u);
    static const std::uint64_t last = read_u64(
        "GALAXY_PRESENT_SNAPSHOT_LAST", std::numeric_limits<std::uint64_t>::max());
    static const std::string directory = [] {
        char value[MAX_PATH]{};
        std::size_t length = 0;
        return getenv_s(&length, value, sizeof(value),
                        "GALAXY_PRESENT_SNAPSHOT_DIR") == 0 && length > 1u
            ? std::string(value) : std::string{};
    }();
    const std::uint64_t presents = ++g_snapshot_presents;
    if (every == 0u || directory.empty() || presents < first || presents > last ||
        presents % every != 0u) {
        return {};
    }
    // Its time, for latency measurements against the input's (GALAXY_INPUT_POINTER_PATH's jumps).
    std::cerr << "[snapshot] present=" << presents << " qpc=" << galaxy::qpc_now() << '\n';
    return directory + "\\present_" + std::to_string(presents) + ".ppm";
}

class PeEventSink final : public FifoSink {
public:
    PeEventSink(GxState& state, const GxServices* services)
        : state_(state), services_(services) {}

    [[nodiscard]] std::size_t draw_payload_size(
        std::uint8_t vtxfmt,
        std::uint16_t vertex_count) const override {
        return VertexLoader::source_vertex_size(state_.vertex_desc(vtxfmt)) *
               static_cast<std::size_t>(vertex_count);
    }

    void on_draw(
        PrimitiveClass,
        std::uint8_t vtxfmt,
        FifoCursor& cursor) override {
        const std::uint16_t vertex_count = cursor.read_u16();
        (void)cursor.take(draw_payload_size(vtxfmt, vertex_count));
    }

    void on_efb_copy(std::uint32_t) override {}

    void on_pe_finish() override {
        if (services_ == nullptr || services_->gx_pe_finish == nullptr) {
            throw std::runtime_error(
                "GX PE_FINISH requires a native gx_pe_finish service");
        }
        services_->gx_pe_finish(services_->user);
    }

    void on_pe_token(std::uint16_t token, bool interrupt) override {
        if (services_ == nullptr || services_->gx_pe_token == nullptr) {
            throw std::runtime_error(
                "GX PE_TOKEN requires a native gx_pe_token service");
        }
        services_->gx_pe_token(services_->user, token, interrupt);
    }

    void on_invalidate_textures() override {}
    void on_tlut_load() override {}
    void on_invalidate_vertex_cache() override {}

private:
    GxState& state_;
    const GxServices* services_;
};

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

// GALAXY_TRACE_LIGHTS=1: the actor-light aim every 600 scenes, the first moved draws and the CPU
// cycles ([rt-actor-light]); =2: the aim of every scene.
std::uint64_t actor_light_trace_level() {
    static const std::uint64_t level = read_env_u64("GALAXY_TRACE_LIGHTS", 0u);
    return level;
}

bool trace_actor_lights_enabled() {
    return actor_light_trace_level() != 0u;
}

// The light trace's cycle counter: the TSC on x86, the monotonic clock's nanoseconds elsewhere.
std::uint64_t trace_cycles() noexcept {
#if defined(_M_X64) || defined(__x86_64__)
    return __rdtsc();
#else
    return static_cast<std::uint64_t>(galaxy::qpc_now());
#endif
}

// Test hook: GALAXY_TEST_RENDER_STALL_MS=<ms>,<every N render chunks> makes
// the render thread sleep, to reproduce a slow GPU/driver on any machine.
struct RenderStallInjection {
    std::uint32_t ms = 0u;
    std::uint32_t every = 0u;
};

const RenderStallInjection& render_stall_injection() {
    static const RenderStallInjection injection = [] {
        RenderStallInjection result{};
        char value[32]{};
        std::size_t length = 0;
        if (getenv_s(&length, value, sizeof(value), "GALAXY_TEST_RENDER_STALL_MS") != 0 || length <= 1) {
            return result;
        }
        const char* end = value + length - 1;
        const char* comma = std::find(static_cast<const char*>(value), end, ',');
        std::from_chars(value, comma, result.ms);
        if (comma != end) {
            std::from_chars(comma + 1, end, result.every);
        }
        if (result.every == 0u) {
            result.ms = 0u;
        }
        return result;
    }();
    return injection;
}

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

// select_xfb_for_present holds the last good frame while the VI-selected XFB is briefly not
// presentable (it is re-acquired as the next copy target during a double or triple buffer flip),
// and falls forward to the latest copy only after a sustained miss. 30 presents (about 0.5 s) ride
// out the 1-3 frame jitter of a flip or of the render queue and still end a real freeze.
constexpr std::uint64_t kMaxXfbPreserveAge = 30u;

// A game frame presents when the chunk with its display copy ends, not at the VI that later names it (that VI finds it
// shown): about 14 ms sooner at 60 fps. Not while frames interpolate (their sequences keep their own clock).
// GALAXY_PRESENT_ON_COPY=0 waits for the VI (A/B).
bool present_on_copy_enabled() {
    static const bool enabled = read_env_u64("GALAXY_PRESENT_ON_COPY", 1u) != 0u;
    return enabled;
}

// The XFB a VI shows: the newest finished copy in the place of the one it names (newest_finished_xfb), a game frame
// sooner (47 ms from the copy to the present at 60 fps before). Not while sub-frames show (subframes_shown): their
// sequence ends on the frame the VI names, and the newer one shown before its sub-frames would step time back each
// game frame (fades and motion see-saw at 60 Hz). GALAXY_PRESENT_NEWEST_XFB=0 shows the named (A/B).
const XfbTexture* displayed_xfb(const XfbTexture* named, const XfbTexture* latest, bool latest_displayed,
                                bool subframes) {
    static const bool enabled = read_env_u64("GALAXY_PRESENT_NEWEST_XFB", 1u) != 0u;
    return enabled && latest_displayed && !subframes ? newest_finished_xfb(named, latest) : named;
}

// GALAXY_GX_FRAME_TIMING_SAMPLE=N times every draw of one frame in N (N >= 2), so a recording holds
// attributed frames among frames at full speed; the per-draw clocks are too costly for every frame.
bool frame_gx_timing_sample_enabled(std::uint64_t frame_index) {
    static const std::uint64_t period = read_env_u64(
        "GALAXY_GX_FRAME_TIMING_SAMPLE", 0u);
    if (period < 2u) {
        return false;
    }
    // frame_index is the count before this frame's increment.
    return (frame_index + 1u) % period == 0u;
}

// Compact snapshots copy only the dependency ranges a chunk reads; they apply whenever the renderer
// does not wait on live memory.
bool compact_memory_snapshot_enabled() {
    static const bool enabled = !get_gx_timing_config().render_live_memory_wait;
    return enabled;
}

void record_guest_memory_range(
    std::vector<GuestMemoryRange>& ranges,
    std::uint32_t guest_base,
    std::uint32_t size) {
    if (size == 0u) {
        return;
    }
    const std::uint64_t end =
        static_cast<std::uint64_t>(guest_base) + size;
    if (end > 0x1'0000'0000ull) {
        throw std::runtime_error(
            "[GxBackend] guest-memory snapshot range overflows u32 address space");
    }
    ranges.push_back(GuestMemoryRange{guest_base, size});
}

void normalize_guest_memory_ranges(std::vector<GuestMemoryRange>& ranges) {
    if (ranges.empty()) {
        return;
    }
    std::sort(
        ranges.begin(),
        ranges.end(),
        [](const GuestMemoryRange& a, const GuestMemoryRange& b) {
            return a.guest_base < b.guest_base ||
                (a.guest_base == b.guest_base && a.size < b.size);
        });
    // Merge in place: the write index never passes the read index.
    std::size_t out = 0u;
    for (const GuestMemoryRange& range : ranges) {
        if (range.size == 0u) {
            continue;
        }
        const std::uint64_t begin = range.guest_base;
        const std::uint64_t end = begin + range.size;
        if (out == 0u) {
            ranges[out++] = range;
            continue;
        }
        GuestMemoryRange& tail = ranges[out - 1u];
        const std::uint64_t tail_begin = tail.guest_base;
        const std::uint64_t tail_end = tail_begin + tail.size;
        if (begin <= tail_end) {
            const std::uint64_t merged_end = std::max(tail_end, end);
            tail.size = static_cast<std::uint32_t>(merged_end - tail_begin);
        } else {
            ranges[out++] = range;
        }
    }
    ranges.resize(out);
}

std::uint64_t guest_memory_range_bytes(
    const std::vector<GuestMemoryRange>& ranges) noexcept {
    std::uint64_t bytes = 0;
    for (const GuestMemoryRange& range : ranges) {
        bytes += range.size;
    }
    return bytes;
}

std::uint64_t dependency_shape_state_hash(const GxState& state) noexcept {
    return state.dependency_shape_hash();
}

std::uint32_t normalize_dependency_guest_addr(
    std::uint32_t address) noexcept {
    if (address >= 0x80000000u && address < 0x81800000u) {
        return address - 0x80000000u;
    }
    if (address >= 0xC0000000u && address < 0xC1800000u) {
        return address - 0xC0000000u;
    }
    if (address >= 0x90000000u && address < 0x94000000u) {
        return 0x10000000u + (address - 0x90000000u);
    }
    if (address >= 0xD0000000u && address < 0xD4000000u) {
        return 0x10000000u + (address - 0xD0000000u);
    }
    return address;
}

bool dependency_ranges_overlap(
    std::uint32_t lhs_addr,
    std::uint32_t lhs_size,
    std::uint32_t rhs_addr,
    std::uint32_t rhs_size) noexcept {
    if (lhs_size == 0u || rhs_size == 0u) {
        return false;
    }
    const std::uint64_t lhs_begin = normalize_dependency_guest_addr(lhs_addr);
    const std::uint64_t rhs_begin = normalize_dependency_guest_addr(rhs_addr);
    const std::uint64_t lhs_end = lhs_begin + lhs_size;
    const std::uint64_t rhs_end = rhs_begin + rhs_size;
    return lhs_begin < rhs_end && rhs_begin < lhs_end;
}

[[nodiscard]] std::uint16_t snapshot_read_be_u16(const std::byte* p) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(p[0])) << 8) |
        static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(p[1])));
}

[[nodiscard]] std::size_t snapshot_component_byte_size(std::uint8_t fmt) {
    switch (static_cast<ComponentFormat>(fmt)) {
    case ComponentFormat::U8:
    case ComponentFormat::S8:
        return 1u;
    case ComponentFormat::U16:
    case ComponentFormat::S16:
        return 2u;
    case ComponentFormat::F32:
        return 4u;
    default:
        throw std::runtime_error(
            "[GxBackend] dependency scan: unknown component format");
    }
}

[[nodiscard]] std::size_t snapshot_color_byte_size(std::uint8_t fmt) {
    switch (static_cast<ColorComponentFormat>(fmt)) {
    case ColorComponentFormat::RGB565:
    case ColorComponentFormat::RGBA4444:
        return 2u;
    case ColorComponentFormat::RGB888:
    case ColorComponentFormat::RGBA6666:
        return 3u;
    case ColorComponentFormat::RGBX8888:
    case ColorComponentFormat::RGBA8888:
        return 4u;
    default:
        throw std::runtime_error(
            "[GxBackend] dependency scan: unknown color format");
    }
}

[[nodiscard]] std::size_t snapshot_pos_component_count(std::uint8_t count) {
    return count == 0u ? 2u : 3u;
}

[[nodiscard]] std::size_t snapshot_tex_component_count(std::uint8_t count) {
    return count == 0u ? 1u : 2u;
}

[[nodiscard]] std::size_t snapshot_attr_direct_size_pos(
    const VertexAttribute& attr) {
    return snapshot_component_byte_size(attr.format) *
        snapshot_pos_component_count(attr.count);
}

[[nodiscard]] std::size_t snapshot_normal_vector_size(
    const VertexAttribute& attr) {
    const std::size_t component_size =
        snapshot_component_byte_size(attr.format);
    if (component_size >
        std::numeric_limits<std::size_t>::max() / 3u) {
        throw std::runtime_error(
            "[GxBackend] dependency scan: normal vector size overflows");
    }
    return component_size * 3u;
}

[[nodiscard]] std::size_t snapshot_attr_direct_size_nrm(
    const VertexAttribute& attr) {
    const std::size_t normal_size = snapshot_normal_vector_size(attr);
    if (attr.count != 0u &&
        normal_size > std::numeric_limits<std::size_t>::max() / 3u) {
        throw std::runtime_error(
            "[GxBackend] dependency scan: NBT element size overflows");
    }
    return attr.count != 0u ? normal_size * 3u : normal_size;
}

[[nodiscard]] bool snapshot_normal_has_nbt(const VertexDescriptor& desc) {
    return desc.normal.count != 0u;
}

[[nodiscard]] bool snapshot_normal_uses_three_indices(
    const VertexDescriptor& desc) {
    return snapshot_normal_has_nbt(desc) && desc.normal_index_3;
}

[[nodiscard]] std::size_t snapshot_attr_direct_size_tex(
    const VertexAttribute& attr) {
    return snapshot_component_byte_size(attr.format) *
        snapshot_tex_component_count(attr.count);
}

[[nodiscard]] std::uint32_t snapshot_tex_data_size(
    TexFormat fmt,
    std::uint32_t width,
    std::uint32_t height) {
    auto blocks = [](std::uint32_t dim, std::uint32_t block) {
        return (dim + block - 1u) / block;
    };
    switch (fmt) {
    case TexFormat::I4:
        return blocks(width, 8u) * blocks(height, 8u) * 32u;
    case TexFormat::I8:
    case TexFormat::IA4:
        return blocks(width, 8u) * blocks(height, 4u) * 32u;
    case TexFormat::IA8:
    case TexFormat::RGB565:
    case TexFormat::RGB5A3:
        return blocks(width, 4u) * blocks(height, 4u) * 32u;
    case TexFormat::RGBA8:
        return blocks(width, 4u) * blocks(height, 4u) * 64u;
    case TexFormat::C4:
        return blocks(width, 8u) * blocks(height, 8u) * 32u;
    case TexFormat::C8:
        return blocks(width, 8u) * blocks(height, 4u) * 32u;
    case TexFormat::C14X2:
        return blocks(width, 4u) * blocks(height, 4u) * 32u;
    case TexFormat::CMPR:
        return blocks(width, 8u) * blocks(height, 8u) * 32u;
    default:
        throw std::runtime_error(
            "[GxBackend] dependency scan: unknown texture format");
    }
}

[[nodiscard]] unsigned snapshot_full_mip_chain_levels(
    std::uint32_t width,
    std::uint32_t height) {
    unsigned levels = 1u;
    while (((width >> levels) | (height >> levels)) != 0u) {
        ++levels;
    }
    return levels;
}

[[nodiscard]] bool snapshot_generated_mips_color_format(TexFormat format) {
    switch (format) {
    case TexFormat::RGB565:
    case TexFormat::RGB5A3:
    case TexFormat::RGBA8:
    case TexFormat::C4:
    case TexFormat::C8:
    case TexFormat::C14X2:
    case TexFormat::CMPR:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool snapshot_should_generate_native_mips(
    const TexImage& image,
    const TexMode& mode) {
    if ((static_cast<unsigned>(mode.min_filter) & 0x3u) != 0u ||
        mode.min_filter != TexMinFilter::Linear ||
        mode.mag_filter != TexMagFilter::Linear ||
        (image.width < 64u && image.height < 64u) ||
        !snapshot_generated_mips_color_format(image.format)) {
        return false;
    }
    return get_render_config().enhanced_mipmaps;
}

[[nodiscard]] std::uint32_t snapshot_texture_guest_size(
    const TexImage& image,
    const TexMode& mode) {
    unsigned guest_levels = 1u;
    const unsigned mip_mode = static_cast<unsigned>(mode.min_filter) & 0x3u;
    const unsigned full_chain =
        snapshot_full_mip_chain_levels(image.width, image.height);
    if (mip_mode != 0u) {
        const unsigned wanted =
            1u + (static_cast<unsigned>(mode.max_lod_x16) + 15u) / 16u;
        guest_levels = std::min(wanted, full_chain);
    }
    // Host-generated mips read only guest level 0, so no setting changes the footprint.

    std::uint64_t total = 0;
    for (unsigned level = 0; level < guest_levels; ++level) {
        total += snapshot_tex_data_size(
            image.format,
            std::max<std::uint32_t>(1u, image.width >> level),
            std::max<std::uint32_t>(1u, image.height >> level));
    }
    if (total > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(
            "[GxBackend] dependency scan: texture source is too large");
    }
    return static_cast<std::uint32_t>(total);
}

struct SnapshotArrayRange {
    bool used = false;
    std::uint32_t begin = 0;
    std::uint64_t end = 0;
};

void record_indexed_array_access(
    std::array<SnapshotArrayRange, 12>& array_ranges,
    unsigned attr_index,
    std::uint32_t address,
    std::uint32_t size) {
    if (attr_index >= array_ranges.size() || size == 0u) {
        return;
    }
    SnapshotArrayRange& range = array_ranges[attr_index];
    const std::uint64_t end =
        static_cast<std::uint64_t>(address) + size;
    if (end > 0x1'0000'0000ull) {
        throw std::runtime_error(
            "[GxBackend] dependency scan: indexed array range overflows");
    }
    if (!range.used) {
        range.used = true;
        range.begin = address;
        range.end = end;
        return;
    }
    range.begin = std::min(range.begin, address);
    range.end = std::max(range.end, end);
}

class DependencyRangeSink final : public FifoSink {
public:
    DependencyRangeSink(
        GxState& state,
        GuestMemory* memory,
        std::vector<GuestMemoryRange>& ranges)
        : state_(state),
          memory_(memory),
          ranges_(ranges) {}

    [[nodiscard]] std::size_t draw_payload_size(
        std::uint8_t vtxfmt,
        std::uint16_t vertex_count) const override {
        return VertexLoader::source_vertex_size(state_.vertex_desc(vtxfmt)) *
            static_cast<std::size_t>(vertex_count);
    }

    void on_draw(
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        FifoCursor& cursor) override {
        (void)primitive;
        const VertexDescriptor desc = state_.vertex_desc(vtxfmt);
        const std::uint16_t vertex_count = cursor.read_u16();
        const std::size_t stride = VertexLoader::source_vertex_size(desc);
        const std::span<const std::byte> raw =
            cursor.take(stride * vertex_count);
        if (record_broad_draw_dependencies_for_vtxfmt(vtxfmt)) {
            return;
        }
        std::array<SnapshotArrayRange, 12> array_ranges{};
        scan_draw_payload_dependencies(desc, raw, vertex_count, array_ranges);
        flush_array_ranges(array_ranges);
        record_texture_dependency_ranges_once();
    }

    void on_efb_copy(std::uint32_t exec_command) override {
        (void)exec_command;
    }

    void on_pe_finish() override {}
    void on_pe_token(std::uint16_t token, bool interrupt) override {
        (void)token;
        (void)interrupt;
    }
    void on_invalidate_textures() override {}
    void on_invalidate_vertex_cache() override {}

    void on_tlut_load() override {
        const TlutTransfer transfer = decode_wii_tlut_transfer(
            state_.bp(bp::kTlutSrcAddr), state_.bp(bp::kTlutDest));
        if (transfer.byte_count != 0u) {
            record_dependency_range(transfer.source_address, transfer.byte_count);
        }
    }

private:
    void record_dependency_range(
        std::uint32_t guest_base,
        std::uint32_t size) {
        if (size == 0u) {
            return;
        }
        const std::uint64_t key =
            (static_cast<std::uint64_t>(guest_base) << 32) | size;
        if (!emitted_range_keys_.insert(key)) {
            return;
        }
        record_guest_memory_range(ranges_, guest_base, size);
    }

    void record_texture_dependency_ranges_to(
        std::vector<GuestMemoryRange>& target) const {
        const std::uint8_t texture_map_mask =
            sampled_texture_map_mask(state_);
        for (unsigned map = 0; map < kMaxTextureMaps; ++map) {
            if ((texture_map_mask & (1u << map)) == 0u) {
                continue;
            }
            const TexImage image = state_.tex_image(map);
            const TexMode mode = state_.tex_mode(map);
            record_guest_memory_range(
                target,
                image.guest_addr,
                snapshot_texture_guest_size(image, mode));
        }
    }

    static void flush_array_ranges_to(
        const std::array<SnapshotArrayRange, 12>& array_ranges,
        std::vector<GuestMemoryRange>& target) {
        for (const SnapshotArrayRange& range : array_ranges) {
            if (range.used) {
                record_guest_memory_range(
                    target,
                    range.begin,
                    static_cast<std::uint32_t>(range.end - range.begin));
            }
        }
    }

    void record_texture_dependency_ranges_once() {
        const std::uint8_t texture_map_mask =
            sampled_texture_map_mask(state_);
        for (unsigned map = 0; map < kMaxTextureMaps; ++map) {
            if ((texture_map_mask & (1u << map)) == 0u) {
                continue;
            }
            const TexImage image = state_.tex_image(map);
            const TexMode mode = state_.tex_mode(map);
            record_dependency_range(
                image.guest_addr,
                snapshot_texture_guest_size(image, mode));
        }
    }

    void flush_array_ranges(
        const std::array<SnapshotArrayRange, 12>& array_ranges) {
        for (const SnapshotArrayRange& range : array_ranges) {
            if (range.used) {
                record_dependency_range(
                    range.begin,
                    static_cast<std::uint32_t>(range.end - range.begin));
            }
        }
    }

    [[nodiscard]] bool record_broad_draw_dependencies_for_vtxfmt(
        std::uint8_t vtxfmt) {
        // The memo is keyed on the state's mutation revision, not a hash: this sink owns the parse
        // of one register instance, so the revision identifies every change exactly (zero, after the
        // counter saturates, forbids reuse).
        if (broad_draw_memo_.contains(state_.dependency_shape_revision(), vtxfmt)) {
            return true;
        }
        const VertexDescriptor desc = state_.vertex_desc(vtxfmt);
        if (!record_broad_draw_dependencies(desc)) {
            return false;
        }
        record_texture_dependency_ranges_once();
        broad_draw_memo_.mark(vtxfmt);
        return true;
    }

    [[nodiscard]] std::uint32_t guest_region_remaining(
        std::uint32_t guest_addr) const {
        if (memory_ == nullptr) {
            return 0u;
        }
        const std::uint32_t normalized =
            normalize_dependency_guest_addr(guest_addr);
        for (std::uint32_t i = 0; i < memory_->region_count; ++i) {
            const GuestMemoryRegion& region = memory_->regions[i];
            if (region.host_base == nullptr || region.size == 0u) {
                continue;
            }
            const std::uint32_t region_base =
                normalize_dependency_guest_addr(region.guest_base);
            const std::uint64_t region_end =
                static_cast<std::uint64_t>(region_base) + region.size;
            if (normalized >= region_base && normalized < region_end) {
                return static_cast<std::uint32_t>(region_end - normalized);
            }
        }
        return 0u;
    }

    [[nodiscard]] bool record_broad_indexed_attribute(
        unsigned attr_index,
        const VertexAttribute& attr,
        std::size_t element_size) {
        if (attr.vcd == VcdType::None || attr.vcd == VcdType::Direct) {
            return true;
        }
        if (attr.vcd != VcdType::Index8 && attr.vcd != VcdType::Index16) {
            return false;
        }
        if (element_size > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error(
                "[GxBackend] dependency scan: indexed array element size "
                "overflows u32");
        }
        const std::uint64_t max_index =
            attr.vcd == VcdType::Index8 ? 255ull : 65535ull;
        const std::uint32_t base = state_.array_base(attr_index);
        const std::uint32_t stride = state_.array_stride(attr_index);
        const std::uint64_t requested =
            max_index * static_cast<std::uint64_t>(stride) + element_size;
        if (requested > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }
        const std::uint32_t remaining = guest_region_remaining(base);
        if (remaining == 0u) {
            return false;
        }
        const std::uint32_t size = std::min<std::uint32_t>(
            remaining,
            static_cast<std::uint32_t>(requested));
        record_dependency_range(base, size);
        return true;
    }

    [[nodiscard]] bool record_broad_draw_dependencies(
        const VertexDescriptor& desc) {
        if (!record_broad_indexed_attribute(
                0u,
                desc.position,
                desc.position.vcd == VcdType::None
                    ? 0u
                    : snapshot_attr_direct_size_pos(desc.position))) {
            return false;
        }
        if (desc.normal.vcd == VcdType::Index8 ||
            desc.normal.vcd == VcdType::Index16) {
            const std::size_t normal_element_size =
                snapshot_normal_uses_three_indices(desc)
                    ? snapshot_normal_vector_size(desc.normal) * 3u
                    : snapshot_attr_direct_size_nrm(desc.normal);
            if (!record_broad_indexed_attribute(
                    1u,
                    desc.normal,
                    normal_element_size)) {
                return false;
            }
        } else if (desc.normal.vcd != VcdType::None &&
                   desc.normal.vcd != VcdType::Direct) {
            return false;
        }
        for (unsigned color = 0; color < 2u; ++color) {
            const VertexAttribute& attr = desc.color[color];
            if (!record_broad_indexed_attribute(
                    2u + color,
                    attr,
                    attr.vcd == VcdType::None
                        ? 0u
                        : snapshot_color_byte_size(attr.format))) {
                return false;
            }
        }
        for (unsigned tex = 0; tex < 8u; ++tex) {
            const VertexAttribute& attr = desc.texcoord[tex];
            if (!record_broad_indexed_attribute(
                    4u + tex,
                    attr,
                    attr.vcd == VcdType::None
                        ? 0u
                        : snapshot_attr_direct_size_tex(attr))) {
                return false;
            }
        }
        return true;
    }

    void scan_attribute(
        std::array<SnapshotArrayRange, 12>& array_ranges,
        unsigned attr_index,
        const VertexAttribute& attr,
        std::size_t element_size,
        const std::byte* source,
        std::size_t& offset) const {
        switch (attr.vcd) {
        case VcdType::None:
            return;
        case VcdType::Direct:
            offset += element_size;
            return;
        case VcdType::Index8: {
            const std::uint32_t index =
                std::to_integer<std::uint8_t>(source[offset]);
            offset += 1u;
            record_indexed_array_access(
                array_ranges,
                attr_index,
                state_.array_base(attr_index) +
                    index * state_.array_stride(attr_index),
                static_cast<std::uint32_t>(element_size));
            return;
        }
        case VcdType::Index16: {
            const std::uint32_t index = snapshot_read_be_u16(source + offset);
            offset += 2u;
            record_indexed_array_access(
                array_ranges,
                attr_index,
                state_.array_base(attr_index) +
                    index * state_.array_stride(attr_index),
                static_cast<std::uint32_t>(element_size));
            return;
        }
        default:
            throw std::runtime_error(
                "[GxBackend] dependency scan: unknown VCD type");
        }
    }

    void scan_draw_payload_dependencies(
        const VertexDescriptor& desc,
        std::span<const std::byte> raw,
        std::uint32_t vertex_count,
        std::array<SnapshotArrayRange, 12>& array_ranges) const {
        const std::size_t stride = VertexLoader::source_vertex_size(desc);
        if (raw.size() != stride * static_cast<std::size_t>(vertex_count)) {
            throw std::runtime_error(
                "[GxBackend] dependency scan: draw payload size mismatch");
        }

        for (std::uint32_t vertex = 0; vertex < vertex_count; ++vertex) {
            const std::byte* source =
                raw.data() + static_cast<std::size_t>(vertex) * stride;
            std::size_t offset = 0;

            if (desc.has_pn_matrix_index) {
                offset += 1u;
            }
            for (unsigned tex = 0; tex < 8u; ++tex) {
                if (desc.has_tex_matrix_index[tex]) {
                    offset += 1u;
                }
            }

            scan_attribute(
                array_ranges,
                0u,
                desc.position,
                desc.position.vcd == VcdType::None
                    ? 0u
                    : snapshot_attr_direct_size_pos(desc.position),
                source,
                offset);

            if (desc.normal.vcd == VcdType::Direct) {
                offset += snapshot_attr_direct_size_nrm(desc.normal);
            } else if (desc.normal.vcd == VcdType::Index8 ||
                       desc.normal.vcd == VcdType::Index16) {
                const std::size_t index_size =
                    desc.normal.vcd == VcdType::Index8 ? 1u : 2u;
                const std::size_t elem_size =
                    snapshot_normal_vector_size(desc.normal);
                if (snapshot_normal_uses_three_indices(desc)) {
                    std::uint32_t indices[3]{};
                    for (unsigned ni = 0; ni < 3u; ++ni) {
                        indices[ni] = desc.normal.vcd == VcdType::Index8
                            ? std::to_integer<std::uint8_t>(
                                  source[offset + ni])
                            : snapshot_read_be_u16(
                                  source + offset + ni * index_size);
                    }
                    offset += index_size * 3u;
                    for (unsigned ni = 0; ni < 3u; ++ni) {
                        record_indexed_array_access(
                            array_ranges,
                            1u,
                            state_.array_base(1u) +
                                indices[ni] * state_.array_stride(1u) +
                                static_cast<std::uint32_t>(ni * elem_size),
                            static_cast<std::uint32_t>(elem_size));
                    }
                } else {
                    const std::uint32_t index =
                        desc.normal.vcd == VcdType::Index8
                        ? std::to_integer<std::uint8_t>(source[offset])
                        : snapshot_read_be_u16(source + offset);
                    offset += index_size;
                    record_indexed_array_access(
                        array_ranges,
                        1u,
                        state_.array_base(1u) +
                            index * state_.array_stride(1u),
                        static_cast<std::uint32_t>(
                            snapshot_normal_has_nbt(desc)
                                ? elem_size * 3u
                                : elem_size));
                }
            } else if (desc.normal.vcd != VcdType::None) {
                throw std::runtime_error(
                    "[GxBackend] dependency scan: unknown normal VCD type");
            }

            for (unsigned color = 0; color < 2u; ++color) {
                const VertexAttribute& attr = desc.color[color];
                scan_attribute(
                    array_ranges,
                    2u + color,
                    attr,
                    attr.vcd == VcdType::None
                        ? 0u
                        : snapshot_color_byte_size(attr.format),
                    source,
                    offset);
            }

            for (unsigned tex = 0; tex < 8u; ++tex) {
                const VertexAttribute& attr = desc.texcoord[tex];
                scan_attribute(
                    array_ranges,
                    4u + tex,
                    attr,
                    attr.vcd == VcdType::None
                        ? 0u
                        : snapshot_attr_direct_size_tex(attr),
                    source,
                    offset);
            }

            if (offset != stride) {
                throw std::runtime_error(
                    "[GxBackend] dependency scan: vertex stride mismatch");
            }
        }
    }

    GxState& state_;
    GuestMemory* memory_ = nullptr;
    std::vector<GuestMemoryRange>& ranges_;
    detail::DependencyRangeMemo emitted_range_keys_;
    detail::BroadDrawDependencyMemo broad_draw_memo_;
};

class DependencyParserReadRecorder final
    : public FifoGuestMemoryReadRecorder {
public:
    explicit DependencyParserReadRecorder(
        std::vector<GuestMemoryRange>& ranges,
        std::vector<GuestMemoryRange>& shape_ranges)
        : ranges_(ranges), shape_ranges_(shape_ranges) {}

    void record_guest_memory_read(
        std::uint32_t guest_addr,
        std::uint32_t size) override {
        record_guest_memory_range(ranges_, guest_addr, size);
    }

    void record_guest_dependency_shape_read(
        std::uint32_t guest_addr,
        std::uint32_t size) override {
        record_guest_memory_range(shape_ranges_, guest_addr, size);
    }

private:
    std::vector<GuestMemoryRange>& ranges_;
    std::vector<GuestMemoryRange>& shape_ranges_;
};

// The vblanks per present slot for a rate on a display of `refresh` hertz: a whole number, the rate kept above 60.
std::int64_t vblanks_per_slot(double refresh, double rate) {
    std::int64_t slot = std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil(refresh / rate - 0.02)));
    if (refresh / static_cast<double>(slot) < 61.0) {
        slot = std::max<std::int64_t>(1, static_cast<std::int64_t>(refresh / 61.0));
    }
    return slot;
}

// Adds its lifetime to the game thread's time blocked on the render thread (GxBackend::game_wait_qpc_).
struct GameWaitTimer {
    std::atomic<std::uint64_t>& total;
    const std::int64_t start = galaxy::qpc_now();
    ~GameWaitTimer() {
        total.fetch_add(static_cast<std::uint64_t>(galaxy::qpc_now() - start), std::memory_order_relaxed);
    }
};

std::uint64_t trace_gx_stall_threshold_us() {
    static const std::uint64_t threshold =
        read_env_u64("GALAXY_TRACE_GX_STALL_US", 1000u);
    return threshold;
}

std::uint64_t elapsed_us(
    std::chrono::steady_clock::time_point start,
    std::chrono::steady_clock::time_point end) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            end - start).count());
}

// A per-thread counter sample; `available` is false when the query failed (zero is a valid value).
struct ThreadCounterSample {
    std::uint64_t value = 0u;
    bool available = false;
};

ThreadCounterSample thread_counter_interval(
    ThreadCounterSample begin, ThreadCounterSample end) noexcept {
    if (!begin.available || !end.available || end.value < begin.value) return {};
    return {end.value - begin.value, true};
}

ThreadCounterSample thread_cycle_count() noexcept {
#if defined(_WIN32)
    ULONG64 cycles = 0u;
    if (QueryThreadCycleTime(GetCurrentThread(), &cycles) == FALSE) return {};
    // Raw cycles, not convertible to time.
    return {static_cast<std::uint64_t>(cycles), true};
#else
    return {};  // no per-thread cycle counter here; thread_cpu_time_100ns covers it
#endif
}

bool pso_cycles_requested() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(&length, value, sizeof(value),
                        "GALAXY_GX_PSO_CYCLES") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

ThreadCounterSample thread_cpu_time_100ns() noexcept {
#if defined(_WIN32)
    // Kernel plus user CPU time of this thread, in 100 ns units.
    FILETIME creation{};
    FILETIME exit{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetThreadTimes(
            GetCurrentThread(), &creation, &exit, &kernel, &user) == FALSE) {
        return {};
    }
    ULARGE_INTEGER kernel_ticks{};
    kernel_ticks.LowPart = kernel.dwLowDateTime;
    kernel_ticks.HighPart = kernel.dwHighDateTime;
    ULARGE_INTEGER user_ticks{};
    user_ticks.LowPart = user.dwLowDateTime;
    user_ticks.HighPart = user.dwHighDateTime;
    return {kernel_ticks.QuadPart + user_ticks.QuadPart, true};
#else
    timespec now{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) != 0) return {};
    return {(static_cast<std::uint64_t>(now.tv_sec) * 1000000000u + static_cast<std::uint64_t>(now.tv_nsec)) / 100u, true};
#endif
}

// Always-on flush timing: one call in kFlushTimingSampleStride is timed, so the clock reads stay
// off the render thread's per-draw cost. flush-mean-us (total / samples) is the per-call cost;
// flush-total-us sums the sampled calls only and is not scaled up.
constexpr std::uint64_t kFlushTimingSampleStride = 64u;

struct ScopedFlushTiming {
    std::chrono::steady_clock::time_point start{};
    std::uint64_t* sampled_total_us = nullptr;
    std::uint64_t* sampled_count = nullptr;
    bool armed = false;

    ScopedFlushTiming(
        std::uint64_t& call_count,
        std::uint64_t& sampled_total_us,
        std::uint64_t& sampled_count) noexcept {
        // call_count runs across the session, not per frame, so the samples spread over every call
        // position even when a frame makes fewer than 64 flushes.
        if ((call_count % kFlushTimingSampleStride) == 0u) {
            start = std::chrono::steady_clock::now();
            this->sampled_total_us = &sampled_total_us;
            this->sampled_count = &sampled_count;
            armed = true;
        }
        ++call_count;
    }

    ~ScopedFlushTiming() {
        if (armed) {
            *this->sampled_total_us += elapsed_us(
                start, std::chrono::steady_clock::now());
            ++*this->sampled_count;
        }
    }

    ScopedFlushTiming(const ScopedFlushTiming&) = delete;
    ScopedFlushTiming& operator=(const ScopedFlushTiming&) = delete;
};

[[nodiscard]] const std::byte* resolve_guest_const_bytes(
    GuestMemory* memory,
    std::uint32_t guest_addr,
    std::uint32_t size,
    bool regions_disjoint_sorted = false) {
    if (memory == nullptr || size == 0u) {
        return nullptr;
    }
    if (std::byte* fast = resolve_guest_fast(memory, guest_addr, size);
        fast != nullptr) {
        return fast;
    }
    if (memory->regions == nullptr) {
        return nullptr;
    }
    if (regions_disjoint_sorted) {
        return detail::resolve_sorted_snapshot_region(
            {memory->regions, memory->region_count}, guest_addr, size);
    }
    const std::uint64_t end =
        static_cast<std::uint64_t>(guest_addr) + size;
    for (std::uint32_t i = 0; i < memory->region_count; ++i) {
        const GuestMemoryRegion& r = memory->regions[i];
        if (r.host_base == nullptr) {
            continue;
        }
        const std::uint64_t region_end =
            static_cast<std::uint64_t>(r.guest_base) + r.size;
        if (guest_addr >= r.guest_base && end <= region_end) {
            return r.host_base + (guest_addr - r.guest_base);
        }
    }
    return nullptr;
}

void fill_ps_constants(const GxState& state, GxPsConstants& out, bool derive_scales) {
    std::memset(&out, 0, sizeof(out));

    const GenMode gm = state.gen_mode();

    // stage_config[s]: colour env (BP 0xC0 + 2s), alpha env (BP 0xC1 + 2s), TEV order, ksel.
    for (unsigned s = 0; s < gm.num_tev_stages && s < kMaxTevStages; ++s) {
        out.stage_config[s][0] =
            state.bp(static_cast<std::uint8_t>(bp::kTevColorEnvBase + s * 2u));
        out.stage_config[s][1] =
            state.bp(static_cast<std::uint8_t>(bp::kTevAlphaEnvBase + s * 2u));
        const TevOrder ord = state.tev_order(s);
        out.stage_config[s][2] =
              (static_cast<std::uint32_t>(ord.texmap)      )
            | (static_cast<std::uint32_t>(ord.texcoord)  << 3)
            | (ord.tex_enable ? (1u << 6) : 0u)
            | (static_cast<std::uint32_t>(ord.ras_channel) << 7);
        // Ksel: kcsel(4:0) | kasel(9:5).
        const unsigned pair = s / 2;
        const TevKSel ks = state.tev_ksel(pair);
        const std::uint8_t kcsel = (s & 1u) ? ks.kcsel_odd : ks.kcsel_even;
        const std::uint8_t kasel = (s & 1u) ? ks.kasel_odd : ks.kasel_even;
        out.stage_config[s][3] =
              static_cast<std::uint32_t>(kcsel)
            | (static_cast<std::uint32_t>(kasel) << 5);
    }

    // TEV registers PREV, REG0-2 (signed 11-bit per channel); the shader converts them back to
    // integers for the TEV math.
    {
        const auto regs = state.tev_register_values();
        for (unsigned r = 0; r < 4; ++r) {
            out.tev_regs[r][0] = static_cast<float>(regs[r][0]) / 255.0f;
            out.tev_regs[r][1] = static_cast<float>(regs[r][1]) / 255.0f;
            out.tev_regs[r][2] = static_cast<float>(regs[r][2]) / 255.0f;
            out.tev_regs[r][3] = static_cast<float>(regs[r][3]) / 255.0f;
        }
    }

    // Konst colours K0-K3, RGBA8 from tev_konst_colors().
    {
        const auto krgba = state.tev_konst_colors();
        for (unsigned k = 0; k < 4; ++k) {
            const std::uint32_t v = krgba[k];
            out.konst[k][0] = ((v >> 24) & 0xFFu) / 255.0f;
            out.konst[k][1] = ((v >> 16) & 0xFFu) / 255.0f;
            out.konst[k][2] = ((v >>  8) & 0xFFu) / 255.0f;
            out.konst[k][3] = ((v       ) & 0xFFu) / 255.0f;
        }
    }

    {
        const AlphaCompare ac = state.alpha_compare();
        out.alpha_refs[0] = ac.ref0 / 255.0f;
        out.alpha_refs[1] = ac.ref1 / 255.0f;
        // comp0 | comp1 << 3 | logic << 6, passed as the bits of a float.
        const std::uint32_t packed =
              static_cast<std::uint32_t>(ac.comp0)
            | (static_cast<std::uint32_t>(ac.comp1) << 3)
            | (static_cast<std::uint32_t>(ac.logic) << 6);
        out.alpha_refs[2] = std::bit_cast<float>(packed);

        // Constant destination alpha (PE_CMODE1).
        const BlendMode bm = state.blend_mode();
        out.alpha_refs[3] = bm.const_alpha_enable
            ? (bm.const_alpha / 255.0f)
            : 0.0f;
    }

    // Fog A (BP 0xEE) and C (BP 0xF1 bits 0-19) are 20-bit packed floats, unpacked here as
    // Dolphin's PixelShaderManager does. B magnitude (u24) and shift (u5) are exact in float; the
    // shader reads them back with uint().
    {
        const FogParams fp = state.fog();
        out.fog_params[0] =
            std::bit_cast<float>(fog_param_float_bits(fp.a_raw & 0xFFFFFu));
        out.fog_params[1] =
            static_cast<float>(fp.b_magnitude_raw & 0xFFFFFFu);
        out.fog_params[2] = static_cast<float>(fp.b_shift_raw & 0x1Fu);
        out.fog_params[3] =
            std::bit_cast<float>(fog_param_float_bits(fp.c_raw));
        out.fog_params[4] = ((fp.color_rgb >> 16) & 0xFFu) / 255.0f;
        out.fog_params[5] = ((fp.color_rgb >>  8) & 0xFFu) / 255.0f;
        out.fog_params[6] = ((fp.color_rgb      ) & 0xFFu) / 255.0f;
        out.fog_params[7] = fp.projection ? 1.0f : 0.0f;  // 1 = orthographic
    }

    // xy: map m's texture size; zw: texture coordinate m's scale (coordinate_scales), which indirect TEV applies
    // before sampling.
    const auto scales = coordinate_scales(state, derive_scales);
    for (unsigned m = 0; m < kMaxTextureMaps; ++m) {
        const TexImage img = state.tex_image(m);
        out.tex_dims[m][0] = static_cast<float>(img.width  > 0 ? img.width  : 1);
        out.tex_dims[m][1] = static_cast<float>(img.height > 0 ? img.height : 1);
        out.tex_dims[m][2] = static_cast<float>(scales[m][0]);
        out.tex_dims[m][3] = static_cast<float>(scales[m][1]);
    }

    // Z-texture bias (BP 0xF4, 24-bit z24 units), normalized to [0,1).
    out.ztex_params[0] =
        static_cast<float>(state.bp(bp::kTevZEnv0) & 0xFFFFFFu) / 16777216.0f;

    // Indirect TEV commands and matrices; matrix rows are {ma,mc,me,17-scale} and
    // {mb,md,mf,17-scale}, as in Dolphin's pixel constants.
    for (unsigned word = 0; word < 16; ++word) {
        out.ind_cmd[word / 4u][word & 3u] =
            state.bp(static_cast<std::uint8_t>(bp::kIndCmdBase + word));
    }
    out.ind_misc[0] = state.bp(bp::kIndRef);
    out.ind_misc[1] = state.bp(bp::kRas1Ss0);
    out.ind_misc[2] = state.bp(bp::kRas1Ss1);
    out.ind_misc[3] = gm.num_ind_stages;
    const auto sign11 = [](std::uint32_t value) -> std::int32_t {
        value &= 0x7FFu;
        return (value & 0x400u) != 0u
            ? static_cast<std::int32_t>(value | ~0x7FFu)
            : static_cast<std::int32_t>(value);
    };
    for (unsigned m = 0; m < 3; ++m) {
        const std::uint32_t a = state.bp(
            static_cast<std::uint8_t>(bp::kIndMtxBase + m * 3u));
        const std::uint32_t b = state.bp(
            static_cast<std::uint8_t>(bp::kIndMtxBase + m * 3u + 1u));
        const std::uint32_t c = state.bp(
            static_cast<std::uint8_t>(bp::kIndMtxBase + m * 3u + 2u));
        const std::int32_t scale =
            static_cast<std::int32_t>(
                bits(a, 22, 2) | (bits(b, 22, 2) << 2u) |
                 (bits(c, 22, 2) << 4u));
        out.ind_mtx[m * 2u][0] = sign11(a);
        out.ind_mtx[m * 2u][1] = sign11(b);
        out.ind_mtx[m * 2u][2] = sign11(c);
        out.ind_mtx[m * 2u][3] = 17 - scale;
        out.ind_mtx[m * 2u + 1u][0] = sign11(a >> 11u);
        out.ind_mtx[m * 2u + 1u][1] = sign11(b >> 11u);
        out.ind_mtx[m * 2u + 1u][2] = sign11(c >> 11u);
        out.ind_mtx[m * 2u + 1u][3] = 17 - scale;
    }
}

void gx_topology_for_primitive(
    PrimitiveClass primitive,
    gpu::Topology& topology,
    gpu::TopologyType& topology_type) {
    topology = gpu::kTopologyTriangleList;
    topology_type = gpu::kTopologyTypeTriangle;
    switch (primitive) {
    case PrimitiveClass::Lines:
    case PrimitiveClass::LineStrip:
        topology = gpu::kTopologyLineList;
        topology_type = gpu::kTopologyTypeLine;
        break;
    case PrimitiveClass::Points:
        topology = gpu::kTopologyPointList;
        topology_type = gpu::kTopologyTypePoint;
        break;
    case PrimitiveClass::Quads:
    case PrimitiveClass::Quads2:
    case PrimitiveClass::Triangles:
    case PrimitiveClass::TriangleStrip:
    case PrimitiveClass::TriangleFan:
    default:
        break;
    }
}

// XF memory words snapshot per palette upload:
//   0x0000-0x00FF  position/texture matrices (64 rows x 4 words)
//   0x0400-0x045F  normal matrices (32 rows x 3 words)
//   0x0500-0x05FF  post-transform (dual-tex) matrices (64 rows x 4 words)
//   0x0600-0x067F  light objects (8 lights x 16 words; color word 3,
//                  cos-atten 4-6, dist-atten 7-9, pos 10-12, dir 13-15)
// One contiguous range keeps the upload a single memcpy and lets the vertex shader fetch matrices
// and lights from one structured buffer.
inline constexpr std::size_t kMatrixPaletteWords = 0x0680u;  // 0x0000-0x067F
// The upload copies the chunks GxState marks written, so both must cover the same words.
static_assert(
    kMatrixPaletteWords == galaxy::gx::GxState::kMatrixPaletteWords,
    "backend palette bound must match the tracked GxState palette range");

}  // anonymous namespace

void GxBackend::reset_fifo_session_state() {
    // The render parser, event parser and dependency scanner read the same command stream on
    // different schedules, so all of them reset together, while no producer or render thread
    // runs: stale state or a partial command in one of them would shift command boundaries.
    state_ = GxState{};
    state_.mark_all_dirty();
    // A new parser also drops the previous session's display-list cache and its capacity.
    parser_ = FifoParser{};
    parser_.set_dump_dir("generated");
    pending_fifo_.clear();

    event_state_ = GxState{};
    event_parser_ = FifoParser{};
    event_parser_.set_dump_dir("generated");
    event_pending_fifo_.clear();

    dependency_state_ = GxState{};
    dependency_state_.mark_all_dirty();
    dependency_parser_ = FifoParser{};
    dependency_parser_.set_dump_dir("generated");
    dependency_pending_fifo_.clear();
    for (DependencyRangeCacheEntry& entry : dependency_range_cache_) {
        entry = DependencyRangeCacheEntry{};
    }
    dependency_range_cache_tick_ = 0u;

    pending_async_dirty_ranges_.clear();
    // Runs before the renderer initializes, so nothing still reads the buffer.
    release_resident_vertex_buffer();
    vertex_layout_cache_ = {};

    frame_memory_ = nullptr;
    frame_memory_regions_disjoint_sorted_ = false;
    frame_services_ = nullptr;
    frame_pe_events_ = nullptr;
    frame_pe_callbacks_enabled_ = true;
    pending_draw_batch_ = {};
    cached_draw_run_active_ = false;
    cached_draw_run_scissor_reject_ = false;
    cached_draw_run_primitive_indexed_ = false;
    cached_draw_run_no_pso_ = false;
    cached_draw_run_batch_compatible_known_ = false;
    cached_draw_run_primitive_ = PrimitiveClass::Points;
    cached_draw_run_vtxfmt_ = 0u;
    cached_draw_run_state_call_ = {};
    frame_fifo_profile_.reset();

    // Derived state below holds descriptors and resources of the previous renderer session.
    ps_key_ = {};
    vs_key_ = {};
    render_state_key_ = {};
    current_vs_hash_ = 0u;
    current_ps_hash_ = 0u;
    shader_key_hashes_valid_ = false;
    current_pso_key_ = {};
    current_pso_key_valid_ = false;
    current_pipeline_ = nullptr;
    current_pipeline_is_uber_ = false;
    current_vs_constants_ = 0u;
    cpu_vs_constants_ = {};
    current_ps_constants_ = 0u;
    current_texture_table_ = {};
    current_texture_handles_ = {};
    current_sampler_table_ = {};
    current_matrix_palette_ = 0u;
    matrix_palette_stale_ = true;
    current_palette_required_ = true;
    inline_matrices_stale_ = true;
    // The ring segment is reused, so no palette chunk is valid until the next upload.
    xf_palette_valid_ = 0u;
    frame_bindings_dirty_ = true;
    texture_bindings_dirty_ = true;
    current_texture_map_mask_ = 0u;
    current_texture_map_mask_valid_ = false;
    current_texture_binding_key_ = {};
    current_texture_binding_key_mask_ = 0u;
    // Also clears current_texture_binding_key_valid_, so no key outlives its tables.
    clear_texture_binding_table_cache();
    texture_handle_cache_.clear();
    dirty_texture_ranges_scratch_.clear();

    // Frame stamps, seeding and XFB addresses restart with the new EFB/XFB resources.
    frame_index_ = 0u;
    timing_frame_index_.store(0u, std::memory_order_relaxed);
    efb_seeded_ = false;
    present_epoch_ = 0u;
    xfb_serial_accounting_ = {};
    reset_monotonic_cadence(xfb_copy_production_cadence_);
    reset_monotonic_cadence(xfb_first_serial_present_cadence_);
    last_presented_xfb_stamp_.store(
        ~std::uint64_t{0}, std::memory_order_relaxed);
    last_presented_xfb_serial_.store(
        ~std::uint64_t{0}, std::memory_order_relaxed);
    last_presented_xfb_addr_.store(0u, std::memory_order_relaxed);

    for (std::atomic_uint64_t& word : dirty_page_words_) {
        word.store(0u, std::memory_order_relaxed);
    }
    for (std::atomic_uint64_t& summary : dirty_word_summary_) {
        summary.store(0u, std::memory_order_relaxed);
    }
}

bool GxBackend::initialize(int w, int h) {
    if (initialized_) {
        return true;
    }

    cadence_diagnostics_ = cadence::global_session_if_enabled();

    // Before any resource is created: the reset audits the previous session and advances the token
    // epoch, so no token of an earlier session can alias one of this session.
    frame_pe_completions_.reset();
    reset_fifo_session_state();

    const RenderConfig cfg = get_render_config();

    boot_step("begins");
    if (!renderer_.initialize(w, h, cfg.efb_scale)) {
        std::cerr << "[GxBackend] RendererD3D12::initialize failed\n";
        return false;
    }
    boot_step("device rest");

    if (!efb_copies_.initialize(renderer_.device(), cfg.efb_scale)) {
        std::cerr << "[GxBackend] EfbCopyManager::initialize failed\n";
        renderer_.shutdown();
        return false;
    }

    if (!texture_cache_.initialize(renderer_.device())) {
        std::cerr << "[GxBackend] TextureCache::initialize failed\n";
        efb_copies_.shutdown();
        renderer_.shutdown();
        return false;
    }
    if (!texture_cache_.preallocate_upload_arenas(kFramesInFlight)) {
        std::cerr << "[GxBackend] TextureCache upload preallocation failed\n";
        texture_cache_.shutdown();
        efb_copies_.shutdown();
        renderer_.shutdown();
        return false;
    }
    boot_step("efb copies and texture cache");
    // The resident decoded-vertex buffer (a 64 MB upload heap) is created here: creating it at first
    // use stalls gameplay.
    (void)ensure_resident_vertex_buffer();
    renderer_.create_first_use_resources();
    boot_step("first-use resources");

    const std::filesystem::path cache_dir = shader_cache_directory();

    // Few compile workers on small CPUs; more on large ones, to warm cached PSOs early.
    const unsigned logical_cores = std::thread::hardware_concurrency();
    const unsigned worker_count =
        (logical_cores > 8u) ? 4u : (logical_cores > 4u) ? 2u : 1u;

    if (!pipeline_cache_.initialize(
            renderer_.device(),
            renderer_.root_signature(),
            cache_dir,
            worker_count)) {
        std::cerr << "[GxBackend] PipelineCache::initialize failed\n";
        texture_cache_.shutdown();
        efb_copies_.shutdown();
        renderer_.shutdown();
        return false;
    }
    boot_step("pipeline cache");

    initialized_ = true;
    stop_render_thread_ = false;
    render_chunks_in_flight_ = 0;
    present_chunks_in_flight_ = 0;
    utility_chunks_in_flight_ = 0;
    render_cpu_reads_in_flight_ = 0;
    render_fifo_effects_in_flight_ = 0;
    pending_frame_effects_.clear();
    pending_pointer_depth_capture_ = {};
    render_thread_ = std::thread(&GxBackend::render_thread_main, this);
    if (read_env_u64("GALAXY_FRAME_TELEMETRY", 1u) != 0u) {
        try {
            frame_telemetry_ = std::make_unique<galaxy::telemetry::Publisher>([this] {
                const auto stats = xfb_present_stats();
                return galaxy::telemetry::Counts{
                    xfb_copy_count(), stats.first_serial_presentations, stats.presented,
                    stats.repeated_or_stale_serial_requests, stats.last_presented_copy_serial,
                    stats.last_presented_frame_stamp, stats.last_presented_xfb_addr,
                    xfb_copy_production_cadence_.published_last_sample_ns.load(std::memory_order_relaxed),
                    xfb_first_serial_present_cadence_.published_last_sample_ns.load(std::memory_order_relaxed)};
            }, static_cast<std::uint64_t>(w), static_cast<std::uint64_t>(h),
               get_render_config().efb_scale);
        } catch (...) { /* Optional startup allocation cannot fail gameplay. */ }
    }
    return true;
}

GxBackend::MemorySnapshotStorage* GxBackend::snapshot_storage_for(
    MemorySnapshot& snapshot,
    std::byte* source,
    std::uint32_t size) {
    if (source == nullptr || size == 0u) {
        return nullptr;
    }

    constexpr std::size_t small_capture_limit = 16u;
    constexpr std::size_t empty_slot = std::numeric_limits<std::size_t>::max();
    const auto hash_key = [](std::byte* address, std::uint32_t bytes) {
        auto key = static_cast<std::uint64_t>(
            reinterpret_cast<std::uintptr_t>(address));
        key ^= static_cast<std::uint64_t>(bytes) * 0x9e3779b97f4a7c15ull;
        key ^= key >> 30u;
        key *= 0xbf58476d1ce4e5b9ull;
        key ^= key >> 27u;
        return static_cast<std::size_t>(key ^ (key >> 31u));
    };
    const auto insert_slot = [&](std::size_t index) {
        const auto& entry = snapshot.storage[index];
        const std::size_t mask = snapshot.storage_lookup.size() - 1u;
        std::size_t bucket = hash_key(entry.source, entry.size) & mask;
        while (snapshot.storage_lookup[bucket] != empty_slot) {
            bucket = (bucket + 1u) & mask;
        }
        snapshot.storage_lookup[bucket] = index;
    };

    MemorySnapshotStorage* target = nullptr;
    if (snapshot.storage_used_count < small_capture_limit) {
        // Tiny captures avoid table setup; unused slots are never scanned.
        for (std::size_t index = 0u; index < snapshot.storage_used_count; ++index) {
            auto& storage = snapshot.storage[index];
            if (storage.source == source && storage.size == size) {
                target = &storage;
                break;
            }
        }
    } else {
        if (!snapshot.storage_lookup_active ||
            snapshot.storage_used_count >= snapshot.storage_lookup.size() / 2u) {
            const std::size_t required = snapshot.storage_used_count * 4u;
            std::size_t capacity = 64u;
            while (capacity < required) capacity *= 2u;
            // Keep allocated capacity across captures, but never reuse keys.
            capacity = std::max(capacity, snapshot.storage_lookup.size());
            snapshot.storage_lookup.assign(capacity, empty_slot);
            for (std::size_t index = 0u; index < snapshot.storage_used_count; ++index) {
                insert_slot(index);
            }
            snapshot.storage_lookup_active = true;
        }
        const std::size_t mask = snapshot.storage_lookup.size() - 1u;
        std::size_t bucket = hash_key(source, size) & mask;
        while (snapshot.storage_lookup[bucket] != empty_slot) {
            auto& storage = snapshot.storage[snapshot.storage_lookup[bucket]];
            if (storage.source == source && storage.size == size) {
                target = &storage;
                break;
            }
            bucket = (bucket + 1u) & mask;
        }
    }
    if (target == nullptr) {
        const std::size_t index = snapshot.storage_used_count;
        if (index == snapshot.storage.size()) snapshot.storage.emplace_back();
        target = &snapshot.storage[index];
        target->source = source;
        target->size = size;
        if (snapshot.storage_lookup_active) insert_slot(index);
        ++snapshot.storage_used_count;
    }

    target->used = true;
    if (target->bytes.size() != size) {
        target->bytes.resize(size);
    }
    return target;
}

void GxBackend::copy_memory_snapshot_storage(MemorySnapshot& snapshot) {
    for (MemorySnapshotStorage& storage : snapshot.storage) {
        if (storage.used && !storage.copied &&
            storage.source != nullptr && storage.size != 0u) {
            std::memcpy(storage.bytes.data(), storage.source, storage.size);
            storage.copied = true;
            // The snapshot owns the copy; drop the pointer into live memory.
            storage.source = nullptr;
        }
    }
}

std::uint64_t GxBackend::memory_snapshot_used_bytes(
    const MemorySnapshot& snapshot) noexcept {
    std::uint64_t bytes = 0;
    for (const MemorySnapshotStorage& storage : snapshot.storage) {
        if (storage.used && storage.copied) {
            bytes += storage.size;
        }
    }
    return bytes;
}

void GxBackend::capture_memory_snapshot(
    GuestMemory* source,
    MemorySnapshot& snapshot,
    const std::vector<GuestMemoryRange>& dependency_ranges,
    bool strict_ranges) {
    snapshot.storage_used_count = 0u;
    snapshot.storage_lookup_active = false;
    snapshot.regions_disjoint_sorted = false;
    if (source == nullptr) {
        snapshot.memory = {};
        snapshot.regions.clear();
        snapshot.sealed = false;
        for (MemorySnapshotStorage& storage : snapshot.storage) {
            storage.source = nullptr;
            storage.used = false;
            storage.copied = false;
        }
        return;
    }

    snapshot.sealed = false;
    for (MemorySnapshotStorage& storage : snapshot.storage) {
        storage.source = nullptr;
        storage.used = false;
        storage.copied = false;
    }

    std::vector<GuestMemoryRange> ranges = dependency_ranges;
    normalize_guest_memory_ranges(ranges);

    snapshot.memory = *source;
    // The render-side copy belongs to the snapshot: nothing in it reaches back to the live game.
    snapshot.memory.user = nullptr;
    snapshot.regions.clear();
    snapshot.regions.reserve(
        source->region_count +
        static_cast<std::uint32_t>(ranges.size()));

    if (strict_ranges) {
        const auto append_snapshot_copy =
            [&](const GuestMemoryRange& range) {
                std::uint64_t cursor = range.guest_base;
                const std::uint64_t range_end = cursor + range.size;
                while (cursor < range_end) {
                    const GuestMemoryRegion* source_region = nullptr;
                    std::uint64_t source_region_end = 0;
                    for (std::uint32_t i = 0; i < source->region_count; ++i) {
                        const GuestMemoryRegion& region = source->regions[i];
                        if (region.host_base == nullptr || region.size == 0u) {
                            continue;
                        }
                        const std::uint64_t region_end =
                            static_cast<std::uint64_t>(region.guest_base) +
                            region.size;
                        if (cursor >= region.guest_base &&
                            cursor < region_end) {
                            source_region = &region;
                            source_region_end = region_end;
                            break;
                        }
                    }
                    if (source_region == nullptr) {
                        throw std::runtime_error(
                            "[GxBackend] compact memory snapshot range is unmapped");
                    }

                    const std::uint64_t copy_end =
                        std::min(range_end, source_region_end);
                    const std::uint32_t copy_size =
                        static_cast<std::uint32_t>(copy_end - cursor);
                    const std::uint32_t source_offset =
                        static_cast<std::uint32_t>(
                            cursor - source_region->guest_base);
                    GuestMemoryRegion copy{};
                    copy.guest_base = static_cast<std::uint32_t>(cursor);
                    copy.size = copy_size;
                    std::byte* source_bytes =
                        source_region->host_base + source_offset;
                    if (MemorySnapshotStorage* storage =
                                   snapshot_storage_for(
                                       snapshot,
                                       source_bytes,
                                       copy_size);
                               storage != nullptr) {
                        copy.host_base = storage->bytes.data();
                        snapshot.regions.push_back(copy);
                    }
                    cursor = copy_end;
                }
            };

        for (const GuestMemoryRange& range : ranges) {
            append_snapshot_copy(range);
        }

        for (GuestMemoryFastRegion& fast : snapshot.memory.fast_regions) {
            fast = {};
        }
        snapshot.memory.region_count =
            static_cast<std::uint32_t>(snapshot.regions.size());
        snapshot.memory.regions = snapshot.regions.data();
        copy_memory_snapshot_storage(snapshot);
        // Small views keep the linear lookup; larger ones use binary search only when the regions
        // are verified sorted and disjoint.
        snapshot.regions_disjoint_sorted = snapshot.regions.size() >= 32u &&
            detail::snapshot_regions_are_disjoint_sorted(snapshot.regions);
        snapshot.sealed = true;
        return;
    }

    for (std::uint32_t i = 0; i < source->region_count; ++i) {
        const GuestMemoryRegion& region = source->regions[i];
        GuestMemoryRegion copy = region;
        if (MemorySnapshotStorage* storage =
                snapshot_storage_for(snapshot, region.host_base, region.size);
            storage != nullptr) {
            copy.host_base = storage->bytes.data();
        }
        snapshot.regions.push_back(copy);
    }

    for (GuestMemoryFastRegion& fast : snapshot.memory.fast_regions) {
        fast = {};
    }
    for (std::size_t i = 0; i < 16u; ++i) {
        const GuestMemoryFastRegion& fast = source->fast_regions[i];
        if (MemorySnapshotStorage* storage =
                snapshot_storage_for(snapshot, fast.host_base, fast.size);
            storage != nullptr) {
            snapshot.memory.fast_regions[i] = {
                fast.size,
                storage->bytes.data(),
            };
        }
    }

    snapshot.memory.region_count =
        static_cast<std::uint32_t>(snapshot.regions.size());
    snapshot.memory.regions = snapshot.regions.data();
    copy_memory_snapshot_storage(snapshot);
    snapshot.sealed = true;
}

void GxBackend::collect_memory_dependency_ranges(
    const std::byte* fifo_data,
    std::size_t fifo_size,
    GuestMemory* memory,
    std::vector<GuestMemoryRange>& out,
    bool& cache_hit) {
    out.clear();
    cache_hit = false;
    if (fifo_data == nullptr || fifo_size == 0u) {
        return;
    }

    const std::span<const std::byte> fifo(fifo_data, fifo_size);
    const bool cacheable = dependency_pending_fifo_.empty();
    // The fingerprint only filters; a hit still compares the whole FIFO and the initial state.
    const std::uint64_t fifo_hash =
        cacheable ? dependency_fifo_fingerprint(fifo) : 0u;
    const std::uint64_t state_hash =
        cacheable ? dependency_shape_state_hash(dependency_state_) : 0u;
    const auto shape_words = cacheable ? dependency_state_.dependency_shape_words()
                                      : GxState::DependencyShapeWords{};
    // A hit restores the final parser state, and the pending one-shot BP write mask changes how this
    // FIFO and later ones parse, so it is part of the key.
    const std::uint32_t pending_bp_write_mask =
        dependency_state_.pending_bp_write_mask();
    if (cacheable) {
        for (DependencyRangeCacheEntry& entry : dependency_range_cache_) {
            if (!entry.valid ||
                entry.fifo_hash != fifo_hash ||
                entry.state_hash != state_hash ||
                entry.shape_words != shape_words ||
                entry.pending_bp_write_mask != pending_bp_write_mask ||
                entry.fifo.size() != fifo_size) {
                continue;
            }
            if (std::memcmp(fifo_data, entry.fifo.data(), fifo_size) != 0) {
                continue;
            }
            out = entry.ranges;
            dependency_state_ = entry.final_state;
            entry.last_used = ++dependency_range_cache_tick_;
            cache_hit = true;
            return;
        }
    }

    std::vector<GuestMemoryRange> shape_ranges;
    DependencyRangeSink sink(
        dependency_state_,
        memory,
        out);
    DependencyParserReadRecorder recorder(out, shape_ranges);
    dependency_parser_.set_memory_read_recorder(&recorder);
    try {
        if (dependency_pending_fifo_.empty()) {
            const std::size_t consumed =
                dependency_parser_.run_available(
                    fifo,
                    memory,
                    sink,
                    dependency_state_);
            if (consumed < fifo.size()) {
                dependency_pending_fifo_.assign(
                    fifo.begin() + static_cast<std::ptrdiff_t>(consumed),
                    fifo.end());
            }
        } else {
            dependency_pending_fifo_.insert(
                dependency_pending_fifo_.end(),
                fifo_data,
                fifo_data + fifo_size);
            const std::size_t consumed =
                dependency_parser_.run_available(
                    dependency_pending_fifo_,
                    memory,
                    sink,
                    dependency_state_);
            if (consumed == dependency_pending_fifo_.size()) {
                dependency_pending_fifo_.clear();
            } else if (consumed > 0u) {
                dependency_pending_fifo_.erase(
                    dependency_pending_fifo_.begin(),
                    dependency_pending_fifo_.begin() +
                        static_cast<std::ptrdiff_t>(consumed));
            }
        }
    } catch (const GxFatalError& e) {
        dependency_parser_.set_memory_read_recorder(nullptr);
        std::cerr << "[GxBackend] fatal dependency scan error: "
                  << e.what()
                  << " at offset 0x" << std::hex << e.fifo_offset()
                  << " opcode=0x" << static_cast<unsigned>(e.opcode())
                  << std::dec << '\n';
        throw;
    } catch (...) {
        dependency_parser_.set_memory_read_recorder(nullptr);
        throw;
    }
    dependency_parser_.set_memory_read_recorder(nullptr);

    normalize_guest_memory_ranges(shape_ranges);
    // Display lists reached by CALL_DL are render input too, so the snapshot owns their bytes and
    // does not depend on the parser's display-list cache.
    out.insert(out.end(), shape_ranges.begin(), shape_ranges.end());
    normalize_guest_memory_ranges(out);

    DependencyRangeCacheEntry* target = nullptr;
    for (DependencyRangeCacheEntry& entry : dependency_range_cache_) {
        if (!entry.valid) {
            target = &entry;
            break;
        }
        if (target == nullptr || entry.last_used < target->last_used) {
            target = &entry;
        }
    }
    if (cacheable && dependency_pending_fifo_.empty() && target != nullptr) {
        constexpr std::size_t kMaxOwnedCacheBytes = 16u * 1024u * 1024u;
        std::size_t needed = fifo_size;
        if (needed > kMaxOwnedCacheBytes) return;
        for (const auto count : {out.size(), shape_ranges.size()}) {
            if (count > (kMaxOwnedCacheBytes - needed) / sizeof(GuestMemoryRange))
                return;
            needed += count * sizeof(GuestMemoryRange);
        }
        // Prepare every allocating field before publishing a valid replacement.
        DependencyRangeCacheEntry next;
        next.fifo.assign(fifo.begin(), fifo.end());
        next.ranges = out;
        next.shape_ranges = std::move(shape_ranges);
        next.fifo_hash = fifo_hash;
        next.state_hash = state_hash;
        next.shape_words = shape_words;
        next.pending_bp_write_mask = pending_bp_write_mask;
        next.last_used = ++dependency_range_cache_tick_;
        next.final_state = dependency_state_;
        const auto owned_bytes = [](const DependencyRangeCacheEntry& entry) {
            return entry.fifo.capacity() +
                (entry.ranges.capacity() + entry.shape_ranges.capacity()) *
                    sizeof(GuestMemoryRange);
        };
        if (owned_bytes(next) > kMaxOwnedCacheBytes) return;
        next.valid = true;
        *target = std::move(next);
        // Bound retained vector storage, including invalidated entries. The
        // fixed 64 parser-state slots are separate, already bounded storage.
        std::size_t retained = 0;
        for (const auto& entry : dependency_range_cache_) retained += owned_bytes(entry);
        while (retained > kMaxOwnedCacheBytes) {
            DependencyRangeCacheEntry* victim = nullptr;
            for (auto& entry : dependency_range_cache_) {
                if (&entry == target || owned_bytes(entry) == 0u) continue;
                if (victim == nullptr || (!entry.valid && victim->valid) ||
                    (entry.valid == victim->valid && entry.last_used < victim->last_used))
                    victim = &entry;
            }
            if (victim == nullptr) break;
            retained -= owned_bytes(*victim);
            *victim = DependencyRangeCacheEntry{};
        }
    }

}
void GxBackend::pump_messages() {
    if (initialized_) {
        renderer_.pump_messages();
    }
}

bool GxBackend::quit_requested() const noexcept {
    return initialized_ && renderer_.quit_requested();
}

std::uint64_t GxBackend::xfb_copy_count() const noexcept {
    return stat_xfb_copies_.load(std::memory_order_relaxed);
}

XfbPresentStats GxBackend::xfb_present_stats() const noexcept {
    const std::uint64_t last_stamp =
        last_presented_xfb_stamp_.load(std::memory_order_relaxed);
    const std::uint64_t last_serial =
        last_presented_xfb_serial_.load(std::memory_order_relaxed);
    return XfbPresentStats{
        stat_xfb_present_requests_.load(std::memory_order_relaxed),
        stat_xfb_present_presented_.load(std::memory_order_relaxed),
        stat_xfb_first_serial_presentations_.load(std::memory_order_relaxed),
        stat_xfb_present_misses_.load(std::memory_order_relaxed),
        stat_xfb_present_stale_fallbacks_.load(std::memory_order_relaxed),
        stat_xfb_present_missing_fallbacks_.load(std::memory_order_relaxed),
        stat_xfb_present_stale_preserved_.load(std::memory_order_relaxed),
        stat_xfb_present_missing_preserved_.load(std::memory_order_relaxed),
        stat_xfb_repeated_or_stale_serial_requests_.load(
            std::memory_order_relaxed),
        stat_xfb_present_skipped_duplicate_presents_.load(
            std::memory_order_relaxed),
        stat_xfb_present_duplicate_run_max_.load(std::memory_order_relaxed),
        stat_xfb_out_of_order_serial_requests_.load(
            std::memory_order_relaxed),
        stat_xfb_present_serial_delta_zero_.load(std::memory_order_relaxed),
        stat_xfb_present_serial_delta_one_.load(std::memory_order_relaxed),
        stat_xfb_present_serial_delta_multi_.load(std::memory_order_relaxed),
        stat_xfb_highest_first_presented_copy_serial_.load(
            std::memory_order_relaxed),
        last_stamp == ~std::uint64_t{0} ? 0u : last_stamp,
        last_serial == ~std::uint64_t{0} ? 0u : last_serial,
        last_presented_xfb_addr_.load(std::memory_order_relaxed),
    };
}

void GxBackend::record_monotonic_cadence_sample(
    MonotonicCadenceAccumulator& accumulator) noexcept {
    const auto signed_now_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
    const std::uint64_t now_ns =
        signed_now_ns > 0 ? static_cast<std::uint64_t>(signed_now_ns) : 0u;
    const std::uint64_t prior_samples =
        accumulator.samples.fetch_add(1u, std::memory_order_relaxed);
    accumulator.published_last_sample_ns.store(
        now_ns, std::memory_order_relaxed);
    if (!accumulator.has_last_sample) {
        accumulator.has_last_sample = true;
        accumulator.last_sample_ns = now_ns;
        if (prior_samples == 0u) {
            accumulator.published_first_sample_ns.store(
                now_ns, std::memory_order_relaxed);
        }
        return;
    }

    const std::uint64_t interval_ns =
        now_ns >= accumulator.last_sample_ns
            ? now_ns - accumulator.last_sample_ns
            : 0u;
    accumulator.last_sample_ns = now_ns;
    static const bool monitor=read_env_u64("GALAXY_MONITOR_FRAME_TAILS",0u)!=0u;
    if(monitor) {
        ++accumulator.monitor_intervals; accumulator.monitor_total_ns+=interval_ns;
        accumulator.monitor_max_ns=std::max(accumulator.monitor_max_ns,interval_ns);
        ++accumulator.monitor_bins[std::min<std::size_t>(interval_ns/1000000u,511u)];
    }
    if (accumulator.retained_interval_samples <
        accumulator.interval_samples.size()) {
        accumulator.interval_samples[accumulator.retained_interval_samples++] =
            interval_ns;
    } else {
        accumulator.interval_samples_overflowed = true;
    }
    accumulator.intervals.fetch_add(1u, std::memory_order_relaxed);
    accumulator.total_interval_ns.fetch_add(
        interval_ns, std::memory_order_relaxed);

    std::uint64_t observed_min =
        accumulator.min_interval_ns.load(std::memory_order_relaxed);
    while (interval_ns < observed_min &&
           !accumulator.min_interval_ns.compare_exchange_weak(
               observed_min,
               interval_ns,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
    std::uint64_t observed_max =
        accumulator.max_interval_ns.load(std::memory_order_relaxed);
    while (interval_ns > observed_max &&
           !accumulator.max_interval_ns.compare_exchange_weak(
               observed_max,
               interval_ns,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

void GxBackend::reset_monotonic_cadence(
    MonotonicCadenceAccumulator& accumulator) noexcept {
    accumulator.monitor_bins.fill(0u);
    accumulator.monitor_intervals=accumulator.monitor_total_ns=accumulator.monitor_max_ns=0u;
    accumulator.has_last_sample = false;
    accumulator.last_sample_ns = 0;
    accumulator.retained_interval_samples = 0;
    accumulator.interval_samples_overflowed = false;
    accumulator.samples.store(0, std::memory_order_relaxed);
    accumulator.intervals.store(0, std::memory_order_relaxed);
    accumulator.total_interval_ns.store(0, std::memory_order_relaxed);
    accumulator.min_interval_ns.store(
        ~std::uint64_t{0}, std::memory_order_relaxed);
    accumulator.max_interval_ns.store(0, std::memory_order_relaxed);
    accumulator.published_first_sample_ns.store(0, std::memory_order_relaxed);
    accumulator.published_last_sample_ns.store(0, std::memory_order_relaxed);
}

void GxBackend::report_frame_tail_window() noexcept {
    static const bool enabled=read_env_u64("GALAXY_MONITOR_FRAME_TAILS",0u)!=0u;
    if(!enabled) return;
    const auto now=xfb_first_serial_present_cadence_.last_sample_ns;
    if(!frame_tail_report_ns_) { frame_tail_report_ns_=now; return; }
    if(now-frame_tail_report_ns_<2000000000u) return;
    const auto elapsed=now-frame_tail_report_ns_;frame_tail_report_ns_=now;
    try {
        std::ostringstream row;
        row<<"[new-frame-tails] boundary=owned-XFB-copy-and-first-Present-return physical-display-time=unmeasured window-ns="<<elapsed;
        const auto append=[&](const char* name,MonotonicCadenceAccumulator& a) {
            const auto percentile=[&](unsigned p) {
                const auto rank=(a.monitor_intervals*p+99u)/100u;
                std::uint64_t n=0u;
                for(std::size_t i=0;i<a.monitor_bins.size();++i) {
                    n+=a.monitor_bins[i];
                    if(n>=rank && rank) return i==511u ? a.monitor_max_ns/1000u : (i+1u)*1000u;
                }
                return std::uint64_t{0};
            };
            row<<' '<<name<<"-intervals="<<a.monitor_intervals<<' '<<name<<"-mean-us="
               <<(a.monitor_intervals?a.monitor_total_ns/a.monitor_intervals/1000u:0u)
               <<' '<<name<<"-p95-upper-us="<<percentile(95u)<<' '<<name<<"-p99-upper-us="<<percentile(99u)
               <<' '<<name<<"-max-us="<<a.monitor_max_ns/1000u;
            // Percentile ladder (1 ms bins) to show the shape of the interval distribution: even steps
            // for a continuous process, plateaus for intervals quantized to display retraces. Meaningful
            // only in a window with tens of intervals.
            row<<' '<<name<<"-pct-upper-us="
               <<percentile(5u)<<'/'<<percentile(10u)<<'/'<<percentile(25u)<<'/'
               <<percentile(50u)<<'/'<<percentile(75u)<<'/'<<percentile(90u);
            a.monitor_bins.fill(0u);a.monitor_intervals=a.monitor_total_ns=a.monitor_max_ns=0u;
        };
        append("production",xfb_copy_production_cadence_);append("first-present",xfb_first_serial_present_cadence_);
        row<<'\n';std::cerr<<row.str();
    } catch (...) { /* Optional monitoring must not unwind rendering. */ }
}

void GxBackend::record_pointer_response(const XfbTexture& texture) noexcept {
    static const bool enabled=read_env_u64("GALAXY_MONITOR_POINTER_LATENCY",0u)!=0u;
    if (!enabled) return;
    const auto& stamp=texture.pointer_response;
    if (!stamp.serial || !stamp.selected_ns || !stamp.drawn_ns ||
        (stamp.generation==last_response_.generation && stamp.serial<=last_response_.serial)) return;
    const auto now=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    if (now<stamp.selected_ns || stamp.processed_ns<stamp.selected_ns || stamp.drawn_ns<stamp.processed_ns) return;
    last_response_=stamp;
    const auto latency=now-stamp.selected_ns;
    ++pointer_response_count_; pointer_latency_total_+=latency;
    pointer_latency_max_=std::max(pointer_latency_max_,latency);
    ++pointer_latency_bins_[std::min<std::size_t>(latency/1000000u,511u)];
    if(stamp.acquisition_age_known) {
        ++pointer_acquisition_age_count_;
        pointer_acquisition_age_total_+=stamp.acquisition_age_ms;
        pointer_acquisition_age_max_=std::max(pointer_acquisition_age_max_,stamp.acquisition_age_ms);
        ++pointer_acquisition_age_bins_[std::min<std::uint64_t>(stamp.acquisition_age_ms,511u)];
    }
    if (now-pointer_report_ns_<2000000000u) return;
    pointer_report_ns_=now;
    const auto percentile=[&](unsigned p) {
        const auto rank=(pointer_response_count_*p+99u)/100u;
        std::uint64_t count=0u;
        for (std::size_t i=0u;i<pointer_latency_bins_.size();++i) {
            count+=pointer_latency_bins_[i];
            if(count>=rank) return i==511u ? pointer_latency_max_/1000u : (i+1u)*1000u;
        }
        return std::uint64_t{0};
    };
    try {
        std::ostringstream row;
        row<<"[mouse-response-latency] boundary=selected-KPAD-to-first-marked-XFB-Present-return physical-display-time=unmeasured"
           <<" samples="<<pointer_response_count_<<" mean-us="<<pointer_latency_total_/pointer_response_count_/1000u
           <<" p50-upper-us="<<percentile(50u)<<" p95-upper-us="<<percentile(95u)<<" p99-upper-us="<<percentile(99u)
           <<" max-us="<<pointer_latency_max_/1000u<<" source-serial="<<stamp.serial<<" generation="<<stamp.generation
           <<" xfb-serial="<<texture.copy_serial<<" selected-ns="<<stamp.selected_ns<<" processed-ns="<<stamp.processed_ns
           <<" drawn-ns="<<stamp.drawn_ns<<" present-return-ns="<<now
           <<" acquisition-age-clock=GetTickCount64-coarse-ms acquisition-age-samples="<<pointer_acquisition_age_count_
           <<" acquisition-age-mean-ms="<<(pointer_acquisition_age_count_?pointer_acquisition_age_total_/pointer_acquisition_age_count_:0u)
           <<" acquisition-age-max-ms="<<pointer_acquisition_age_max_
           <<" source-acquisition-age-known="<<stamp.acquisition_age_known
           <<" source-acquisition-age-ms="<<stamp.acquisition_age_ms;
        const auto age_rank=(pointer_acquisition_age_count_*95u+99u)/100u;
        std::uint64_t age_seen=0u,age_p95=0u;
        for(std::size_t i=0;age_rank && i<pointer_acquisition_age_bins_.size();++i) {
            age_seen+=pointer_acquisition_age_bins_[i];
            if(age_seen>=age_rank) {age_p95=i==511u?pointer_acquisition_age_max_:i;break;}
        }
        row<<" acquisition-age-p95-upper-ms="<<age_p95<<'\n';
        std::cerr<<row.str();
    } catch (...) { /* observational text must not unwind rendering */ }
}

bool GxBackend::subframes_shown() const {
    // The renderer's mode, not the chunk's copy of it: a present between chunks follows a change at once.
    return renderer_.interpolation_mode() && std::min({interp_frames_, interp_cadence_cap_, interp_gpu_cap_}) > 0u;
}

bool GxBackend::is_vi_named_xfb(std::uint32_t guest_addr) const noexcept {
    const std::uint32_t addr = xfb_physical_address(guest_addr);
    return std::find(vi_named_xfbs_.begin(), vi_named_xfbs_.end(), addr) != vi_named_xfbs_.end();
}

void GxBackend::note_vi_named_xfb(std::uint32_t guest_addr) noexcept {
    const std::uint32_t addr = xfb_physical_address(guest_addr);
    if (!is_vi_named_xfb(addr)) {
        vi_named_xfbs_[vi_named_xfb_next_] = addr;
        vi_named_xfb_next_ = (vi_named_xfb_next_ + 1u) % vi_named_xfbs_.size();
    }
}

void GxBackend::record_presented_xfb_copy(
    const XfbTexture& texture) noexcept {
    galaxy::timeline::record(galaxy::timeline::Event::XfbPresented, texture.guest_addr,
                         texture.copy_serial);
    record_pointer_response(texture);
    ++stat_xfb_present_presented_;
    const XfbSerialAccountingEvent event =
        account_xfb_present_serial(
            xfb_serial_accounting_,
            texture.last_accounted_present_serial,
            texture.copy_serial);
    last_presented_xfb_stamp_.store(
        texture.frame_stamp, std::memory_order_relaxed);
    last_presented_xfb_serial_.store(
        texture.copy_serial, std::memory_order_relaxed);

    if (event.first_presentation) {
        ++stat_xfb_first_serial_presentations_;
        stat_xfb_highest_first_presented_copy_serial_.store(
            xfb_serial_accounting_.highest_presented_serial,
            std::memory_order_relaxed);
        record_monotonic_cadence_sample(xfb_first_serial_present_cadence_);
        report_frame_tail_window();
        if (event.below_high_water) {
            ++stat_xfb_out_of_order_serial_requests_;
        }
        if (event.delta_one) {
            ++stat_xfb_present_serial_delta_one_;
        } else if (event.delta_multi) {
            ++stat_xfb_present_serial_delta_multi_;
        }
        return;
    }

    ++stat_xfb_repeated_or_stale_serial_requests_;
    if (event.adjacent_repeat) {
        ++stat_xfb_present_serial_delta_zero_;
    } else {
        ++stat_xfb_out_of_order_serial_requests_;
    }
    std::uint64_t observed =
        stat_xfb_present_duplicate_run_max_.load(std::memory_order_relaxed);
    while (event.nonfirst_run > observed &&
           !stat_xfb_present_duplicate_run_max_.compare_exchange_weak(
               observed,
               event.nonfirst_run,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

void GxBackend::record_skipped_duplicate_xfb_present(
    const XfbTexture& texture) noexcept {
    if (texture.copy_serial !=
        last_presented_xfb_serial_.load(std::memory_order_relaxed)) {
        return;
    }

    const XfbSerialAccountingEvent event =
        account_xfb_present_serial(
            xfb_serial_accounting_,
            texture.last_accounted_present_serial,
            texture.copy_serial);
    if (!event.not_first_presentation) {
        return;
    }
    ++stat_xfb_repeated_or_stale_serial_requests_;
    ++stat_xfb_present_skipped_duplicate_presents_;
    if (event.adjacent_repeat) {
        ++stat_xfb_present_serial_delta_zero_;
    } else {
        ++stat_xfb_out_of_order_serial_requests_;
    }
    std::uint64_t observed =
        stat_xfb_present_duplicate_run_max_.load(std::memory_order_relaxed);
    while (event.nonfirst_run > observed &&
           !stat_xfb_present_duplicate_run_max_.compare_exchange_weak(
               observed,
               event.nonfirst_run,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

void GxBackend::notify_guest_memory_write(
    std::uint32_t address,
    std::uint32_t size) noexcept {
    if (size == 0u) {
        return;
    }
    const std::uint64_t write_begin = address;
    const std::uint64_t write_end = write_begin + size;
    const auto mark_physical = [this](
                                   std::uint64_t physical_begin,
                                   std::uint64_t physical_end) noexcept {
        const std::uint64_t tracked_begin = kDirtyTrackedBase;
        const std::uint64_t tracked_end = tracked_begin + kDirtyTrackedSize;
        const std::uint64_t begin = std::max(physical_begin, tracked_begin);
        const std::uint64_t end = std::min(physical_end, tracked_end);
        if (begin >= end) {
            return;
        }

        const std::uint32_t first_page =
            static_cast<std::uint32_t>(
                (begin - tracked_begin) >> kDirtyPageShift);
        const std::uint32_t last_page =
            static_cast<std::uint32_t>(
                (end - 1u - tracked_begin) >> kDirtyPageShift);
        if (last_page >= kDirtyPageCount) {
            return;
        }
        galaxy::guest_mark_dirty_page_words(
            dirty_page_words_.data(), first_page, last_page);
        for (std::uint32_t word = first_page / 64u;; ++word) {
            galaxy::guest_publish_dirty_word_summary(dirty_word_summary_.data(), word);
            if (word == last_page / 64u) break;
        }
    };

    const auto mark_alias = [write_begin, write_end, &mark_physical](
                                std::uint32_t alias_base,
                                std::uint32_t alias_size,
                                std::uint32_t physical_base) noexcept {
        const std::uint64_t alias_begin = alias_base;
        const std::uint64_t alias_end = alias_begin + alias_size;
        const std::uint64_t begin = std::max(write_begin, alias_begin);
        const std::uint64_t end = std::min(write_end, alias_end);
        if (begin >= end) {
            return;
        }
        const std::uint64_t physical_begin =
            static_cast<std::uint64_t>(physical_base) + (begin - alias_begin);
        mark_physical(physical_begin, physical_begin + (end - begin));
    };

    mark_alias(0x00000000u, kMem1Size, kMem1PhysicalBase);
    mark_alias(0x80000000u, kMem1Size, kMem1PhysicalBase);
    mark_alias(0xC0000000u, kMem1Size, kMem1PhysicalBase);
    mark_alias(0x10000000u, kMem2Size, kMem2PhysicalBase);
    mark_alias(0x90000000u, kMem2Size, kMem2PhysicalBase);
    mark_alias(0xD0000000u, kMem2Size, kMem2PhysicalBase);
}

void GxBackend::drain_guest_memory_writes(
    std::vector<GuestWriteRange>& out) noexcept {
    out.clear();
    GuestWriteRange pending_range{};
    bool have_pending = false;
    const auto flush_pending = [&]() {
        if (have_pending) {
            out.push_back(pending_range);
            pending_range = {};
            have_pending = false;
        }
    };

    // Dirty bits exist only for MEM1 and MEM2, so the scan skips the physical hole between them.
    constexpr std::size_t kBytesPerWord = kDirtyPageSize * 64u;
    constexpr std::size_t kMem1EndWord =
        (kMem1PhysicalBase + kMem1Size - kDirtyTrackedBase) / kBytesPerWord;
    constexpr std::size_t kMem2BeginWord =
        (kMem2PhysicalBase - kDirtyTrackedBase) / kBytesPerWord;
    static_assert((kMem1PhysicalBase + kMem1Size) % kBytesPerWord == 0u);
    static_assert(kMem2PhysicalBase % kBytesPerWord == 0u);
    static_assert(kMem1EndWord < kMem2BeginWord &&
                  kMem2BeginWord < kDirtyPageWordCount);
    const std::size_t word_count = dirty_page_words_.size();
    for (std::size_t word_index = 0; word_index < word_count;) {
        // Words are visited in groups of 64 through the group's summary word, so an empty group
        // (most of them) costs one load. Producers always set the summary bit, and this loop clears
        // the summary before exchanging any word of the group, so no publication is missed.
        const std::size_t group = word_index / 64u;
        std::uint64_t pending = 0u;
        if (group < dirty_word_summary_.size()) {
            pending = dirty_word_summary_[group].exchange(
                0u, std::memory_order_relaxed);
        } else {
            pending = kDirtyFullWord;
        }
        std::size_t group_end = (group + 1u) * 64u;
        if (group_end > word_count) {
            group_end = word_count;
        }
        if (pending == 0u) {
            word_index = word_index < kMem1EndWord && group_end >= kMem1EndWord
                ? kMem2BeginWord
                : group_end;
            continue;
        }
        while (pending != 0u) {
            const unsigned in_group =
                static_cast<unsigned>(std::countr_zero(pending));
            pending &= pending - 1u;
            const std::size_t candidate = group * 64u + in_group;
            if (candidate >= group_end) {
                continue;
            }
            // The summary bit is only a hint; the word itself is authoritative.
            if (dirty_page_words_[candidate].load(std::memory_order_relaxed) ==
                0u) {
                continue;
            }
            word_index = candidate;
            // Exchange, not store: a bit a producer sets concurrently must survive.
            std::uint64_t bits = dirty_page_words_[candidate].exchange(
                0u, std::memory_order_relaxed);
            while (bits != 0u) {
                const unsigned bit_index =
                    static_cast<unsigned>(std::countr_zero(bits));
                bits &= bits - 1u;
                const std::uint32_t page = static_cast<std::uint32_t>(
                    candidate * 64u + bit_index);
                if (page >= kDirtyPageCount) {
                    continue;
                }

                const std::uint32_t addr =
                    kDirtyTrackedBase + page * kDirtyPageSize;
                if (have_pending &&
                    pending_range.guest_addr + pending_range.size == addr) {
                    pending_range.size += kDirtyPageSize;
                } else {
                    flush_pending();
                    pending_range = GuestWriteRange{addr, kDirtyPageSize};
                    have_pending = true;
                }
            }
        }
        word_index = word_index < kMem1EndWord && group_end >= kMem1EndWord
            ? kMem2BeginWord
            : group_end;
    }
    flush_pending();
}

void GxBackend::coalesce_dirty_ranges(
    std::vector<GuestWriteRange>& ranges) noexcept {
    if (ranges.size() < 2u) {
        return;
    }

    std::sort(
        ranges.begin(),
        ranges.end(),
        [](const GuestWriteRange& lhs, const GuestWriteRange& rhs) noexcept {
            if (lhs.guest_addr != rhs.guest_addr) {
                return lhs.guest_addr < rhs.guest_addr;
            }
            return lhs.size < rhs.size;
        });

    std::size_t out = 0;
    for (const GuestWriteRange& range : ranges) {
        if (range.size == 0u) {
            continue;
        }
        if (out == 0u) {
            ranges[out++] = range;
            continue;
        }

        GuestWriteRange& previous = ranges[out - 1u];
        const std::uint64_t previous_begin = previous.guest_addr;
        const std::uint64_t previous_end = previous_begin + previous.size;
        const std::uint64_t range_begin = range.guest_addr;
        const std::uint64_t range_end = range_begin + range.size;
        if (range_begin <= previous_end) {
            const std::uint64_t merged_end = std::max(previous_end, range_end);
            previous.size = static_cast<std::uint32_t>(
                merged_end - previous_begin);
            continue;
        }

        ranges[out++] = range;
    }
    ranges.resize(out);
}

std::uint64_t GxBackend::dirty_range_bytes(
    const std::vector<GuestWriteRange>& ranges) noexcept {
    std::uint64_t bytes = 0;
    for (const GuestWriteRange& range : ranges) {
        bytes += range.size;
    }
    return bytes;
}

std::size_t GxBackend::invalidate_dirty_texture_ranges(
    const std::vector<GuestWriteRange>& ranges) {
    if (ranges.empty()) {
        return 0u;
    }

    dirty_texture_ranges_scratch_.clear();
    dirty_texture_ranges_scratch_.reserve(ranges.size());
    for (const GuestWriteRange& range : ranges) {
        dirty_texture_ranges_scratch_.push_back(
            TextureCache::GuestRange{range.guest_addr, range.size});
    }

    // Memory the game rewrote and only flushed: an image handed over there is out of date, so it decodes from memory.
    texture_cache_.drop_registered_images(dirty_texture_ranges_scratch_);
    const std::size_t evicted =
        texture_cache_.invalidate_guest_ranges(dirty_texture_ranges_scratch_);
    if (evicted != 0u) {
        for (const GuestWriteRange& range : ranges) {
            invalidate_texture_binding_cache_range(
                range.guest_addr,
                range.size);
        }
        texture_bindings_dirty_ = true;
    }
    return evicted;
}

void GxBackend::invalidate_dependency_range_cache(
    const std::vector<GuestWriteRange>& ranges) noexcept {
    if (ranges.empty()) {
        return;
    }
    std::uint64_t invalidated = 0;
    for (DependencyRangeCacheEntry& entry : dependency_range_cache_) {
        if (!entry.valid || entry.shape_ranges.empty()) {
            continue;
        }
        bool overlaps = false;
        // The dirty ranges are coalesced and sorted, so each shape range takes one search.
        for (const GuestMemoryRange& shape : entry.shape_ranges) {
            if (shape.size == 0u) {
                continue;
            }
            const std::uint64_t shape_begin =
                normalize_dependency_guest_addr(shape.guest_base);
            const std::uint64_t shape_end = shape_begin + shape.size;
            const auto dirty = std::lower_bound(
                ranges.begin(), ranges.end(), shape_begin,
                [](const GuestWriteRange& range, std::uint64_t begin) {
                    return static_cast<std::uint64_t>(range.guest_addr) +
                               range.size <= begin;
                });
            if (dirty != ranges.end() && dirty->guest_addr < shape_end) {
                overlaps = true;
                break;
            }
        }
        if (overlaps) {
            entry.valid = false;
            ++invalidated;
        }
    }
}

bool GxBackend::ensure_resident_vertex_buffer() {
    gpu::Device* const device = renderer_.device();
    if (device == nullptr) {
        return false;
    }
    if (resident_vertex_buffer_ && resident_vertex_device_ == device) {
        return true;
    }
    if (resident_vertex_buffer_failed_ && resident_vertex_device_ == device) {
        return false;
    }
    // A different device means the old one was shut down (drained) first.
    release_resident_vertex_buffer();
    resident_vertex_device_ = device;
    UploadBuffer buffer = renderer_.create_resident_vertices(
        static_cast<std::size_t>(kResidentDecodedVertexCapacity) * sizeof(GxVertexOut),
        "GX resident decoded vertices");
    if (!buffer) {
        // Not fatal: every hit keeps using the per-frame ring upload.
        resident_vertex_buffer_failed_ = true;
        std::cerr << "[gx-resident-vertices] buffer creation failed; "
                     "decoded vertices stay on the frame ring\n";
        return false;
    }
    resident_vertex_mapped_ = buffer.cpu;
    resident_vertex_gpu_ = buffer.gpu;
    resident_vertex_buffer_ = std::move(buffer);
    resident_vertices_.reset(kResidentDecodedVertexCapacity, kFramesInFlight);
    renderer_.set_resident_vertices(resident_vertex_gpu_,
                                    static_cast<std::uint64_t>(kResidentDecodedVertexCapacity) *
                                        sizeof(GxVertexOut));
    return true;
}

void GxBackend::release_resident_vertex_buffer() noexcept {
    // The GPU no longer reads the buffer (session reset before renderer initialization, or shutdown
    // after drain_gpu()). The arena reset bumps its epoch, invalidating every entry's block.
    resident_vertex_buffer_ = {};
    resident_vertex_mapped_ = nullptr;
    resident_vertex_gpu_ = 0;
    renderer_.set_resident_vertices(0u, 0u);
    resident_vertex_device_ = nullptr;
    resident_vertex_buffer_failed_ = false;
    resident_vertices_.reset(0u, kFramesInFlight);
}

void GxBackend::scan_pe_events_on_sim_thread(
    const std::byte* fifo_data,
    std::size_t fifo_size,
    GuestMemory* memory,
    const GxServices* services) {
    if (fifo_data == nullptr || fifo_size == 0) {
        return;
    }

    PeEventSink sink(event_state_, services);
    try {
        if (event_pending_fifo_.empty()) {
            const std::span<const std::byte> fifo(fifo_data, fifo_size);
            const std::size_t consumed =
                event_parser_.run_available(fifo, memory, sink, event_state_);
            if (consumed < fifo.size()) {
                event_pending_fifo_.assign(
                    fifo.begin() + static_cast<std::ptrdiff_t>(consumed),
                    fifo.end());
            }
        } else {
            event_pending_fifo_.insert(
                event_pending_fifo_.end(), fifo_data, fifo_data + fifo_size);
            const std::size_t consumed = event_parser_.run_available(
                event_pending_fifo_, memory, sink, event_state_);
            if (consumed == event_pending_fifo_.size()) {
                event_pending_fifo_.clear();
            } else if (consumed > 0) {
                event_pending_fifo_.erase(
                    event_pending_fifo_.begin(),
                    event_pending_fifo_.begin() +
                        static_cast<std::ptrdiff_t>(consumed));
            }
        }
    } catch (const GxFatalError& e) {
        std::cerr << "[GxBackend] fatal PE event scan error: " << e.what()
                  << " at offset 0x" << std::hex << e.fifo_offset()
                  << " opcode=0x" << static_cast<unsigned>(e.opcode())
                  << std::dec << '\n';
        throw;
    }
}

// Producer (simulation thread): copies the frame and hands it to the render thread. Blocks only
// while the queue is full.
FramePeCompletionToken GxBackend::render_frame(
    const std::byte* fifo_data,
    std::size_t fifo_size,
    GuestMemory* memory,
    const GxServices* services,
    bool present_swap_chain,
    std::uint32_t displayed_xfb_addr)
{
    if (!initialized_) {
        return {};
    }

    const bool trace_gx_stalls = trace_gx_stalls_enabled();
    // frame_index_ belongs to the render thread; the producer samples on its published copy.
    const bool producer_time_detail =
        trace_gx_stalls ||
        frame_gx_timing_sample_enabled(
            timing_frame_index_.load(std::memory_order_relaxed));
    const bool fifo_empty = fifo_data == nullptr || fifo_size == 0u;
    std::vector<GuestWriteRange>& drained_dirty_ranges = drain_scratch_ranges_;
    drain_guest_memory_writes(drained_dirty_ranges);
    coalesce_dirty_ranges(drained_dirty_ranges);
    if (fifo_empty) {
        if (!drained_dirty_ranges.empty()) {
            invalidate_dependency_range_cache(drained_dirty_ranges);
            pending_async_dirty_ranges_.insert(
                pending_async_dirty_ranges_.end(),
                drained_dirty_ranges.begin(),
                drained_dirty_ranges.end());
            coalesce_dirty_ranges(pending_async_dirty_ranges_);
        }
        if (present_swap_chain) {
            present_cached_xfb(displayed_xfb_addr, true);
        } else {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (render_thread_error_ != nullptr) {
                std::exception_ptr error = render_thread_error_;
                render_thread_error_ = nullptr;
                std::rethrow_exception(error);
            }
        }
        return {};
    }
    galaxy::timeline::record(galaxy::timeline::Event::SubmitBegin, fifo_size);
    // Taken under the same condition that measures producer_wait_ns (the simulation thread's wait
    // for queue space), so a measured frame never subtracts a default time_point.
    const auto wait_start = producer_time_detail
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    FrameChunk chunk;
    chunk.pointer_response=producer_pointer_response_;
    take_attached_pointer_depth_capture(chunk, fifo_size);
    take_attached_fifo_markers(chunk, fifo_size);
    take_attached_stripe_particles(chunk, fifo_size);
    take_attached_shape_records(chunk, fifo_size);
    chunk.prerecorded_movie=producer_movie_content_;
    chunk.safety_surround=producer_safety_surround_;
    chunk.movie_frame=producer_movie_frame_;
    chunk.services = services;
    chunk.memory   = memory;
    chunk.present_swap_chain = present_swap_chain;
    chunk.displayed_xfb_addr = displayed_xfb_addr;
    const GxTimingConfig& timing = get_gx_timing_config();
    if (!timing.vi_render_fifo_wait && !timing.pe_events_gpu_fence) {
        scan_pe_events_on_sim_thread(fifo_data, fifo_size, memory, services);
        chunk.pe_events_scanned_on_sim_thread = true;
    }

    std::uint64_t producer_wait_ns = 0;
    std::uint64_t diagnostic_queue_wait_ns = 0;
    std::size_t queue_depth_after_push = 0;
    FramePeCompletionToken submitted_token{};
    const std::size_t queue_depth_limit = timing.render_queue_depth;
    const bool live_memory_wait =
        timing.render_live_memory_wait && memory != nullptr;
    const bool snapshot_memory =
        timing.render_memory_snapshot &&
        memory != nullptr;
    const bool compact_snapshot =
        snapshot_memory && compact_memory_snapshot_enabled();
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        const auto queued_render_chunks = [this] {
            return static_cast<std::size_t>(
                std::count_if(
                    frame_queue_.begin(),
                    frame_queue_.end(),
                    [](const FrameChunk& queued) {
                        return !queued.present_only;
                    }));
        };
        const std::uint64_t diagnostic_queue_wait_start_ns =
            cadence::steady_now_ns_if_enabled(cadence_diagnostics_);
        const std::size_t stalled_depth_limit =
            std::max(queue_depth_limit, kStalledRenderQueueDepth);
        const GameWaitTimer game_wait{game_wait_qpc_};
        for (;;) {
            if (stop_render_thread_ || render_thread_error_ != nullptr) {
                break;
            }
            const std::size_t queued = queued_render_chunks();
            if (queued < queue_depth_limit) {
                break;
            }
            if (queued >= stalled_depth_limit || render_chunk_started_ns_ == 0u) {
                queue_space_cv_.wait(lock);
                continue;
            }
            const std::uint64_t now = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            const std::uint64_t stalled_at = render_chunk_started_ns_ + kRenderStallNs;
            if (now >= stalled_at) {
                break;
            }
            queue_space_cv_.wait_for(lock, std::chrono::nanoseconds(stalled_at - now));
        }
        if (cadence_diagnostics_ != nullptr) {
            const std::uint64_t diagnostic_queue_wait_end_ns =
                cadence::steady_now_ns_if_enabled(cadence_diagnostics_);
            diagnostic_queue_wait_ns =
                diagnostic_queue_wait_end_ns >= diagnostic_queue_wait_start_ns
                ? diagnostic_queue_wait_end_ns -
                      diagnostic_queue_wait_start_ns
                : 0u;
        }
        if (producer_time_detail) {
            const auto wait_end = std::chrono::steady_clock::now();
            producer_wait_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    wait_end - wait_start).count());
        }
        if (render_thread_error_ != nullptr) {
            // Rethrow the render thread's failure here rather than leave a frozen display.
            std::exception_ptr error = render_thread_error_;
            render_thread_error_ = nullptr;
            std::rethrow_exception(error);
        }
        if (stop_render_thread_) {
            return {};
        }
        if (!fifo_pool_.empty()) {
            chunk.fifo = std::move(fifo_pool_.back());
            fifo_pool_.pop_back();
            chunk.fifo.clear();
        }
        if (!dirty_range_pool_.empty()) {
            chunk.dirty_ranges = std::move(dirty_range_pool_.back());
            dirty_range_pool_.pop_back();
            chunk.dirty_ranges.clear();
        }
        if (snapshot_memory && !memory_snapshot_pool_.empty()) {
            chunk.memory_snapshot = std::move(memory_snapshot_pool_.back());
            memory_snapshot_pool_.pop_back();
        }
        if (compact_snapshot && !memory_range_pool_.empty()) {
            chunk.memory_ranges = std::move(memory_range_pool_.back());
            memory_range_pool_.pop_back();
            chunk.memory_ranges.clear();
        }
    }

    if (fifo_data != nullptr && fifo_size > 0) {
        chunk.fifo.assign(fifo_data, fifo_data + fifo_size);
    }
    if (!pending_async_dirty_ranges_.empty()) {
        chunk.dirty_ranges.swap(pending_async_dirty_ranges_);
        pending_async_dirty_ranges_.clear();
    }
    chunk.dirty_ranges.insert(
        chunk.dirty_ranges.end(),
        drained_dirty_ranges.begin(),
        drained_dirty_ranges.end());
    coalesce_dirty_ranges(chunk.dirty_ranges);
    chunk.dirty_range_count = chunk.dirty_ranges.size();
    chunk.dirty_bytes = dirty_range_bytes(chunk.dirty_ranges);
    invalidate_dependency_range_cache(chunk.dirty_ranges);
    if (snapshot_memory) {
        if (compact_snapshot) {
            const auto dependency_scan_start = producer_time_detail
                    ? std::chrono::steady_clock::now()
                    : std::chrono::steady_clock::time_point{};
            collect_memory_dependency_ranges(
                chunk.fifo.data(),
                chunk.fifo.size(),
                memory,
                chunk.memory_ranges,
                chunk.dependency_cache_hit);
            if (producer_time_detail) {
                const auto dependency_scan_end =
                    std::chrono::steady_clock::now();
                chunk.dependency_scan_ns = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        dependency_scan_end - dependency_scan_start).count());
            }
            chunk.snapshot_range_count = chunk.memory_ranges.size();
            chunk.snapshot_range_bytes =
                guest_memory_range_bytes(chunk.memory_ranges);
            chunk.memory_snapshot_strict = true;
        } else {
            chunk.memory_ranges.clear();
            chunk.memory_snapshot_strict = false;
        }
        const auto snapshot_start = producer_time_detail
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        capture_memory_snapshot(
            memory,
            chunk.memory_snapshot,
            chunk.memory_ranges,
            chunk.memory_snapshot_strict);
        if (!chunk.memory_snapshot.sealed) {
            throw std::runtime_error(
                "[GxBackend] render memory snapshot was not sealed");
        }
        if (producer_time_detail) {
            const auto snapshot_end = std::chrono::steady_clock::now();
            chunk.snapshot_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    snapshot_end - snapshot_start).count());
        }
        chunk.snapshot_bytes = memory_snapshot_used_bytes(chunk.memory_snapshot);
        chunk.memory = &chunk.memory_snapshot.memory;
        chunk.memory_snapshot_active = true;
    }

    if (trace_gx_stalls) {
        const std::uint64_t dependency_scan_us =
            chunk.dependency_scan_ns / 1000u;
        if (dependency_scan_us >= trace_gx_stall_threshold_us()) {
            std::cerr << "[gx-dependency-scan] us=" << dependency_scan_us
                      << " fifo-bytes=" << fifo_size
                      << " ranges=" << chunk.snapshot_range_count
                      << " range-bytes=" << chunk.snapshot_range_bytes
                      << " pending-fifo=" << dependency_pending_fifo_.size()
                      << '\n';
        }
        const std::uint64_t snapshot_us = chunk.snapshot_ns / 1000u;
        if (snapshot_us >= trace_gx_stall_threshold_us()) {
            std::cerr << "[gx-memory-snapshot] us=" << snapshot_us
                      << " copied-bytes=" << chunk.snapshot_bytes
                      << " ranges=" << chunk.snapshot_range_count
                      << " range-bytes=" << chunk.snapshot_range_bytes
                      << " strict=" << (chunk.memory_snapshot_strict ? 1 : 0)
                      << '\n';
        }
    }

    chunk.producer_wait_ns = producer_wait_ns;
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (render_thread_error_ != nullptr) {
            std::exception_ptr error = render_thread_error_;
            render_thread_error_ = nullptr;
            std::rethrow_exception(error);
        }
        if (stop_render_thread_) {
            // Same retention bound as the render thread's return path.
            if (chunk.memory_snapshot_active) {
                if (memory_snapshot_pool_.size() < kChunkPoolRetainedBuffers) {
                    memory_snapshot_pool_.push_back(
                        std::move(chunk.memory_snapshot));
                }
            }
            if (!chunk.memory_ranges.empty()) {
                if (memory_range_pool_.size() < kChunkPoolRetainedBuffers) {
                    memory_range_pool_.push_back(std::move(chunk.memory_ranges));
                }
            }
            return {};
        }
        // The render thread resolves the token when it has consumed the chunk (its PE events, if
        // any, delivered then): the stream is parsed once, there, not again here to find events.
        submitted_token = frame_pe_completions_.issue();
        chunk.pe_completion_token = submitted_token;
        chunk.pe_completion_requires_resolution = true;
        frame_queue_.push_back(std::move(chunk));
        queue_depth_after_push = frame_queue_.size();
    }
    if (cadence_diagnostics_ != nullptr) {
        cadence_diagnostics_->record_phase(
            cadence::TimingPhase::ProducerQueueWait,
            diagnostic_queue_wait_ns);
        cadence_diagnostics_->record_token_issued(submitted_token.kind);
    }
    // Publication and token accounting precede wakeup; diagnostics do not.
    queue_cv_.notify_one();
    const std::uint64_t producer_wait_us = producer_wait_ns / 1000u;
    if (trace_gx_stalls &&
        producer_wait_us >= trace_gx_stall_threshold_us()) {
        std::cerr << "[gx-queue-wait] wait-us=" << producer_wait_us
                  << " fifo-bytes=" << fifo_size
                  << " queue-depth=" << queue_depth_after_push
                  << " queue-limit=" << queue_depth_limit << '\n';
    }
    if (live_memory_wait) {
        const cadence::ScopedPhaseTimer live_memory_wait_timer{
            cadence_diagnostics_,
            cadence::TimingPhase::LiveMemoryReadBarrierWait};
        wait_for_render_cpu_reads_idle();
    }
    galaxy::timeline::record(galaxy::timeline::Event::SubmitEnd, submitted_token.value,
                             static_cast<std::uint64_t>(submitted_token.kind));
    return submitted_token;
}

void GxBackend::wait_for_render_idle() {
    if (!initialized_) {
        return;
    }

    const GameWaitTimer game_wait{game_wait_qpc_};
    std::unique_lock<std::mutex> lock(queue_mutex_);
    queue_space_cv_.wait(lock, [this] {
        return stop_render_thread_ ||
               render_thread_error_ != nullptr ||
               (frame_queue_.empty() &&
                render_chunks_in_flight_ == 0u &&
                present_chunks_in_flight_ == 0u &&
                utility_chunks_in_flight_ == 0u &&
                pending_frame_effects_.empty() &&
                !pending_pointer_depth_capture_.capture);
    });
    if (render_thread_error_ != nullptr) {
        std::exception_ptr error = render_thread_error_;
        render_thread_error_ = nullptr;
        std::rethrow_exception(error);
    }
}

void GxBackend::wait_for_render_cpu_reads_idle() {
    if (!initialized_) {
        return;
    }

    const GxTimingConfig& timing = get_gx_timing_config();
    // When the renderer reads its own snapshot, or reads guest memory live by design (the game's
    // GXDrawDone discipline bounds its writes), this waits only for queue space, as render_frame()'s
    // push does; a full drain would serialize the two threads. Otherwise the renderer reads memory
    // the game is about to change, and the queue must drain.
    const std::size_t queue_depth_limit = timing.render_queue_depth;
    const bool renderer_owns_its_memory =
        timing.render_memory_snapshot || timing.render_live_memory_reads;

    const GameWaitTimer game_wait{game_wait_qpc_};
    std::unique_lock<std::mutex> lock(queue_mutex_);
    const auto queued_render_chunks = [this] {
        return static_cast<std::size_t>(
            std::count_if(
                frame_queue_.begin(),
                frame_queue_.end(),
                [](const FrameChunk& queued) {
                    return !queued.present_only;
                }));
    };
    queue_space_cv_.wait(lock, [this, &queued_render_chunks, queue_depth_limit,
                                renderer_owns_its_memory] {
        if (stop_render_thread_ || render_thread_error_ != nullptr) {
            return true;
        }
        if (renderer_owns_its_memory) {
            // Depth only: render_cpu_reads_in_flight_ drops after the render thread pops a chunk,
            // so also waiting for it here can deadlock against a full queue.
            return queued_render_chunks() < queue_depth_limit;
        }
        return queued_render_chunks() == 0u &&
               render_cpu_reads_in_flight_ == 0u;
    });
    if (render_thread_error_ != nullptr) {
        std::exception_ptr error = render_thread_error_;
        render_thread_error_ = nullptr;
        std::rethrow_exception(error);
    }
}

bool GxBackend::wait_for_frame_pe_completion_for(
    FramePeCompletionToken token,
    std::uint32_t timeout_ms) {
    if (!initialized_) {
        throw std::logic_error(
            "frame-specific PE completion wait used an uninitialized GX backend");
    }

    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeout_ms);
    const GameWaitTimer game_wait{game_wait_qpc_};
    std::unique_lock<std::mutex> lock(queue_mutex_);
    for (;;) {
        if (render_thread_error_ != nullptr) {
            std::exception_ptr error = render_thread_error_;
            render_thread_error_ = nullptr;
            std::rethrow_exception(error);
        }
        if (frame_pe_completions_.probe_wait(token) ==
            FramePeWaitState::Complete) {
            frame_pe_completions_.consume_wait(token);
            galaxy::timeline::record(galaxy::timeline::Event::TokenConsumed, token.value);
            return true;
        }
        if (stop_render_thread_) {
            throw std::runtime_error(
                "render thread stopped before frame-specific PE completion");
        }
        if (timeout_ms == 0u ||
            std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        (void)queue_space_cv_.wait_until(lock, deadline);
    }
}

// A perspective draw in the temporal scene: the clip-space offset that moves
// the image by the scene jitter in render pixels at this draw's viewport.
// The first such draw also supplies the camera planes and field of view.
void GxBackend::apply_scene_jitter(const GxVsConstants& cpu, GxVsConstants& upload,
                                   float jitter_ndc[2]) {
    jitter_ndc[0] = 0.0f;
    jitter_ndc[1] = 0.0f;
    if (state_.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 6u)) != 0u) {
        return;
    }
    const auto xf_float = [this](unsigned offset) {
        return std::bit_cast<float>(state_.xf(static_cast<std::uint16_t>(offset)));
    };
    const float scale = static_cast<float>(renderer_.efb_scale());
    const float width = 2.0f * std::fabs(xf_float(xf::kViewportBase)) * scale;
    const float height = 2.0f * std::fabs(xf_float(xf::kViewportBase + 1u)) * scale;
    if (!(width > 0.0f) || !(height > 0.0f)) {
        return;
    }
    const float jx = 2.0f * scene_jitter_px_[0] / width;
    const float jy = -2.0f * scene_jitter_px_[1] / height;
    jitter_ndc[0] = jx;
    jitter_ndc[1] = jy;
    // Read the CPU image, write the upload (write-combined memory).
    for (unsigned column = 0; column < 4u; ++column) {
        upload.projection[0][column] = cpu.projection[0][column] + jx * cpu.projection[3][column];
        upload.projection[1][column] = cpu.projection[1][column] + jy * cpu.projection[3][column];
    }
    if (!scene_camera_captured_) {
        scene_camera_captured_ = true;
        // GX perspective: clip z = E z + F, w = -z; NDC z -1 at near, 0 at far.
        const float c = xf_float(xf::kProjectionBase + 2u);
        const float e = xf_float(xf::kProjectionBase + 4u);
        const float f = xf_float(xf::kProjectionBase + 5u);
        const float near_plane = f / (e - 1.0f);
        const float far_plane = f / e;
        const float fov_y = c != 0.0f ? 2.0f * std::atan(1.0f / std::fabs(c)) : 1.0f;
        if (std::isfinite(near_plane) && std::isfinite(far_plane) &&
            near_plane > 0.0f && far_plane > near_plane) {
            renderer_.set_scene_camera(near_plane, far_plane, fov_y);
        }
    }
}

// One VS constant upload inside a temporal scene. Palettes are addressed
// from the matrix ring base (the draw's root SRV), so every upload carries
// its snapshot's offset; the transform record and the motion fields follow.
void GxBackend::write_scene_vs_constants(const GxVsConstants& cpu, GxVsConstants& upload) {
    float jitter_ndc[2]{};
    if (temporal_scene_) {
        apply_scene_jitter(cpu, upload, jitter_ndc);
    }
    const gpu::Address ring_base = renderer_.matrix_ring().gpu_base();
    const std::uint32_t palette_offset = current_matrix_palette_ != 0u
        ? static_cast<std::uint32_t>((current_matrix_palette_ - ring_base) / 16u)
        : kNoPaletteOffset;
    upload.mtx_palette_base = palette_offset != kNoPaletteOffset ? palette_offset : 0u;
    temporal_vs_palette_va_ = current_matrix_palette_;

    const auto xf_float = [this](unsigned offset) {
        return std::bit_cast<float>(state_.xf(static_cast<std::uint16_t>(offset)));
    };
    if (std::exchange(scene_chunk_restart_, false) && interp_recording_ && !motion_scene_ &&
        current_replay_record_ >= 0) [[unlikely]] {
        // The chunk start re-uploaded the bindings of the open record: when
        // nothing changed (no marker, no matrix write, the same constants and
        // palette), its draws stay in that record instead of an unmatched one.
        InterpScene& scene = interp_scenes_[interp_scene_];
        const auto r = static_cast<std::size_t>(current_replay_record_);
        const std::uint64_t base = combine64(
            identity_marker_, static_cast<std::uint32_t>(state_.matrix_write_count() - identity_matrix_base_));
        if (identity_scene_ && base == identity_last_base_ && r < scene.constants.size()) {
            GxVsConstants probe = cpu;
            probe.mtx_palette_base = scene.constants[r].mtx_palette_base;
            apply_uber_vs_key(vs_key_, probe);
            // Derived from the fields compared (write_scene_vs_constants).
            std::memcpy(probe.motion_prev_proj, scene.constants[r].motion_prev_proj,
                        offsetof(GxVsConstants, motion_ctrl) + sizeof(probe.motion_ctrl) -
                            offsetof(GxVsConstants, motion_prev_proj));
            const std::int32_t shadow = scene.palette[r];
            const bool palette_continues =
                shadow < 0
                    ? !current_palette_required_
                    : current_palette_shadow_ >= 0 &&
                          (current_palette_shadow_ == shadow ||
                           std::memcmp(scene.palettes.data() + static_cast<std::size_t>(shadow) * kMatrixPaletteWords,
                                       scene.palettes.data() +
                                           static_cast<std::size_t>(current_palette_shadow_) * kMatrixPaletteWords,
                                       kMatrixPaletteWords * sizeof(std::uint32_t)) == 0);
            if (palette_continues && std::memcmp(&probe, &scene.constants[r], sizeof(probe)) == 0) {
                if (shadow >= 0 && current_palette_shadow_ != shadow &&
                    static_cast<std::size_t>(current_palette_shadow_) + 1u == scene.palette_count) {
                    --scene.palette_count;  // the duplicate snapshot
                }
                current_palette_shadow_ = shadow;
                ++interp_chunk_continues_;
                return;
            }
        }
    }
    TransformRecord record{};
    record.signature = combine64(
        combine64(mix64(current_vs_hash_ & ~0ull), current_ps_hash_),
        record_inputs_ != nullptr
            ? record_inputs_->positions ^ (static_cast<std::uint64_t>(record_inputs_->matrix_bits) << 48u)
            : (static_cast<std::uint64_t>(state_.cp(cp::kArrayBaseBase)) << 32u) ^
                  (static_cast<std::uint64_t>(state_.cp(cp::kVcdLo)) << 16u) ^ state_.cp(cp::kVcdHi));
    record.identity = record_identity();
    record.layout = current_vs_hash_;
    record.load_signature = state_.matrix_load_signature();
    record.category = identity_category_;
    record.continuity = identity_continuity_;
    record.particle = identity_continuity_particle_;
    record.palette = (vs_key_.flags & 0x01u) != 0u;
    if (record.palette) {
        // PNMTX0 (XF rows 0-2) stands for the per-vertex palette's object.
        for (unsigned i = 0; i < 12u; ++i) record.position[i] = xf_float(i);
        record.palette_offset = palette_offset;
    } else {
        std::memcpy(record.position.data(), cpu.inline_pos_matrix, sizeof(record.position));
    }
    record.translation[0] = record.position[3];
    record.translation[1] = record.position[7];
    record.translation[2] = record.position[11];
    for (unsigned i = 0; i < 6u; ++i) {
        record.projection[i] = xf_float(xf::kProjectionBase + i);
    }
    record.projection[6] =
        state_.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 6u)) != 0u ? 1.0f : 0.0f;
    TransformHistory& history = side_layer_ == kScreenLayer ? screen_history_
        : side_layer_ == kMirrorLayer                         ? mirror_history_
                                                              : transform_history_;
    const TransformRecord* previous = history.add(record);
    if (interp_recording_) [[unlikely]] {
        // The replay rebuilds this record's constants per sub-frame.
        InterpScene& scene = interp_scenes_[interp_scene_];
        current_replay_record_ = static_cast<std::int32_t>(scene.constants.size());
        scene.constants.push_back(cpu);
        // Not read back from `upload`: the ring is write-combined memory.
        scene.constants.back().mtx_palette_base = palette_offset != kNoPaletteOffset ? palette_offset : 0u;
        // Uber pipelines read the key from the constants: a sub-frame's copy
        // needs it as the frame's upload has it.
        apply_uber_vs_key(vs_key_, scene.constants.back());
        const std::int32_t m = history.current_matches().back();
        scene.prev_index.push_back(m);
        // Inline draws read normals, lights and indexed texture matrices from
        // the palette too: their snapshot is interpolated like a skinned one.
        scene.palette.push_back(record.palette || current_palette_required_ ? current_palette_shadow_ : -1);
        scene.per_vertex.push_back(record.palette ? 1u : 0u);
        scene.category.push_back(record.category);
    }
    if (!motion_scene_) {
        return;
    }

    // Previous transform: the matched record, else the camera delta. With
    // temporal upscaling of the replays, every presented frame is a replay
    // whose program moves what moves: a block keeps its own transform.
    const bool replays_upscaled = renderer_.temporal_interpolation();
    const Affine34& camera = transform_history_.camera_delta();
    std::uint32_t mode = 1u;
    std::uint32_t previous_palette = 0u;
    Affine34 prev_pos = replays_upscaled ? affine_identity() : camera;
    if (replays_upscaled) {
        if (!record.palette) {
            prev_pos = record.position;
        } else if (palette_offset != kNoPaletteOffset) {
            mode = 0u;
            previous_palette = palette_offset;
        }
    } else if (record.palette) {
        if (previous != nullptr && previous->palette_offset != kNoPaletteOffset &&
            transform_history_.current_frame() - transform_history_.previous_frame() <=
                renderer_.matrix_history_frames()) {
            mode = 0u;
            previous_palette = previous->palette_offset;
        }
    } else {
        prev_pos = previous != nullptr ? previous->position
                                       : affine_multiply(camera, record.position);
    }
    const std::array<float, 7>& proj =
        previous != nullptr && !replays_upscaled ? previous->projection : record.projection;
    upload.motion_prev_proj[0][0] = proj[0];
    upload.motion_prev_proj[0][1] = proj[1];
    upload.motion_prev_proj[0][2] = proj[2];
    upload.motion_prev_proj[0][3] = proj[3];
    upload.motion_prev_proj[1][0] = proj[4];
    upload.motion_prev_proj[1][1] = proj[5];
    upload.motion_prev_proj[1][2] = proj[6];
    upload.motion_prev_proj[1][3] = 0.0f;
    std::memcpy(upload.motion_prev_pos, prev_pos.data(), sizeof(upload.motion_prev_pos));
    upload.motion_params[0] = jitter_ndc[0];
    upload.motion_params[1] = jitter_ndc[1];
    upload.motion_params[2] = 0.5f * cpu.line_point_raster[0];
    upload.motion_params[3] = -0.5f * cpu.line_point_raster[1];
    upload.motion_ctrl[0] = mode;
    upload.motion_ctrl[1] = previous_palette;
    upload.motion_ctrl[2] = 0u;
    upload.motion_ctrl[3] = 0u;
    if (interp_recording_ && replays_upscaled) [[unlikely]] {
        // The sequence's base copies carry them: the CPU image too (evaluation check).
        // Written from the values above: `upload` is write-combined memory.
        GxVsConstants& image = interp_scenes_[interp_scene_].constants.back();
        for (unsigned i = 0; i < 4u; ++i) {
            image.motion_prev_proj[0][i] = proj[i];
        }
        image.motion_prev_proj[1][0] = proj[4];
        image.motion_prev_proj[1][1] = proj[5];
        image.motion_prev_proj[1][2] = proj[6];
        image.motion_prev_proj[1][3] = 0.0f;
        std::memcpy(image.motion_prev_pos, prev_pos.data(), sizeof(image.motion_prev_pos));
        image.motion_params[0] = jitter_ndc[0];
        image.motion_params[1] = jitter_ndc[1];
        image.motion_params[2] = 0.5f * cpu.line_point_raster[0];
        image.motion_params[3] = -0.5f * cpu.line_point_raster[1];
        image.motion_ctrl[0] = mode;
        image.motion_ctrl[1] = previous_palette;
        image.motion_ctrl[2] = 0u;
        image.motion_ctrl[3] = 0u;
    }
}

std::uint32_t GxBackend::note_identity_occurrence(std::uint64_t key) {
    if (identity_occurrences_.size() < 2u * (identity_occurrences_used_ + 1u)) {
        // Grow and rehash this scene's entries; older scenes' entries drop out.
        std::vector<IdentityOccurrence> old;
        old.swap(identity_occurrences_);
        identity_occurrences_.assign(std::max<std::size_t>(old.size() * 2u, 8192u),
                                     IdentityOccurrence{});
        identity_occurrences_used_ = 0u;
        const std::size_t mask = identity_occurrences_.size() - 1u;
        for (const IdentityOccurrence& entry : old) {
            if (entry.scene != identity_scene_index_ || entry.count == 0u) continue;
            std::size_t slot = static_cast<std::size_t>(entry.key ^ (entry.key >> 29u)) & mask;
            while (identity_occurrences_[slot].scene == identity_scene_index_ &&
                   identity_occurrences_[slot].count != 0u) {
                slot = (slot + 1u) & mask;
            }
            identity_occurrences_[slot] = entry;
            ++identity_occurrences_used_;
        }
    }
    const std::size_t mask = identity_occurrences_.size() - 1u;
    std::size_t slot = static_cast<std::size_t>(key ^ (key >> 29u)) & mask;
    for (;;) {
        IdentityOccurrence& entry = identity_occurrences_[slot];
        if (entry.scene != identity_scene_index_ || entry.count == 0u) {
            entry = {key, identity_scene_index_, 1u};
            ++identity_occurrences_used_;
            return 0u;
        }
        if (entry.key == key) {
            return entry.count++;
        }
        slot = (slot + 1u) & mask;
    }
}

void GxBackend::begin_identity_scene() {
    identity_scene_ = identity_scene_markers_ != 0u;
    identity_scene_markers_ = 0u;
    ++identity_scene_index_;
    identity_occurrences_used_ = 0u;
    identity_marker_ = mix64(0x5343454E45ull);
    identity_matrix_base_ = state_.matrix_write_count();
    identity_last_base_ = 0u;
    identity_sub_index_ = 0u;
    identity_continuity_ = 0u;
    identity_continuity_particle_ = false;
    identity_category_ = motion::kNoCategory;
    state_.reset_matrix_load_signature();
}

std::uint64_t GxBackend::record_identity() noexcept {
    if (!identity_scene_) {
        return 0u;
    }
    const std::uint64_t base = combine64(
        identity_marker_, static_cast<std::uint32_t>(state_.matrix_write_count() - identity_matrix_base_));
    identity_sub_index_ = base == identity_last_base_ ? identity_sub_index_ + 1u : 0u;
    identity_last_base_ = base;
    const std::uint64_t key = combine64(base, identity_sub_index_);
    return key != 0u ? key : 1u;
}

std::uint32_t GxBackend::rt_light_key() noexcept {
    const std::uint64_t key = mix64(mix64(rt_light_object_, rt_light_material_), rt_light_draws_++);
    const auto folded = static_cast<std::uint32_t>(key ^ (key >> 32u));
    return folded != 0u ? folded : 1u;
}

void GxBackend::on_host_marker(std::uint8_t kind, std::uint16_t aux, std::uint32_t a,
                               std::uint32_t b) {
    if (kind == motion::kMarkerClearQuad) {
        clear_quad_pending_ = true;
        clear_quad_z24_ = a & 0xFFFFFFu;
        clear_quad_has_colour_ = aux == 1u;
        clear_quad_colour_ = b;
        return;
    }
    // The game's bloom, drawn by the renderer at this point of the stream (game_bloom.h).
    if (kind == motion::kMarkerBloom) {
        flush_pending_draw_batch();
        renderer_.game_bloom(aux, a, b);
        return;
    }
    // The traced draws' identity (rt_light_key): an object, a model's shape, an emitter (its object, then its resource
    // or shape) or a particle (its pool node, its lifetime: the next particle in the node is another) starts it over.
    if (kind == motion::kMarkerNameObj || kind == motion::kMarkerShapePacket || kind == motion::kMarkerEmitter ||
        kind == motion::kMarkerParticle) {
        rt_light_object_ = mix64(mix64(kind, a), kind == motion::kMarkerParticle ? (b & 0xFFFFu) : b);
        rt_light_material_ = 0u;
        rt_light_draws_ = 0u;
    } else if (kind == motion::kMarkerMaterial) {
        rt_light_material_ = a;
        rt_light_draws_ = 0u;
    }
    if (kind < identity_markers_by_kind_.size()) {
        if (identity_markers_by_kind_[kind]++ == 0u && rt_log_glows()) [[unlikely]] {
            std::fprintf(stderr, "[rt-identity] first marker of kind %u\n", static_cast<unsigned>(kind));
        }
    }
    // The J3D material whose shapes draw next: the table's row for it, where it has one, over its texture's.
    if (kind == motion::kMarkerMaterial) {
        rt_material_identity_ = rt_material_by_identity(a);
        rt_identity_seen_ = a;
        // GALAXY_RT_LOG_GLOWS=1: each identity the first time it is seen, and the row it found.
        if (rt_log_glows()) [[unlikely]] {
            static std::unordered_set<std::uint32_t> logged;
            if (logged.size() < 4096u && logged.insert(a).second) {
                std::fprintf(stderr, "[rt-identity] seen %08X -> material %u\n", a,
                             static_cast<unsigned>(rt_material_identity_));
            }
        }
        refresh_rt_material();
        return;
    }
    // The effect an emitter's particles belong to (its marker follows the emitter's): its table row, where it has one,
    // over their textures' (a fire's light by its name, whatever its sprites' textures).
    if (kind == motion::kMarkerEffect) {
        rt_effect_material_ = rt_material_by_identity(a);
        if (rt_log_glows()) [[unlikely]] {
            static std::unordered_set<std::uint32_t> logged;
            if (a != 0u && logged.size() < 4096u && logged.insert(a).second) {
                std::fprintf(stderr, "[rt-effect] seen %08X -> material %u\n", a,
                             static_cast<unsigned>(rt_effect_material_));
            }
        }
        refresh_rt_material();
        return;
    }
    // Draws the traced scene takes and the picture never shows (the player's body in the first-person view). The
    // batch before them is drawn as it was.
    if (kind == motion::kMarkerTraceOnly) {
        flush_pending_draw_batch();
        rt_trace_only_ = aux != 0u;
        return;
    }
    if (kind >= motion::kMarkerCategory) {
        // A draw list or phase begins: no emitter's particles until the next emitter.
        if (rt_effect_material_ != 0u) {
            rt_effect_material_ = 0u;
            refresh_rt_material();
        }
        on_motion_marker(kind, aux, a, b);
        return;
    }
    // The drawing object for the material census and the named classes: a NameObj's draws follow
    // its marker; emitters and particles are none.
    if (kind != motion::kMarkerShapePacket && kind != motion::kMarkerMaterial) {
        rt_identity_seen_ = 0u;
    }
    if (kind != motion::kMarkerShapePacket && rt_material_identity_ != 0u) {
        // A NameObj's own draws, emitters and particles: no J3D material.
        rt_material_identity_ = 0u;
        refresh_rt_material();
    }
    // A particle keeps its emitter's effect; anything else (a new emitter, before its effect marker; an object; a
    // model's shape) ends it.
    if (kind != motion::kMarkerParticle && rt_effect_material_ != 0u) {
        rt_effect_material_ = 0u;
        refresh_rt_material();
    }
    if (kind == motion::kMarkerNameObj) {
        rt_object_ = a;
        rt_object_fake_shadow_ = renderer_.rt_lighting() && rt_fake_shadow_object(a);
    } else if (kind == motion::kMarkerShapePacket) {
        // A model from a draw buffer: a shadow drawer draws through GX directly, so the flag of the
        // object before it does not apply.
        rt_object_fake_shadow_ = false;
    } else if (kind == motion::kMarkerEmitter || kind == motion::kMarkerParticle) {
        rt_object_ = 0u;
        rt_object_fake_shadow_ = false;
    }
    if (!history_scene_) {
        return;
    }
    ++(side_layer_ != 0u ? layer_identity_markers_[side_layer_] : identity_scene_markers_);
    std::uint64_t tag = b;
    identity_continuity_particle_ = kind == motion::kMarkerParticle;
    identity_continuity_ = 0u;
    if (identity_continuity_particle_) {
        // Particles come from a pool: node, pass and lifetime name the slot's
        // particle; its age (the record's continuity) must continue on a
        // match, else the slot holds a new particle (TransformHistory::add).
        identity_continuity_ = b >> 16u;
        tag = b & 0xFFFFu;
    }
    set_identity_marker(kind, aux, a, static_cast<std::uint32_t>(tag));
}

void GxBackend::set_identity_marker(std::uint8_t kind, std::uint16_t aux, std::uint32_t a, std::uint32_t tag) {
    std::uint64_t key = combine64(combine64(kind | (static_cast<std::uint64_t>(aux) << 8u), a), tag);
    key = combine64(key, note_identity_occurrence(key));
    identity_marker_ = key;
    identity_matrix_base_ = state_.matrix_write_count();
    state_.reset_matrix_load_signature();
}

void GxBackend::on_motion_marker(std::uint8_t kind, std::uint16_t aux, std::uint32_t a, std::uint32_t b) {
    switch (kind) {
    case motion::kMarkerCategory: {
        ++motion_category_markers_;
        // The game's sky lists (MR::DrawBufferType Sky 1, Air 2, AstroDomeSky 0x23) until the next list, draw type or
        // phase. The sun list (Sun 3: its disc and glow) is neither sky nor scene: the sun is the key light, and its glow
        // in the sky's light would count it twice and, the sky's level being its mean, darken the rest of the sky.
        const bool list = aux == motion::kCategoryBufferOpa || aux == motion::kCategoryBufferXlu;
        rt_sky_buffer_ = list && ((a & 0xFFFFu) == 1u || (a & 0xFFFFu) == 2u || (a & 0xFFFFu) == 0x23u);
        rt_sun_buffer_ = list && (a & 0xFFFFu) == 3u;
        // The game's shadow lists (MR::DrawType ShadowSurface 0x26, ShadowVolume 0x27, AlphaShadow 0x29).
        rt_fake_shadow_list_ = aux == motion::kCategoryDrawType &&
                               ((a & 0xFFFFu) == 0x26u || (a & 0xFFFFu) == 0x27u || (a & 0xFFFFu) == 0x29u);
        if (aux == motion::kCategoryPhase) {
            note_rt_phase(static_cast<std::uint8_t>(a));
        }
        // The game's effects begin (its ShadowSurface draw type 0x26, the first list after the translucent ones in
        // GameScene::draw3D): the effects layer opens while the water's composite is still to come, so that composite
        // goes under what they draw.
        if (aux == motion::kCategoryDrawType && (a & 0xFFFFu) == 0x26u && scene_phase_ && motion_scene_ &&
            renderer_.rt_water_open()) {
            flush_pending_draw_batch();
            renderer_.begin_layer_window();
        }
        if (aux == motion::kCategoryBufferOpa || aux == motion::kCategoryBufferXlu) {
            // The draw buffer (MR::DrawBufferType) the next draws come from (the census, the de-baking).
            rt_draw_buffer_ = (a & 0xFFFFu) | (aux == motion::kCategoryBufferXlu ? 0x10000u : 0u);
        } else if (aux == motion::kCategoryDrawType) {
            rt_draw_buffer_ = 0xFFFFu;   // a draw type's list (plants, oceans, ropes): no buffer
        }
        if (!history_scene_) {
            return;
        }
        std::uint8_t phase = motion::category_phase(identity_category_);
        if (aux == motion::kCategoryPhase) {
            phase = static_cast<std::uint8_t>(a);
            if (phase == static_cast<std::uint8_t>(motion::ScenePhase::ImageEffect)) {
                ++motion_image_effect_markers_;
            }
        }
        identity_category_ = motion::encode_category(phase, aux, a);
        return;
    }
    case motion::kMarkerScene:
        ++motion_scene_markers_;
        if ((aux & motion::kSceneCameraCut) != 0u) {
            // A cut with this many objects appearing or reset is a stage load (another sky and other
            // lights): stage loads bring thousands, cuts within a stage none.
            constexpr std::uint32_t kStageObjectEvents = 256u;
            rt_scene_cut_ = true;
            rt_cut_objects_ = std::max(rt_cut_objects_, b);
            if (b >= kStageObjectEvents && !scene_stage_known_) {
                begin_rt_stage();
            }
        }
        if (!history_scene_) {
            return;
        }
        // The game cut the view: the temporal upscaler's history is invalid.
        if (temporal_scene_ && (aux & motion::kSceneCameraCut) != 0u) {
            renderer_.reset_temporal_history();
        }
        if (interp_recording_) {
            InterpScene& scene = interp_scenes_[interp_scene_];
            scene.camera_epoch = a;
            scene.camera_epoch_known = true;
        }
        return;
    case motion::kMarkerPass:
        ++motion_mirror_markers_;
        rt_mirror_pass_ = aux == static_cast<std::uint16_t>(motion::PassMarker::MirrorBegin);
        if (rt_mirror_pass_) {
            // The frame's A/B state before its scene begins (rt_hidden_draw reads it for the mirror's draws).
            renderer_.rt_set_ab_original(((g_snapshot_presents + 1u) & 1u) != 0u);
        }
        if (aux == static_cast<std::uint16_t>(motion::PassMarker::MirrorBegin)) {
            end_side_layer();
            begin_side_layer(kMirrorLayer);
        } else if (side_layer_ == kMirrorLayer) {
            end_side_layer();
        }
        return;
    case motion::kMarkerLight: {
        ++motion_light_markers_;
        const ActorLightLoad load = actor_light_load(aux, b);
        // GX lights 0 and 1 of an ActorLightInfo or coin load (LightFunction::loadLightInfo).
        const auto hooked = static_cast<std::uint8_t>(
            aux <= static_cast<std::uint16_t>(motion::LightSite::Coin) ? load.written & 0x03u : 0u);
        rt_known_lights_ = static_cast<std::uint8_t>((rt_known_lights_ & ~load.written) | hooked);
        rt_follow_lights_ = static_cast<std::uint8_t>((rt_follow_lights_ & ~load.written) | load.follow);
        if (rt_light_tracking_) {
            rt_light_pending_ = static_cast<std::uint8_t>((rt_light_pending_ & ~load.written) | hooked);
        }
        return;
    }
    case motion::kMarkerLayoutPane:
    case motion::kMarkerLayoutActor:
    case motion::kMarkerStarPointer: {
        if (side_layer_ == kScreenLayer) {
            // The pane, layout actor or pointer layout names the draws after it.
            ++layer_identity_markers_[kScreenLayer];
            identity_continuity_particle_ = false;
            identity_continuity_ = 0u;
            set_identity_marker(kind, aux, a, b);
        }
        return;
    }
    default:
        return;
    }
}

// GameScene::draw2D's entry (FifoMarker::Scene2DBegin).
void GxBackend::on_scene_2d_begin() {
    if (scene_phase_) {
        // GameScene::draw3D returned before its clearZBuffer (movie, nerve):
        // the scene ends here.
        on_fifo_marker(FifoMarker::SceneEnd, nullptr);
    }
    ++window_2d_scenes_;
}

void GxBackend::take_attached_stripe_particles(FrameChunk& chunk, std::size_t fifo_size) {
    if (producer_stripe_particles_.empty()) {
        return;
    }
    // Marks past this submission (staged gather bytes) move to the next one.
    std::size_t kept = 0u;
    for (const StripeParticleMark& mark : producer_stripe_particles_) {
        if (mark.offset < fifo_size) {
            chunk.stripe_particles.push_back(mark);
        } else {
            StripeParticleMark rest = mark;
            rest.offset -= fifo_size;
            producer_stripe_particles_[kept++] = rest;
        }
    }
    producer_stripe_particles_.resize(kept);
}

void GxBackend::take_attached_shape_records(FrameChunk& chunk, std::size_t fifo_size) {
    if (producer_shape_records_.empty()) {
        return;
    }
    // A record at the submission's end runs before the next one's commands; later ones move to the next submission.
    std::size_t kept = 0u;
    for (shapes::Record& record : producer_shape_records_) {
        if (record.offset <= fifo_size) {
            chunk.shape_records.push_back(std::move(record));
        } else {
            record.offset -= fifo_size;
            producer_shape_records_[kept++] = std::move(record);
        }
    }
    producer_shape_records_.resize(kept);
}

void GxBackend::note_stripe_particles(const FrameChunk& chunk) {
    motion_stripe_marks_ += chunk.stripe_particles.size();
}

void GxBackend::take_attached_fifo_markers(FrameChunk& chunk, std::size_t fifo_size) noexcept {
    chunk.fifo_marker_count = 0;
    chunk.fifo_marker_view_mask = 0;
    chunk.fifo_marker_sky_up_mask = 0;
    for (std::uint8_t i = 0; i < producer_fifo_marker_count_; ++i) {
        if (producer_fifo_markers_[i].first <= fifo_size &&
            chunk.fifo_marker_count < chunk.fifo_markers.size()) {
            const std::uint8_t index = chunk.fifo_marker_count++;
            chunk.fifo_markers[index] = producer_fifo_markers_[i];
            chunk.fifo_marker_serials[index] = producer_fifo_marker_serials_[i];
            if ((producer_fifo_marker_view_mask_ & (1u << i)) != 0u) {
                chunk.fifo_marker_views[index] = producer_fifo_marker_views_[i];
                chunk.fifo_marker_view_mask |= static_cast<std::uint8_t>(1u << index);
            }
            if ((producer_fifo_marker_sky_up_mask_ & (1u << i)) != 0u) {
                chunk.fifo_marker_sky_up[index] = producer_fifo_marker_sky_up_[i];
                chunk.fifo_marker_sky_up_mask |= static_cast<std::uint8_t>(1u << index);
            }
            chunk.fifo_marker_stages[index] = producer_fifo_marker_stages_[i];
        }
    }
    producer_fifo_marker_count_ = 0;
    producer_fifo_marker_view_mask_ = 0;
    producer_fifo_marker_sky_up_mask_ = 0;
}

// A fresh InterpScene for the frame being recorded (its mirror pass or its 3D
// scene, whichever comes first).
void GxBackend::open_interp_scene() {
    note_interp_log_completion();
    interp_scene_ = (interp_scene_ + 1u) % 4u;
    if (interp_shown_scene_ == static_cast<std::int32_t>(interp_scene_)) {
        interp_shown_scene_ = -1;  // its inputs are about to be reused
    }
    if (interp_sequence_.active &&
        (interp_scene_ == interp_sequence_.scene ||
         interp_scene_ == interp_sequence_.before)) {
        // The playing scene's inputs are about to be reused.
        interp_dropped_ += interp_sequence_.subframes - interp_sequence_.next;
        interp_sequence_.subframes = interp_sequence_.next;
        interp_sequence_.flush_requested = true;
    }
    if (interp_scene_ == interp_log_scene_) {
        interp_log_waiting_ = false;
        interp_start_pending_ = false;
    }
    interp_scenes_[interp_scene_].clear();
    interp_scenes_[interp_scene_].valid = true;
}

// The matches of a layer that ended index the previous frame's same layer:
// they become indices of the scene before, whose layer that is when the two
// are consecutive.
void GxBackend::end_record_layer(unsigned layer) {
    InterpScene& scene = interp_scenes_[interp_scene_];
    InterpScene::LayerRange& range = scene.layers[layer];
    range.count = scene.constants.size() - range.first;
    range.serial = ++layer_serials_[layer];
    const InterpScene& before = interp_scenes_[(interp_scene_ + 3u) % 4u];
    const InterpScene::LayerRange& was = before.layers[layer];
    const bool consecutive = before.valid && was.serial != 0u && was.serial + 1u == range.serial;
    layer_records_[layer] += range.count;
    for (std::size_t r = range.first; r < scene.constants.size(); ++r) {
        std::int32_t& m = scene.prev_index[r];
        layer_unmapped_[layer] += !consecutive && m >= 0 ? 1u : 0u;
        m = consecutive && m >= 0 && static_cast<std::size_t>(m) < was.count
            ? static_cast<std::int32_t>(was.first + static_cast<std::size_t>(m)) : -1;
    }
}

// A side layer of an interpolated frame: the screen layer after its 3D scene
// (to the display copy) or the mirror pass before it.
void GxBackend::begin_side_layer(unsigned layer) {
    if (!interp_recording_ || history_scene_) {
        return;
    }
    if (layer == kScreenLayer) {
        if (!scene_3d_recorded_ || !interp_scenes_[interp_scene_].valid) {
            return;
        }
        scene_3d_recorded_ = false;
    } else {
        flush_pending_draw_batch();
        open_interp_scene();
        mirror_opened_scene_ = true;
    }
    flush_pending_draw_batch();
    InterpScene& scene = interp_scenes_[interp_scene_];
    side_layer_ = layer;
    history_scene_ = true;
    frame_bindings_dirty_ = true;
    scene.layers[layer].first = scene.constants.size();
    TransformHistory& history = layer == kScreenLayer ? screen_history_ : mirror_history_;
    if (history.current().capacity() == 0u) {
        history.reserve(2048u);
    }
    history.begin_scene(renderer_.matrix_segment_serial(), nullptr);
    ++identity_scene_index_;
    identity_scene_ = layer_identity_[layer];
    layer_identity_markers_[layer] = 0u;
    identity_occurrences_used_ = 0u;
    identity_marker_ = mix64(0x5343524545ull);
    identity_matrix_base_ = state_.matrix_write_count();
    identity_last_base_ = 0u;
    identity_sub_index_ = 0u;
    identity_continuity_ = 0u;
    identity_continuity_particle_ = false;
    identity_category_ = motion::kNoCategory;
    state_.reset_matrix_load_signature();
    state_.set_matrix_load_tracking(true);
    matrix_palette_stale_ = true;
    current_palette_shadow_ = -1;
    current_replay_record_ = -1;
    current_ps_record_ = -1;
    ps_record_restart_ = true;
}

void GxBackend::end_side_layer() {
    if (side_layer_ == 0u) {
        return;
    }
    flush_pending_draw_batch();
    const unsigned layer = side_layer_;
    (layer == kScreenLayer ? screen_history_ : mirror_history_).end_scene();
    end_record_layer(layer);
    layer_identity_[layer] = layer_identity_markers_[layer] != 0u;
    side_layer_ = 0u;
    history_scene_ = false;
    frame_bindings_dirty_ = true;
    current_replay_record_ = -1;
    current_ps_record_ = -1;
    state_.set_matrix_load_tracking(false);
}

// A frame of a run that draws no 3D scene, from its first draw: an InterpScene whose whole picture is its screen
// layer (no camera), matched with the frame before's by the layouts' identities, and a renderer log that counts as
// the frame's scene.
void GxBackend::begin_screen_frame() {
    flush_pending_draw_batch();
    open_interp_scene();
    renderer_.note_replay_screen_frame();
    screen_frame_open_ = true;
    // begin_side_layer opens a screen layer after a recorded scene: this frame's record stands for one.
    scene_3d_recorded_ = true;
    begin_side_layer(kScreenLayer);
}

// The screen-only record of a frame that drew a 3D scene after all: dropped (no sequence plays from it; the scene's
// record follows it in the ring).
void GxBackend::cancel_screen_frame() {
    end_side_layer();
    interp_scenes_[interp_scene_].valid = false;
    renderer_.cancel_replay_screen_frame();
    screen_frame_open_ = false;
}

bool GxBackend::rt_geometry_draw() const noexcept {
    if (!(rt_state_eligible_ || rt_card_draw() || rt_alpha_draw()) || !scene_phase_ || side_layer_ != kSceneLayer ||
        !renderer_.rt_scene_open()) {
        return false;
    }
    using motion::ScenePhase;
    return rt_phase_ == static_cast<std::uint8_t>(ScenePhase::OpaBeforeVolumeShadow) ||
           rt_phase_ == static_cast<std::uint8_t>(ScenePhase::OpaBeforeSilhouette) ||
           rt_phase_ == static_cast<std::uint8_t>(ScenePhase::Opa) ||
           rt_phase_ == static_cast<std::uint8_t>(ScenePhase::ListOpa);
}

// Late geometry: opaque draws after the screen capture (indirect-textured models), after the trace.
bool GxBackend::rt_late_draw() const noexcept {
    static_assert(static_cast<std::uint8_t>(motion::ScenePhase::AfterIndirect) == kRtLatePhase);
    // Alpha-tested and card draws of this phase are traced too, as in the other phases (rt_alpha_draw).
    return (rt_state_eligible_ || rt_alpha_draw() || rt_card_draw()) && scene_phase_ && side_layer_ == kSceneLayer &&
           renderer_.rt_late_open() && rt_phase_ == static_cast<std::uint8_t>(motion::ScenePhase::AfterIndirect);
}

unsigned GxBackend::rt_texgen_of_map(unsigned map) const noexcept {
    for (unsigned s = 0; s < ps_key_.num_tev_stages && s < 16u; ++s) {
        const std::uint32_t order = ps_key_.stages[s].order;   // texmap 0-2, texcoord 3-5, enable 6
        if ((order & (1u << 6u)) != 0u && (order & 7u) == map) {
            const unsigned coord = (order >> 3u) & 7u;
            return coord < vs_key_.num_texgens ? coord : 0xFFu;
        }
    }
    return 0xFFu;
}

void GxBackend::begin_rt_sky_batch(RtDrawInfo& info) {
    info = RtDrawInfo{};
    info.surface = kRtDrawSky;
    info.phase = rt_phase_;
    info.channel = state_.xf(xf::kChannelCtrlBase);
    info.ambient = state_.xf(xf::kAmbientColorBase);
    info.material = state_.xf(xf::kMaterialColorBase);
    // The first TEV stage's texture (as the hits' material texture).
    for (unsigned s = 0; s < ps_key_.num_tev_stages && s < 16u; ++s) {
        const std::uint32_t order = ps_key_.stages[s].order;
        if ((order & (1u << 6u)) == 0u) {
            continue;
        }
        const unsigned map = order & 7u;
        if (current_texture_table_.ptr != 0u && (current_texture_map_mask_ & (1u << map)) != 0u) {
            info.texture = rt_texture_source(map);
            info.texgen = rt_texgen_of_map(map);
        }
        break;
    }
    // Its alpha test (as alpha-tested geometry: the first GREATER or GEQUAL compare).
    const AlphaCompare test = state_.alpha_compare();
    if (test.comp0 == CompareFunc::Greater || test.comp0 == CompareFunc::GEqual) {
        info.alpha_from = test.ref0 + (test.comp0 == CompareFunc::Greater ? 1u : 0u);
    } else if (test.comp1 == CompareFunc::Greater || test.comp1 == CompareFunc::GEqual) {
        info.alpha_from = test.ref1 + (test.comp1 == CompareFunc::Greater ? 1u : 0u);
    }
    info.alpha_from = std::min(info.alpha_from, 255u);
    const BlendMode blend = state_.blend_mode();
    info.blend = (blend.blend_enable ? 1u : 0u) | (static_cast<std::uint32_t>(blend.src_factor) << 1u) |
                 (static_cast<std::uint32_t>(blend.dst_factor) << 4u) | (blend.subtract ? 0x80u : 0u) |
                 (blend.color_update ? 0x100u : 0u) | (blend.logic_op_enable ? 0x200u : 0u);
    fill_rt_tev(info, 1.0f);
}

std::uint32_t GxBackend::debake_channels() const {
    static const bool enabled = read_env_u64("GALAXY_RT_DEBAKE", 1u) != 0u;
    using motion::ScenePhase;
    if (!enabled || (enhanced_ps_flags_ & kEnhancedRtPhysical) == 0u || rt_sky_buffer_ || !scene_phase_ ||
        side_layer_ != kSceneLayer || state_.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 6u)) != 0u ||
        !(rt_phase_ == static_cast<std::uint8_t>(ScenePhase::OpaBeforeVolumeShadow) ||
          rt_phase_ == static_cast<std::uint8_t>(ScenePhase::OpaBeforeSilhouette) ||
          rt_phase_ == static_cast<std::uint8_t>(ScenePhase::Opa) ||
          rt_phase_ == static_cast<std::uint8_t>(ScenePhase::ListOpa))) {
        return 0u;
    }
    if (const RtMaterial* const material = rt_material(current_rt_material_);
        material != nullptr && material->emission != RtEmission::None) {
        return 0u;
    }
    // Unlit channels coloured by their vertices (bits 0-1); lit channels (bits 2-3; none with GALAXY_RT_DEBAKE_LIT=0).
    // Vertex colours hold baked light on the terrain only (MR::DrawBufferType ClippedMapParts 0, Planet 4, PlanetLow 5,
    // Environment 6-7, IndirectPlanet 0x1D, and the draw types' lists): the game lights its actors as they move, and
    // map objects paint their vertices with their own colours, lit channels included, so neither is de-baked.
    static const bool lit_enabled = read_env_u64("GALAXY_RT_DEBAKE_LIT", 1u) != 0u;
    const std::uint32_t buffer = rt_draw_buffer_ & 0xFFFFu;
    const bool terrain = buffer == 0xFFFFu || buffer == 0x00u || buffer == 0x04u || buffer == 0x05u ||
                         buffer == 0x06u || buffer == 0x07u || buffer == 0x1Du;
    const bool actor = !terrain;
    std::uint32_t channels = 0u;
    const unsigned count = std::min<unsigned>(state_.gen_mode().num_color_chans, 2u);
    for (unsigned c = 0; c < count; ++c) {
        const XfChannelCtrl ctrl = state_.channel_ctrl(c);
        if (!ctrl.lighting_enable && ctrl.material_from_vertex && !actor) {
            channels |= 1u << c;
        } else if (ctrl.lighting_enable && lit_enabled && !(actor && ctrl.material_from_vertex)) {
            channels |= 4u << c;
        }
    }
    return channels;
}

void GxBackend::build_rt_tev(RtTevMaterial& tev, RtTevStage* stages_out, float facing) const {
    tev = RtTevMaterial{};
    const unsigned stages = std::min<unsigned>(ps_key_.num_tev_stages, kRtTevMaxStages);
    const unsigned texgens = std::min<unsigned>(vs_key_.num_texgens, 8u);
    tev.header = stages | (texgens << 28u);
    const auto scales = coordinate_scales(state_, scales_from_textures_);
    for (unsigned s = 0; s < stages; ++s) {
        const TevStagePacked& packed = ps_key_.stages[s];
        RtTevStage& stage = stages_out[s];
        stage.color_env = packed.color_env;
        stage.alpha_env = packed.alpha_env;
        stage.order = packed.order;
        stage.ksel = packed.ksel;
        // The raster's coordinate scale: the texture coordinate's GX scale over its map's size.
        const unsigned map = packed.order & 7u;
        const unsigned coord = (packed.order >> 3u) & 7u;
        const TexImage image = state_.tex_image(map);
        stage.scale_s = static_cast<float>(scales[coord][0]) / static_cast<float>(std::max<unsigned>(image.width, 1u));
        stage.scale_t = static_cast<float>(scales[coord][1]) / static_cast<float>(std::max<unsigned>(image.height, 1u));
        const TexMode tex_mode = state_.tex_mode(map);
        stage.pad[1] = static_cast<std::uint32_t>(tex_mode.wrap_s) | (static_cast<std::uint32_t>(tex_mode.wrap_t) << 2u);
        // Its indirect command (the raster's: active with indirect stages and a nonzero command), and whether the
        // program has indirect stages at all (rt_tev.hlsli carries the coordinates from stage to stage then).
        if (ps_key_.num_ind_stages != 0u) {
            stage.pad[1] |= ((ps_key_.ind_cmd[s / 4u][s & 3u] & 0x1FFFFFu) << 8u) | (1u << 31u);
        }
        // A texture whose coordinates the vertex normals generate: an environment map, the game's painted reflection
        // (pad[0] 1; header bit 7: the draw has one). Under physical light the traced reflection takes its place;
        // GALAXY_RT_PAINTED_REFLECTION=1 keeps them.
        static const bool painted = read_env_u64("GALAXY_RT_PAINTED_REFLECTION", 0u) != 0u;
        stage.pad[0] = 0u;
        if (!painted && ((packed.order >> 6u) & 1u) != 0u && coord < texgens) {
            const XfTexGen texgen = decode_xf_texgen(vs_key_.texgen[coord]);
            if (texgen.type == TexGenType::Regular && texgen.source_row == 1u) {
                stage.pad[0] = 1u;
                tev.header |= 1u << 7u;
            }
        }
    }
    const AlphaCompare test = state_.alpha_compare();
    tev.alpha_test = static_cast<std::uint32_t>(test.ref0) | (static_cast<std::uint32_t>(test.ref1) << 8u) |
                     (static_cast<std::uint32_t>(test.comp0) << 16u) | (static_cast<std::uint32_t>(test.comp1) << 19u) |
                     (static_cast<std::uint32_t>(test.logic) << 22u);
    // Colour channels 0 and 1 (colour and alpha controls 0-3): the material register or the vertices' colour, lit at
    // `facing` of every light in its mask over its ambient register, clamped as GX's channel.
    const unsigned channels = std::min<unsigned>(state_.gen_mode().num_color_chans, 2u);
    for (unsigned c = 0; c < 2u; ++c) {
        if (c >= channels) {
            tev.channel[c * 2u] = 0u;
            tev.channel[c * 2u + 1u] = 0xFFFFFFFFu;
            continue;
        }
        const XfChannelCtrl color = state_.channel_ctrl(c);
        const XfChannelCtrl alpha = state_.channel_ctrl(2u + c);
        tev.channel[c * 2u] = state_.xf(static_cast<std::uint16_t>(xf::kMaterialColorBase + c));
        tev.header |= (color.material_from_vertex ? 1u : 0u) << (24u + 2u * c);
        tev.header |= (alpha.material_from_vertex ? 1u : 0u) << (25u + 2u * c);
        const std::uint32_t ambient = state_.xf(static_cast<std::uint16_t>(xf::kAmbientColorBase + c));
        float sums[4]{};
        for (unsigned k = 0; k < 4u; ++k) {
            const XfChannelCtrl& ctrl = k < 3u ? color : alpha;
            const unsigned shift = 24u - 8u * k;
            float sum = 1.0f;
            if (ctrl.lighting_enable) {
                sum = static_cast<float>((ambient >> shift) & 0xFFu) / 255.0f;
                for (unsigned n = 0; n < 8u; ++n) {
                    if ((ctrl.light_mask & (1u << n)) != 0u) {
                        const std::uint32_t lc = state_.xf(static_cast<std::uint16_t>(xf::kLightsBase + 16u * n + 3u));
                        sum += facing * static_cast<float>((lc >> shift) & 0xFFu) / 255.0f;
                    }
                }
            }
            sums[k] = sum;
        }
        // The player's light rig under physical light: its level, not its hue (rt_player_rig_draw).
        if (c == 0u && (enhanced_ps_flags_ & kEnhancedRtPhysical) != 0u && rt_player_rig_draw()) {
            const float level = 0.2126f * sums[0] + 0.7152f * sums[1] + 0.0722f * sums[2];
            sums[0] = sums[1] = sums[2] = level;
        }
        std::uint32_t light = 0u;
        for (unsigned k = 0; k < 4u; ++k) {
            light |= static_cast<std::uint32_t>(std::lround(std::clamp(sums[k], 0.0f, 1.0f) * 255.0f)) << (24u - 8u * k);
        }
        tev.channel[c * 2u + 1u] = light;
    }
    const auto konst = state_.tev_konst_colors();
    for (unsigned k = 0; k < 4u; ++k) {
        tev.konst[k] = konst[k];
    }
    const auto regs = state_.tev_register_values();
    const auto half = [](float v) {
        // IEEE half, round to nearest (registers are -1024..1023 over 255).
        const std::uint32_t bits = std::bit_cast<std::uint32_t>(v);
        const std::uint32_t sign = (bits >> 16u) & 0x8000u;
        const int exponent = static_cast<int>((bits >> 23u) & 0xFFu) - 127 + 15;
        if (exponent <= 0) {
            return sign;
        }
        if (exponent >= 31) {
            return sign | 0x7C00u;
        }
        std::uint32_t h = sign | (static_cast<std::uint32_t>(exponent) << 10u) | ((bits >> 13u) & 0x3FFu);
        if ((bits & 0x1000u) != 0u) {
            ++h;
        }
        return h;
    };
    for (unsigned r = 0; r < 4u; ++r) {
        tev.regs[r * 2u] = half(static_cast<float>(regs[r][0]) / 255.0f) |
                           (half(static_cast<float>(regs[r][1]) / 255.0f) << 16u);
        tev.regs[r * 2u + 1u] = half(static_cast<float>(regs[r][2]) / 255.0f) |
                                (half(static_cast<float>(regs[r][3]) / 255.0f) << 16u);
    }
}

// Paint or baked light (apply_debake_model), from a model's distinct vertex colours (at most 2048 sampled per batch):
// light baked over curved surfaces takes hundreds to thousands of values (39 or more in the game's models), shading
// painted by hand a handful of exact ones (2-12). Hue is no cue, as the baked light is often coloured. One flat colour
// is paint too. `hue_count` (saturated hues within 0.15 of chromaticity) is for the log.
bool debake_colours_baked(const std::unordered_set<std::uint32_t>& colours, unsigned& hue_count, float& lo, float& hi) {
    std::vector<std::array<float, 3>> hues;
    lo = 1.0f;
    hi = 0.0f;
    for (const std::uint32_t c : colours) {
        const float r = static_cast<float>((c >> 16u) & 0xFFu) / 255.0f;
        const float g = static_cast<float>((c >> 8u) & 0xFFu) / 255.0f;
        const float b = static_cast<float>(c & 0xFFu) / 255.0f;
        const float peak = std::max({r, g, b});
        const float least = std::min({r, g, b});
        lo = std::min(lo, peak);
        hi = std::max(hi, peak);
        // Saturated (a paint's hue, not a light's tint) and bright enough for its hue to be known.
        if (peak < 0.2f || 1.0f - least / peak < 0.35f) {
            continue;
        }
        const std::array<float, 3> hue{r / peak, g / peak, b / peak};
        const bool known = std::any_of(hues.begin(), hues.end(), [&hue](const std::array<float, 3>& h) {
            return std::abs(h[0] - hue[0]) < 0.15f && std::abs(h[1] - hue[1]) < 0.15f && std::abs(h[2] - hue[2]) < 0.15f;
        });
        if (!known) {
            hues.push_back(hue);
        }
    }
    hue_count = static_cast<unsigned>(hues.size());
    return colours.size() > 16u && hi - lo >= 0.08f;
}

// The nine real spherical harmonics (bands 0-2) at unit direction `n` (Sloan 2008, the same basis as rt_lighting.hlsl
// sh9 and the de-baking shaders).
void debake_sh9(const float* n, float* y) {
    y[0] = 0.282095f;
    y[1] = 0.488603f * n[1];
    y[2] = 0.488603f * n[2];
    y[3] = 0.488603f * n[0];
    y[4] = 1.092548f * n[0] * n[1];
    y[5] = 1.092548f * n[1] * n[2];
    y[6] = 0.315392f * (3.0f * n[2] * n[2] - 1.0f);
    y[7] = 1.092548f * n[0] * n[2];
    y[8] = 0.546274f * (n[0] * n[0] - n[1] * n[1]);
}

// The light baked into a model's vertex colours as a function of the surface's facing: per channel the least-squares
// fit of nine spherical harmonics of the vertex normal to the colour (Ramamoorthi & Hanrahan 2001: a distant light's
// irradiance lies almost wholly in these bands), solved by Cholesky with a small ridge (1e-3 of the mean diagonal).
// What the facing does not explain stays: paint, occlusion.
void GxBackend::fit_debake_light(DebakeModel& model) {
    model.fitted = false;
    if (model.samples.size() < 32u) {
        return;
    }
    double a[9][9]{};
    double b[3][9]{};
    double sum_y = 0.0;
    double sum_y2 = 0.0;
    for (const std::array<float, 6>& s : model.samples) {
        float y[9];
        debake_sh9(s.data(), y);
        for (unsigned i = 0; i < 9u; ++i) {
            for (unsigned j = 0; j <= i; ++j) {
                a[i][j] += static_cast<double>(y[i]) * y[j];
            }
            for (unsigned c = 0; c < 3u; ++c) {
                b[c][i] += static_cast<double>(y[i]) * s[3u + c];
            }
        }
        const double lum = 0.2126 * s[3] + 0.7152 * s[4] + 0.0722 * s[5];
        sum_y += lum;
        sum_y2 += lum * lum;
    }
    double trace = 0.0;
    for (unsigned i = 0; i < 9u; ++i) {
        trace += a[i][i];
    }
    const double ridge = 1e-3 * trace / 9.0;
    double l[9][9]{};
    for (unsigned i = 0; i < 9u; ++i) {
        for (unsigned j = 0; j <= i; ++j) {
            double sum = a[i][j] + (i == j ? ridge : 0.0);
            for (unsigned k = 0; k < j; ++k) {
                sum -= l[i][k] * l[j][k];
            }
            if (i == j) {
                if (sum <= 0.0) {
                    return;
                }
                l[i][i] = std::sqrt(sum);
            } else {
                l[i][j] = sum / l[j][j];
            }
        }
    }
    for (unsigned c = 0; c < 3u; ++c) {
        double z[9];
        for (unsigned i = 0; i < 9u; ++i) {
            double sum = b[c][i];
            for (unsigned k = 0; k < i; ++k) {
                sum -= l[i][k] * z[k];
            }
            z[i] = sum / l[i][i];
        }
        for (int i = 8; i >= 0; --i) {
            double sum = z[i];
            for (unsigned k = static_cast<unsigned>(i) + 1u; k < 9u; ++k) {
                sum -= l[k][i] * z[k];
            }
            z[i] = sum / l[i][i];
            model.sh[c * 9u + static_cast<unsigned>(i)] = static_cast<float>(z[i]);
        }
    }
    // The most lit sampled facing, and how much of the luminance's variance the facing explains.
    model.peak = {0.0f, 0.0f, 0.0f};
    double sse = 0.0;
    for (const std::array<float, 6>& s : model.samples) {
        float y[9];
        debake_sh9(s.data(), y);
        float fit[3]{};
        for (unsigned c = 0; c < 3u; ++c) {
            for (unsigned i = 0; i < 9u; ++i) {
                fit[c] += model.sh[c * 9u + i] * y[i];
            }
            model.peak[c] = std::max(model.peak[c], fit[c]);
        }
        const double lum = 0.2126 * s[3] + 0.7152 * s[4] + 0.0722 * s[5];
        const double fitted = 0.2126 * fit[0] + 0.7152 * fit[1] + 0.0722 * fit[2];
        sse += (lum - fitted) * (lum - fitted);
    }
    const double n = static_cast<double>(model.samples.size());
    const double sst = sum_y2 - sum_y * sum_y / n;
    model.fit_r2 = sst > 1e-9 ? static_cast<float>(std::max(0.0, 1.0 - sse / sst)) : 0.0f;
    model.fitted = true;
}

std::uint32_t GxBackend::apply_debake_model(std::uint32_t channels) {
    debake_model_level_ = 0u;
    debake_fit_active_ = false;
    if ((channels & 3u) == 0u) {
        return channels;
    }
    // Indexed colours only (a model's arrays: a record's keys, else the GX array registers; the descriptor's VCD
    // part is the same for every format).
    std::uint64_t key = 0u;
    if (record_inputs_ != nullptr) {
        if (!record_inputs_->colored || record_inputs_->own_positions) {
            return channels;
        }
        key = record_inputs_->colors;
    } else {
        const VertexDescriptor desc = state_.vertex_desc(0u);
        if (desc.color[0].vcd != VcdType::Index8 && desc.color[0].vcd != VcdType::Index16) {
            return channels;
        }
        key = static_cast<std::uint64_t>(state_.array_base(2u)) |
              (static_cast<std::uint64_t>(state_.array_base(3u)) << 32u);
    }
    if (const std::uint32_t stage = rt_materials_stage_serial(); stage != debake_models_stage_) {
        debake_models_.clear();
        debake_models_stage_ = stage;
    }
    DebakeModel& model = debake_models_[key];
    if (!model.decided) {
        if (model.frame == 0u) {
            model.frame = frame_index_ + 1u;
        }
        if (frame_index_ + 1u <= model.frame || model.vertices == 0u) {
            // Its first frame: drawn as the game paints it while its colours are sampled.
            debake_sample_key_ = key;
            return channels & ~3u;
        }
        unsigned hues = 0u;
        float lo = 0.0f;
        float hi = 0.0f;
        model.baked = debake_colours_baked(model.colours, hues, lo, hi);
        const std::size_t sampled = model.samples.size();
        fit_debake_light(model);
        model.level = static_cast<std::uint8_t>(std::lround(std::clamp(hi, 0.0f, 1.0f) * 255.0f));
        model.decided = true;
        static const bool log = read_env_u64("GALAXY_RT_DEBAKE_LOG", 0u) != 0u;
        if (log) {
            std::string sample;
            unsigned shown = 0u;
            for (const std::uint32_t c : model.colours) {
                if (shown++ == 12u) {
                    break;
                }
                char text[10];
                std::snprintf(text, sizeof(text), " %06X", c);
                sample += text;
            }
            std::fprintf(stderr,
                         "[rt-debake] model=%08X:%08X buffer=%u vertices=%u colours=%zu hues=%u value=%.2f-%.2f %s "
                         "normals=%zu fit=%d r2=%.2f peak=%.2f,%.2f,%.2f%s\n",
                         static_cast<unsigned>(key & 0xFFFFFFFFu), static_cast<unsigned>(key >> 32u),
                         rt_draw_buffer_ & 0xFFFFu, model.vertices, model.colours.size(), hues, lo, hi,
                         model.baked ? "baked" : "paint", sampled, model.fitted ? 1 : 0, model.fit_r2, model.peak[0],
                         model.peak[1], model.peak[2], sample.c_str());
        }
        model.colours = {};
        model.samples = {};
        if (model.baked) {
            debake_model_order_.push_back(key);
        }
    }
    // Its light as a function of the facing (its vertices have normals): the shaders take that out alone, whatever
    // its colours count (GALAXY_RT_DEBAKE_FIT=0: the colours' verdict and each colour over its brightest channel).
    static const bool by_fit = read_env_u64("GALAXY_RT_DEBAKE_FIT", 1u) != 0u;
    if (by_fit && model.fitted) {
        for (unsigned i = 0; i < 27u; ++i) {
            debake_fit_sh_[i / 4u][i % 4u] = model.sh[i];
        }
        debake_fit_sh_[6][3] = 0.0f;
        debake_fit_peak_[0] = model.peak[0];
        debake_fit_peak_[1] = model.peak[1];
        debake_fit_peak_[2] = model.peak[2];
        debake_fit_peak_[3] = 0.0f;
        debake_fit_active_ = true;
        return channels;
    }
    if (!model.baked) {
        return channels & ~3u;
    }
    // GALAXY_RT_DEBAKE_CYCLE=1 (diagnostics): one baked model de-baked at a time, the next every 120 frames, to find a
    // model in the picture.
    static const bool cycle = read_env_u64("GALAXY_RT_DEBAKE_CYCLE", 0u) != 0u;
    if (cycle && !debake_model_order_.empty()) {
        const std::size_t turn = static_cast<std::size_t>((frame_index_ / 120u) % debake_model_order_.size());
        static std::size_t logged_turn = ~std::size_t{0};
        if (turn != logged_turn) {
            logged_turn = turn;
            std::fprintf(stderr, "[rt-debake-cycle] frame=%llu turn=%zu model=%08X:%08X\n",
                         static_cast<unsigned long long>(frame_index_), turn,
                         static_cast<unsigned>(debake_model_order_[turn] & 0xFFFFFFFFu),
                         static_cast<unsigned>(debake_model_order_[turn] >> 32u));
        }
        if (debake_model_order_[turn] != key) {
            return channels & ~3u;
        }
    }
    // GALAXY_RT_DEBAKE_LEVEL=1: de-baked to the model's brightest vertex; else to the program's neutral level.
    static const bool brightest = read_env_u64("GALAXY_RT_DEBAKE_LEVEL", 0u) != 0u;
    debake_model_level_ = brightest ? model.level : 0u;
    return channels;
}

void GxBackend::sample_debake_model(std::uint64_t key, const DrawCall& call) {
    const auto found = debake_models_.find(key);
    if (found == debake_models_.end() || found->second.decided) {
        return;
    }
    DebakeModel& model = found->second;
    // The batch's decoded vertices: the frame ring or the resident buffer (both CPU-mapped upload memory, read
    // sparsely: at most 2048 vertices a batch).
    const gpu::Address va = call.vertex_view.BufferLocation;
    const std::uint32_t bytes = call.vertex_view.SizeInBytes;
    const std::byte* vertices = nullptr;
    const std::uint64_t resident_bytes =
        static_cast<std::uint64_t>(kResidentDecodedVertexCapacity) * sizeof(GxVertexOut);
    if (resident_vertex_mapped_ != nullptr && va >= resident_vertex_gpu_ && va + bytes <= resident_vertex_gpu_ + resident_bytes) {
        vertices = resident_vertex_mapped_ + (va - resident_vertex_gpu_);
    } else {
        vertices = renderer_.vertex_ring().cpu_at(va);
    }
    if (vertices == nullptr) {
        return;
    }
    const std::uint32_t count = bytes / static_cast<std::uint32_t>(sizeof(GxVertexOut));
    const std::uint32_t step = std::max(count / 2048u, 1u);
    for (std::uint32_t v = 0u; v < count && model.colours.size() < kDebakeModelColours; v += step) {
        std::uint32_t colour = 0u;
        std::memcpy(&colour, vertices + static_cast<std::size_t>(v) * sizeof(GxVertexOut) + offsetof(GxVertexOut, color0),
                    sizeof(colour));
        model.colours.insert(colour >> 8u);   // RGB (R in the high byte)
        if (model.samples.size() < kDebakeModelSamples) {
            float normal[3];
            std::memcpy(normal, vertices + static_cast<std::size_t>(v) * sizeof(GxVertexOut) + offsetof(GxVertexOut, normal),
                        sizeof(normal));
            const float length2 = normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2];
            if (std::isfinite(length2) && length2 > 0.25f) {
                const float inv = 1.0f / std::sqrt(length2);
                model.samples.push_back({normal[0] * inv, normal[1] * inv, normal[2] * inv,
                                         static_cast<float>((colour >> 24u) & 0xFFu) / 255.0f,
                                         static_cast<float>((colour >> 16u) & 0xFFu) / 255.0f,
                                         static_cast<float>((colour >> 8u) & 0xFFu) / 255.0f});
            }
        }
    }
    model.vertices += count;
}

std::uint32_t GxBackend::debake_state() {
    const std::uint32_t channels = apply_debake_model(debake_channels());
    if (channels == 0u) {
        return 0u;
    }
    // The program's neutral light per de-baked channel (rt_tev_neutral_light), by the TEV state it depends on.
    RtTevMaterial tev{};
    std::array<RtTevStage, kRtTevMaxStages> stages{};
    build_rt_tev(tev, stages.data(), 1.0f);
    std::uint64_t key = combine64(channels, tev.header);
    const auto words = [&key](const std::uint32_t* w, std::size_t n) {
        for (std::size_t i = 0; i < n; ++i) {
            key = combine64(key, w[i]);
        }
    };
    words(&tev.alpha_test, 1u + 4u + 4u + 8u);
    for (unsigned s = 0; s < (tev.header & 0x1Fu); ++s) {
        words(&stages[s].color_env, 4u);
    }
    // A baked model's unlit channels: every vertex as bright as its brightest (the surface as the game shows it fully
    // lit, the programs' doubling and clamping included), not the program's neutral level, which shows the texture
    // 1:1 at about half the game's lit brightness.
    const auto model_level = [this](std::uint32_t state) {
        if (debake_model_level_ != 0u) {
            for (unsigned c = 0; c < 2u; ++c) {
                if ((state & (1u << c)) != 0u) {
                    state = (state & ~(0xFFu << (8u + 8u * c))) |
                            (static_cast<std::uint32_t>(debake_model_level_) << (8u + 8u * c));
                }
            }
        }
        return state;
    };
    const auto found = debake_levels_.find(key);
    if (found != debake_levels_.end()) {
        return model_level(found->second);
    }
    std::uint32_t state = channels;
    for (unsigned c = 0; c < 2u; ++c) {
        if ((channels & (5u << c)) != 0u) {
            state |= rt_tev_neutral_light(tev, stages.data(), c, (channels & (4u << c)) != 0u) << (8u + 8u * c);
        }
    }
    if (debake_levels_.size() > 65536u) {
        debake_levels_.clear();
    }
    debake_levels_.emplace(key, state);
    return model_level(state);
}

void GxBackend::fill_rt_tev(RtDrawInfo& info, float facing) {
    RtTevMaterial& tev = info.tev;
    build_rt_tev(tev, info.tev_stages.data(), facing);
    const unsigned stages = tev.header & 0x1Fu;
    const unsigned texgens = tev.header >> 28u;
    // The two texture slots: the material texture's coordinates, then the next ones a stage reads (the raster's rule:
    // coordinates past the draw's generators read generator 0).
    unsigned slot0 = info.texgen < texgens ? info.texgen : 0xFFu;
    unsigned slot1 = 0xFFu;
    for (unsigned s = 0; s < stages; ++s) {
        const std::uint32_t order = ps_key_.stages[s].order;
        if ((order & (1u << 6u)) == 0u || texgens == 0u) {
            continue;
        }
        const unsigned coord = ((order >> 3u) & 7u) < texgens ? (order >> 3u) & 7u : 0u;
        if (slot0 == 0xFFu) {
            slot0 = coord;
        } else if (coord != slot0 && slot1 == 0xFFu) {
            slot1 = coord;
        }
    }
    // Indirect texturing (RtTevIndirect): its stages' maps and coordinates, their scales and matrices as the raster's
    // (ShaderGenerator, fill_ps_constants); an indirect map's coordinates take a free slot.
    info.tev_indirect = {};
    const unsigned ind_stages = std::min<unsigned>(ps_key_.num_ind_stages, 4u);
    if (ind_stages != 0u) {
        RtTevIndirect& ind = info.tev_indirect;
        ind.order = (ps_key_.ind_ref & 0xFFFFFFu) | (ind_stages << 24u);
        const auto coord_size = [this](unsigned coord, unsigned axis) {
            return coordinate_scales(state_, scales_from_textures_)[coord & 7u][axis];
        };
        for (unsigned i = 0; i < ind_stages; ++i) {
            const unsigned map = (ps_key_.ind_ref >> (6u * i)) & 7u;
            const unsigned coord_raw = (ps_key_.ind_ref >> (6u * i + 3u)) & 7u;
            const unsigned coord = coord_raw < texgens ? coord_raw : 0u;
            const std::uint32_t shifts = ps_key_.ind_scale[i / 2u] >> ((i & 1u) != 0u ? 8u : 0u);
            const TexImage image = state_.tex_image(map);
            ind.scale[i * 2u] = static_cast<float>(coord_size(coord, 0u)) /
                                (std::ldexp(1.0f, static_cast<int>(shifts & 0xFu)) *
                                 static_cast<float>(std::max<unsigned>(image.width, 1u)));
            ind.scale[i * 2u + 1u] = static_cast<float>(coord_size(coord, 1u)) /
                                     (std::ldexp(1.0f, static_cast<int>((shifts >> 4u) & 0xFu)) *
                                      static_cast<float>(std::max<unsigned>(image.height, 1u)));
            if (texgens != 0u && slot0 == 0xFFu) {
                slot0 = coord;
            } else if (texgens != 0u && coord != slot0 && slot1 == 0xFFu) {
                slot1 = coord;
            }
        }
        const auto sign11 = [](std::uint32_t value) {
            value &= 0x7FFu;
            return (value & 0x400u) != 0u ? static_cast<std::int32_t>(value | ~0x7FFu) : static_cast<std::int32_t>(value);
        };
        const auto pack = [](std::int32_t lo, std::int32_t hi) {
            return (static_cast<std::uint32_t>(lo) & 0xFFFFu) | (static_cast<std::uint32_t>(hi) << 16u);
        };
        for (unsigned m = 0; m < 3u; ++m) {
            const std::uint32_t a = state_.bp(static_cast<std::uint8_t>(bp::kIndMtxBase + m * 3u));
            const std::uint32_t b = state_.bp(static_cast<std::uint8_t>(bp::kIndMtxBase + m * 3u + 1u));
            const std::uint32_t c = state_.bp(static_cast<std::uint8_t>(bp::kIndMtxBase + m * 3u + 2u));
            const std::int32_t shift =
                17 - static_cast<std::int32_t>(((a >> 22u) & 3u) | (((b >> 22u) & 3u) << 2u) | (((c >> 22u) & 3u) << 4u));
            ind.matrix[m * 4u] = pack(sign11(a), sign11(b));
            ind.matrix[m * 4u + 1u] = pack(sign11(c), shift);
            ind.matrix[m * 4u + 2u] = pack(sign11(a >> 11u), sign11(b >> 11u));
            ind.matrix[m * 4u + 3u] = pack(sign11(c >> 11u), shift);
        }
        for (unsigned t = 0; t < 8u; ++t) {
            ind.coord_size[t] = (coord_size(t, 0u) - 1u) | ((coord_size(t, 1u) - 1u) << 16u);
        }
    }
    info.texgen = slot0;
    info.texgen2 = slot1;
    for (unsigned t = 0; t < 8u; ++t) {
        const unsigned coord = t < texgens ? t : 0u;
        tev.header |= (coord == slot1 ? 1u : 0u) << (8u + 2u * t);
    }
    // Bits 5-6: an unlit channel's vertex colour de-baked to its program's neutral light (pad[0] bits 0-15: the levels);
    // pad[0] bits 16-17: a lit channel lit at its neutral light, its vertex colour's hue its material. As the raster's
    // (debake_state; never the sky's).
    // The painted reflection (environment-map stages, header bit 7) comes out only on metal, whose traced reflection
    // takes its place; crystal, glass, glossy and unlit surfaces keep it as their look (their brilliance, sheen).
    if (((info.surface & kRtDrawClassMask) >> kRtDrawClassShift) != static_cast<std::uint32_t>(RtSurfaceClass::Metal)) {
        tev.header &= ~(1u << 7u);
    }
    const std::uint32_t debake = (info.surface & kRtDrawSky) != 0u ? 0u : debake_state();
    // By the model's facing fit the traced triangles' colours come de-baked (ray_tracing.hlsl GeometryCS).
    tev.header |= debake_fit_active_ ? 0u : (debake & 3u) << 5u;
    tev.pad[0] = ((debake >> 8u) & 0xFFFFu) | (((debake >> 2u) & 3u) << 16u);
    // A lit channel at its neutral light in its full light's hue (as the raster's, shader_gen): the ambient register or
    // the vertices can hold the object's own colour.
    for (unsigned c = 0; c < 2u; ++c) {
        if ((debake & (4u << c)) != 0u) {
            const std::uint32_t level = (debake >> (8u + 8u * c)) & 0xFFu;
            const std::uint32_t light = tev.channel[c * 2u + 1u];
            const std::uint32_t rgb[3] = {light >> 24u, (light >> 16u) & 0xFFu, (light >> 8u) & 0xFFu};
            const std::uint32_t peak = std::max({rgb[0], rgb[1], rgb[2]});
            std::uint32_t word = 0xFFu;
            for (unsigned k = 0; k < 3u; ++k) {
                const std::uint32_t v = peak != 0u ? (rgb[k] * level + peak / 2u) / peak : level;
                word |= std::min(v, 255u) << (24u - 8u * k);
            }
            tev.channel[c * 2u + 1u] = word;
        }
    }
    info.tev_maps = {};
    if (current_texture_table_.ptr != 0u) {
        for (unsigned m = 0; m < kMaxTextureMaps && m < 8u; ++m) {
            if ((current_texture_map_mask_ & (1u << m)) != 0u) {
                info.tev_maps[m] = rt_texture_source(m);
            }
        }
    }
}

RtTextureSource GxBackend::rt_texture_source(unsigned map) const noexcept {
    const TextureHandle& handle = current_texture_handles_[map];
    if (handle.resource == nullptr) {
        return {};
    }
#if defined(NEBULA_GPU_VULKAN)
    return {};  // no ray tracing on Vulkan yet
#else
    D3D12_CPU_DESCRIPTOR_HANDLE view = texture_cache_.backend().srv_cpu_base();
    view.ptr += static_cast<SIZE_T>(handle.srv_index) * texture_cache_.backend().srv_descriptor_stride();
    return {static_cast<std::uintptr_t>(view.ptr), handle.resource, handle.from_efb_copy ? 0u : handle.pack,
            handle.width, handle.height, handle.mip_levels};
#endif
}

// An alpha-tested draw of the material table's foliage (grass tufts, flower carpets): traced as a card,
// whole (kRtDrawCard: the lighting counts its hits where the raster drew it). GALAXY_RT_CARDS=0 leaves
// cards out of the traced scene.
bool GxBackend::rt_card_draw() const noexcept {
    static const bool enabled = read_env_u64("GALAXY_RT_CARDS", 1u) != 0u;
    if (!enabled || !rt_card_eligible_ || current_rt_material_ == 0u) {
        return false;
    }
    const RtMaterial* const material = rt_material(current_rt_material_);
    return material != nullptr && material->foliage;
}

// An alpha-tested opaque draw outside the foliage table: most such tests cut little or nothing, so it is traced
// whole and the rays test its texture's alpha (kRtDrawAlphaTest). GALAXY_RT_ALPHA_GEOMETRY=0 leaves them out.
bool GxBackend::rt_alpha_draw() const noexcept {
    static const bool enabled = read_env_u64("GALAXY_RT_ALPHA_GEOMETRY", 1u) != 0u;
    if (!enabled || !rt_card_eligible_ || rt_card_draw()) {
        return false;
    }
    // A particle texture the table names a light source, drawn as opaque geometry, is that light's surface and is
    // traced; other particle textures and the sky are not.
    const RtMaterial* const material = current_rt_material_ != 0u ? rt_material(current_rt_material_) : nullptr;
    return material == nullptr || ((!material->particle || material->emission != RtEmission::None) && !material->sky &&
                                    !material->hide);
}

// Glows: additive draws of the translucent phases, after the trace and before the scene's end.
bool GxBackend::rt_glow_draw() const noexcept {
    if (!rt_glow_eligible_ || !scene_phase_ || side_layer_ != kSceneLayer || !renderer_.rt_glow_open()) {
        return false;
    }
    using motion::ScenePhase;
    return rt_phase_ == static_cast<std::uint8_t>(ScenePhase::Xlu) ||
           rt_phase_ == static_cast<std::uint8_t>(ScenePhase::ListXlu) ||
           rt_phase_ == static_cast<std::uint8_t>(ScenePhase::AfterIndirect);
}

// Light sprites: alpha-blended draws of the translucent phases whose texture the material table names as a
// light: captured with the glows, their light found from what they add to the picture.
bool GxBackend::rt_sprite_light_draw() const noexcept {
    if (!rt_sprite_eligible_ || !scene_phase_ || side_layer_ != kSceneLayer || !renderer_.rt_glow_open()) {
        return false;
    }
    using motion::ScenePhase;
    if (rt_phase_ != static_cast<std::uint8_t>(ScenePhase::Xlu) &&
        rt_phase_ != static_cast<std::uint8_t>(ScenePhase::ListXlu) &&
        rt_phase_ != static_cast<std::uint8_t>(ScenePhase::AfterIndirect)) {
        return false;
    }
    const RtMaterial* const material = rt_material(current_rt_material_);
    return material != nullptr && material->emission != RtEmission::None && !material->no_light;
}

// Water: alpha-blended draws of the translucent phases (seas, pools, lakes) before the image
// effects; begin_rt_batch keeps those of the water class (indirect-textured) and the clear ones
// (crystals, glass: environment-mapped or named).
bool GxBackend::rt_water_draw() const noexcept {
    if (!rt_water_wanted_ || !rt_water_eligible_ || !scene_phase_ || side_layer_ != kSceneLayer ||
        !renderer_.rt_water_open()) {
        return false;
    }
    // Particles are never a water surface: effects drawn with indexed positions (a waterfall's spray cloud, smoke,
    // sparkles, bubbles) had the water pass's body painted over them, washed out.
    if (const RtMaterial* const material = rt_material(current_rt_material_); material != nullptr && material->particle) {
        return false;
    }
    using motion::ScenePhase;
    return rt_phase_ == static_cast<std::uint8_t>(ScenePhase::Xlu) ||
           rt_phase_ == static_cast<std::uint8_t>(ScenePhase::ListXlu) ||
           rt_phase_ == static_cast<std::uint8_t>(ScenePhase::AfterIndirect);
}

// The game's procedural water surfaces (OceanBowl, OceanSphere, OceanRing): direct vertices drawn in the translucent
// phases with a material the table names water (particles have their own rows), alpha blended, or added from below
// while the camera is in the water. Water as the models' surfaces are (rt_water_draw needs indexed positions to keep
// particles out).
bool GxBackend::rt_ocean_water_draw() const noexcept {
    if (!rt_water_wanted_ || !scene_phase_ || side_layer_ != kSceneLayer || !renderer_.rt_water_open() ||
        draw_positions_indexed() ||
        state_.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 6u)) != 0u) {
        return false;
    }
    using motion::ScenePhase;
    if (rt_phase_ != static_cast<std::uint8_t>(ScenePhase::Xlu) &&
        rt_phase_ != static_cast<std::uint8_t>(ScenePhase::ListXlu) &&
        rt_phase_ != static_cast<std::uint8_t>(ScenePhase::AfterIndirect)) {
        return false;
    }
    const BlendMode blend = state_.blend_mode();
    if (!blend.blend_enable || blend.logic_op_enable || blend.subtract || !blend.color_update ||
        blend.src_factor != BlendFactor::SrcAlpha ||
        (blend.dst_factor != BlendFactor::InvSrcAlpha && blend.dst_factor != BlendFactor::One)) {
        return false;
    }
    const RtMaterial* const material = rt_material(current_rt_material_);
    return material != nullptr && material->surface_class == static_cast<std::uint8_t>(RtSurfaceClass::Water) &&
           !material->particle && !material->helper && !material->waterfall && material->emission == RtEmission::None;
}

// The material table entry of a draw: its first mapped texture's that has one. Lookup textures
// (environment, indirect and mask maps, shared by many models) decide only a draw that maps nothing
// else the table names. Falling water decides whatever comes before it: Beach Bowl's and the Twin Falls'
// waterfalls map a ripple map and an indirect warp ahead of their fall texture.
std::uint16_t GxBackend::rt_material_of(std::uint32_t map_mask,
                                        const std::array<TextureHandle, kMaxTextureMaps>& handles,
                                        unsigned& map) noexcept {
    for (unsigned m = 0; m < kMaxTextureMaps; ++m) {
        if ((map_mask & (1u << m)) == 0u || handles[m].rt_material == 0u) {
            continue;
        }
        const RtMaterial* const material = rt_material(handles[m].rt_material);
        if (material != nullptr && material->waterfall && !material->helper) {
            map = m;
            return handles[m].rt_material;
        }
    }
    std::uint16_t lookup = 0u;
    unsigned lookup_map = 0xFFu;
    unsigned first = 0xFFu;
    std::uint16_t metal_helper = 0u;
    for (unsigned m = 0; m < kMaxTextureMaps; ++m) {
        if ((map_mask & (1u << m)) == 0u) {
            continue;
        }
        if (first == 0xFFu && handles[m].resource != nullptr) {
            first = m;
        }
        if (handles[m].rt_material == 0u) {
            continue;
        }
        const RtMaterial* const material = rt_material(handles[m].rt_material);
        if (material == nullptr || !material->helper) {
            map = m;
            return handles[m].rt_material;
        }
        if (lookup == 0u) {
            lookup = handles[m].rt_material;
            lookup_map = m;
        }
        // A material of helpers alone (Spring Mario's spring: an indirect dummy, then its environment maps) takes
        // the helper the table names metal over a shared dummy before it.
        if (metal_helper == 0u && material != nullptr &&
            material->surface_class == static_cast<std::uint8_t>(RtSurfaceClass::Metal)) {
            metal_helper = handles[m].rt_material;
        }
    }
    // A helper (an environment or indirect map) is not the surface's colour: the first bound map is.
    map = first != 0xFFu ? first : lookup_map;
    return metal_helper != 0u ? metal_helper : lookup;
}

void GxBackend::begin_rt_stage() {
    renderer_.rt_reset_sky();
    rt_fake_shadow_cache_.clear();
    rt_world_held_ = {};
    note_rt_stage();
}

// The loaded stage and its row of the sun table: the game's own report, else guest memory as the original program
// lays it out (GameSystem's scene controller holds the name inline at +0x20).
void GxBackend::note_rt_stage() {
    constexpr std::uint32_t kGameSystemPointer = 0x806A1228u;
    const auto word = [this](std::uint32_t address, std::uint32_t& out) {
        const std::byte* p = frame_memory_ != nullptr ? resolve_guest_const_bytes(frame_memory_, address, 4u) : nullptr;
        if (p == nullptr) {
            return false;
        }
        std::memcpy(&out, p, 4u);
        out = byte_swap_u32(out);
        return true;
    };
    std::uint32_t system = 0u;
    std::uint32_t controller = 0u;
    char name[sizeof(SceneStage)]{};
    if (scene_stage_known_) {
        std::memcpy(name, scene_stage_.data(), sizeof(name));
    } else if (word(kGameSystemPointer, system) && system >= 0x80000000u && word(system + 0x24u, controller) &&
               controller >= 0x80000000u) {
        for (std::uint32_t i = 0; i + 1u < sizeof(name); ++i) {
            const std::byte* c = resolve_guest_const_bytes(frame_memory_, controller + 0x20u + i, 1u);
            const auto v = c != nullptr ? std::to_integer<unsigned char>(*c) : 0u;
            if (v <= 0x20u || v >= 0x7Fu) {
                break;
            }
            name[i] = static_cast<char>(v);
        }
    }
    rt_materials_set_stage(name);
    {
        std::uint64_t h = 1469598103934665603ull;
        for (const char* c = name; *c != 0; ++c) {
            h = (h ^ static_cast<unsigned char>(*c)) * 1099511628211ull;
        }
        rt_stage_hash_ = h;
    }
    const RtStageSun* const sun = name[0] != 0 ? rt_stage_sun(name) : nullptr;
    renderer_.rt_set_stage_sun(sun);
    if (trace_actor_lights_enabled()) [[unlikely]] {
        std::fprintf(stderr, "[rt-stage] name=%s sun=%s\n", name[0] != 0 ? name : "?",
                     sun == nullptr                             ? "no row"
                     : sun->source == RtStageSun::Source::Table ? "table"
                     : sun->source == RtStageSun::Source::None  ? "none"
                     : sun->source == RtStageSun::Source::Core  ? "core"
                     : sun->source == RtStageSun::Source::Moon  ? "moon"
                                                                : "game");
    }
}

// The game's own shadow drawers (blob and volume shadows: 影描画[...], 板状シャドウボリューム描画): under
// traced lighting every object casts its true shadow, so these are not drawn. Their names are read
// once per object (cached until the next stage).
bool GxBackend::rt_fake_shadow_object(std::uint32_t object) {
    if (object == 0u) {
        return false;
    }
    if (const auto it = rt_fake_shadow_cache_.find(object); it != rt_fake_shadow_cache_.end()) {
        return it->second;
    }
    char name[48]{};
    rt_object_name(name, sizeof(name));
    static constexpr char kShadow[] = "\x89\x65\x95\x60\x89\xE6";           // 影描画
    static constexpr char kPlateShadow[] = "\x94\xC2\x8F\xF3\x83\x56\x83\x83";  // 板状シャ...
    // The drop shadows only (sphere, oval, cylinder, box volumes, plates, water-surface circles): the line,
    // electric-rail and ring-beam volumes draw effects the traced light does not replace.
    static constexpr char kLine[] = "\x83\x89\x83\x43\x83\x93";       // ライン
    static constexpr char kRail[] = "\x93\x64\x8C\x82";               // 電撃
    static constexpr char kRing[] = "\x83\x8A\x83\x93\x83\x4F";       // リング
    const bool drawer = std::strncmp(name, kShadow, sizeof(kShadow) - 1u) == 0 ||
                        std::strncmp(name, kPlateShadow, sizeof(kPlateShadow) - 1u) == 0;
    const bool shadow = drawer && std::strstr(name, kLine) == nullptr && std::strstr(name, kRail) == nullptr &&
                        std::strstr(name, kRing) == nullptr;
    if (rt_fake_shadow_cache_.size() > 4096u) {
        rt_fake_shadow_cache_.clear();
    }
    rt_fake_shadow_cache_.emplace(object, shadow);
    return shadow;
}

bool GxBackend::rt_traced_clear_draw() const noexcept {
    // Glass joins the water window (its draws after the trace); a clear solid is the scene's own geometry, unseen
    // (drawn before the trace: the crystal cages).
    const RtMaterial* const clear = rt_material(current_rt_material_);
    return clear != nullptr && clear->traced && scene_phase_ && side_layer_ == kSceneLayer &&
           renderer_.rt_engine_clear() && (clear->solid ? renderer_.rt_scene_open() : renderer_.rt_water_open());
}

bool GxBackend::rt_traced_solid_draw() const noexcept {
    const RtMaterial* const clear = rt_material(current_rt_material_);
    return clear != nullptr && clear->solid && rt_traced_clear_draw();
}

bool GxBackend::rt_hidden_draw() const noexcept {
    // A trace-only draw is never drawn (rt_trace_only_: the game draws it only for the ray tracer).
    if (rt_trace_only_) {
        return true;
    }
    // GALAXY_RT_HIDE_MIRROR=1: the game's mirror pass (the world drawn from the mirrored camera into the
    // texture mirrors project) is not drawn under traced reflections. Off by default: mirrored draws made after
    // the reflection composite stay either way.
    static const bool hide_mirror = read_env_u64("GALAXY_RT_HIDE_MIRROR", 0u) != 0u;
    if (rt_mirror_pass_ && hide_mirror && renderer_.rt_reflections_active()) {
        return true;
    }
    if (!scene_phase_ || side_layer_ != kSceneLayer || !renderer_.rt_lighting()) {
        return false;
    }
    // A clear surface the engine traces (the table's `traced`): its reflection and what shows through are traced
    // (rt_engine.hlsl EngineClearCS); the game's draw paints a fake reflection over it.
    if (rt_traced_clear_draw()) {
        return true;
    }
    // A spray sprite of the table's `mist` effects: a puff of drops the engine lights (note_rt_mist), not the game's
    // painted cloud.
    if (rt_mist_draw()) {
        return true;
    }
    // The game's own shadows: its shadow lists by their draw type (every drawer the game files there: blob and volume
    // shadows of items, enemies and objects), and the drawers known by name.
    if (rt_fake_shadow_list_ || rt_object_fake_shadow_) {
        return true;
    }
    // The alpha-shadow phase draws one object, Mario's drop shadow (MarioActor::drawShadow, the only member of
    // MR::DrawType_AlphaShadow); under the traced shadows it would be a second one.
    if (rt_phase_ == static_cast<std::uint8_t>(motion::ScenePhase::AlphaShadow)) {
        return true;
    }
    // GALAXY_RT_DEBUG_HIDE_PHASE=n (diagnostics): scene phase n's draws are not drawn under the traced lighting.
    static const std::uint64_t debug_hide_phase = read_env_u64("GALAXY_RT_DEBUG_HIDE_PHASE", 255u);
    if (debug_hide_phase < 255u && rt_phase_ == static_cast<std::uint8_t>(debug_hide_phase)) {
        return true;
    }
    // GALAXY_RT_DEBUG_HIDE_MATERIAL_LO/_HI (diagnostics): the draws of the table's materials lo..hi (0: none) are not
    // drawn under the traced lighting (bisecting a draw by material; mat_id.py names them).
    // _PHASE: only in that scene phase.
    static const std::uint64_t debug_hide_lo = read_env_u64("GALAXY_RT_DEBUG_HIDE_MATERIAL_LO", 1u);
    static const std::uint64_t debug_hide_hi = read_env_u64("GALAXY_RT_DEBUG_HIDE_MATERIAL_HI", 0u);
    static const std::uint64_t debug_hide_material_phase = read_env_u64("GALAXY_RT_DEBUG_HIDE_MATERIAL_PHASE", 255u);
    if (current_rt_material_ >= debug_hide_lo && current_rt_material_ <= debug_hide_hi &&
        (debug_hide_material_phase >= 255u || rt_phase_ == static_cast<std::uint8_t>(debug_hide_material_phase))) {
        return true;
    }
    // The game's own mirror copies drawn in the translucent and image-effect phases (Mario's blended copy under glass)
    // carry a mirrored position matrix; under traced reflections they would double what the trace reflects.
    // GALAXY_RT_KEEP_GAME_MIRRORS=1 keeps them. Mario's reflection under ice (MarioActor::drawReflectModel, the
    // indirect phase) stays: ice reflects at its own Fresnel (n 1.31), 2 % looking down, and a floor's traced mirror
    // sees only his unlit underside, so under the trace alone he vanished from Freezeflame's rink (the owner, 27
    // October 2026: ice reflects everything). The opaque phases keep any mirrored geometry the game builds with a
    // negative scale.
    static const bool keep_mirrors = read_env_u64("GALAXY_RT_KEEP_GAME_MIRRORS", 0u) != 0u;
    const bool mirror_phase = rt_phase_ == static_cast<std::uint8_t>(motion::ScenePhase::Xlu) ||
                              rt_phase_ == static_cast<std::uint8_t>(motion::ScenePhase::ListXlu) ||
                              rt_phase_ == static_cast<std::uint8_t>(motion::ScenePhase::ImageEffect);
    if (!keep_mirrors && mirror_phase && renderer_.rt_reflections_active()) {
        const std::uint32_t base = (state_.cp(cp::kMatrixIndexA) & 0x3Fu) * 4u;
        const auto m = [this, base](unsigned row, unsigned column) {
            return std::bit_cast<float>(state_.xf(static_cast<std::uint16_t>(base + row * 4u + column)));
        };
        const float det = m(0, 0) * (m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1)) -
                          m(0, 1) * (m(1, 0) * m(2, 2) - m(1, 2) * m(2, 0)) +
                          m(0, 2) * (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0));
        if (std::isfinite(det) && det < 0.0f) {
            return true;
        }
    }
    if (current_rt_material_ == 0u) {
        return false;
    }
    const RtMaterial* const material = rt_material(current_rt_material_);
    return material != nullptr && material->hide;
}

// The current object's name, Shift-JIS bytes with blanks as '_': as the game reported it (game_object_name), else
// NameObj::mName (+4) in guest memory. Returns a key for the name (0 for none).
std::uint32_t GxBackend::rt_object_name(char* out, std::size_t size) {
    out[0] = 0;
    if (rt_object_ == 0u) {
        return 0u;
    }
    if (game_object_name(rt_object_, out, size)) {
        for (char* c = out; *c != 0; ++c) {
            const auto v = static_cast<unsigned char>(*c);
            if (v <= 0x20u || v == 0x7Fu) {
                *c = '_';
            }
        }
        return rt_object_;
    }
    if (frame_memory_ == nullptr) {
        return 0u;
    }
    const std::byte* field = resolve_guest_const_bytes(frame_memory_, rt_object_ + 4u, 4u);
    if (field == nullptr) {
        return 0u;
    }
    std::uint32_t pointer = 0u;
    std::memcpy(&pointer, field, 4u);
    pointer = byte_swap_u32(pointer);
    std::size_t n = 0u;
    for (std::uint32_t i = 0; pointer != 0u && i < 32u && n + 4u < size; ++i) {
        const std::byte* c = resolve_guest_const_bytes(frame_memory_, pointer + i, 1u);
        if (c == nullptr || *c == std::byte{0}) {
            break;
        }
        const auto v = std::to_integer<unsigned char>(*c);
        // Shift-JIS bytes as they are (the census decodes them); blanks and controls as '_'.
        out[n++] = v > 0x20u && v != 0x7Fu ? static_cast<char>(v) : '_';
    }
    out[n] = 0;
    return pointer;
}

// The census line of the current scene draw: its object's name, class, blend and the texture its
// environment stage (else stage 0) reads.
void GxBackend::note_rt_material() {
    RtSurface surface = classify_rt_surface(vs_key_, ps_key_);
    RtMaterialNote note{};
    char name[48]{};
    rt_object_name(name, sizeof(name));
    surface.surface_class = rt_named_class(name, surface.surface_class);
    note.name = name;
    note.object = rt_object_;
    note.surface = surface;
    note.stages = ps_key_.num_tev_stages;
    note.texgens = vs_key_.num_texgens;
    note.indirect = ps_key_.num_ind_stages;
    note.channel = state_.xf(xf::kChannelCtrlBase);
    const unsigned stage = surface.env_stage < 16u ? surface.env_stage : 0u;
    note.combiner = surface.env_stage < 16u ? ps_key_.stages[stage].color_env & 0xFFFFFFu : 0u;
    const BlendMode blend = state_.blend_mode();
    note.blend = !blend.blend_enable ? 0u
                 : blend.src_factor == BlendFactor::SrcAlpha && blend.dst_factor == BlendFactor::InvSrcAlpha ? 1u
                 : blend.dst_factor == BlendFactor::One ? 2u
                                                         : 3u;
    const unsigned map = ps_key_.stages[stage].order & 7u;
    const std::uint8_t bank = map >= 4u ? bp::kTexHighBankOffset : 0u;
    const std::uint32_t image0 = state_.bp(static_cast<std::uint8_t>(bp::kTexImage0Base + bank + (map & 3u)));
    note.texture = (state_.bp(static_cast<std::uint8_t>(bp::kTexImage3Base + bank + (map & 3u))) & 0xFFFFFFu) << 5u;
    note.texture_size = ((image0 & 0x3FFu) + 1u) | ((((image0 >> 10u) & 0x3FFu) + 1u) << 16u);
    note.texture_format = (image0 >> 20u) & 0xFu;
    note.phase = rt_phase_;
    note.buffer = rt_draw_buffer_;
    note.pack = rt_texture_source(map).pack;
    note.material_pack = current_rt_map_ < kMaxTextureMaps ? rt_texture_source(current_rt_map_).pack : 0u;
    galaxy::gx::note_rt_material(note);
}

// A water or clear draw the composites replace (Beach Bowl's ocean, drawn after the game's effects) is in no effects
// layer: the water's composite goes over it as over the game's water before the effects. Its keys were set before its
// textures named its material (flush_draw_state), so once its batch knows it is water its call takes the same pipeline
// without the layer.
void GxBackend::exclude_layer_water(DrawCall& call, const RtDrawInfo& info, bool rt) {
    if (call.aux_target != 3u || !rt || (info.surface & kRtDrawWater) == 0u) {
        return;
    }
    PixelShaderKey ps = ps_key_;
    ps.reserved = static_cast<std::uint8_t>(ps.reserved & ~kPsKeyLayer);
    RenderStateKey rs = render_state_key_;
    rs.blend_bits &= ~kRenderStateLayerTarget;
    const PsoKey key{current_vs_hash_, ps.hash(), rs};
    gpu::Pipeline* const pipeline = pipeline_cache_.get(key, ps, vs_key_);
    if (pipeline == nullptr) {
        return;
    }
    call.pipeline = pipeline;
    call.aux_target = pipeline_cache_.last_get_used_uber() ? 0u : 2u;
}

bool GxBackend::rt_mist_draw() const noexcept {
    if (current_rt_material_ == 0u || !scene_phase_ || side_layer_ != kSceneLayer || !renderer_.rt_mist_open()) {
        return false;
    }
    const RtMaterial* const material = rt_material(current_rt_material_);
    return material != nullptr && material->mist;
}

// A spray sprite (JPA's quad: JPADrawBillboard and its kin, one particle a draw at its own position matrix) as a puff
// of drops: a Gaussian cloud at the quad's centre (the middle of its corners' eye-space box: a parallelogram's centre),
// its width sigma half the quad's half side (its corners' farthest from the centre over root 2: the cloud reaches the
// quad's edges at two sigma), its optical depth through its centre its table row's (`intensity`: a fully opaque
// sprite's; else 2) times the sprite's opacity (TEV register 0's alpha, the particle's colour JPA sets: its alpha is
// the texture's times it). A Gaussian cloud of depth tau covers 2 pi sigma^2 Ein(tau), a quarter of pi Ein(tau) of the
// side squared, as much as the sprite's texture (its mean alpha times the side squared): the cloud textures (mean
// alpha 0.52) depth 2, the steam's (0.26) 0.8, the droplets' dots (0.14) 0.4. The corners are the draw's vertices in
// the frame's vertex ring, else JPA's centred quad (JPAResource jpa_pos: +-25 units).
void GxBackend::note_rt_mist(const DrawCall& call) {
    const std::uint32_t base = (state_.cp(cp::kMatrixIndexA) & 0x3Fu) * 4u;
    float m[3][4];
    for (unsigned r = 0; r < 3u; ++r) {
        for (unsigned c = 0; c < 4u; ++c) {
            m[r][c] = std::bit_cast<float>(state_.xf(static_cast<std::uint16_t>(base + r * 4u + c)));
        }
    }
    std::array<std::array<float, 3>, 6> corners{{{-25.0f, 25.0f, 0.0f},
                                                 {25.0f, 25.0f, 0.0f},
                                                 {25.0f, -25.0f, 0.0f},
                                                 {-25.0f, -25.0f, 0.0f}}};
    std::size_t count = 4u;
    const std::uint32_t vertices = call.vertex_view.SizeInBytes / static_cast<std::uint32_t>(sizeof(GxVertexOut));
    if (const std::byte* cpu = renderer_.vertex_ring().cpu_at(call.vertex_view.BufferLocation);
        cpu != nullptr && vertices >= 3u && vertices <= corners.size()) {
        for (std::uint32_t i = 0; i < vertices; ++i) {
            GxVertexOut v;
            std::memcpy(&v, cpu + static_cast<std::size_t>(i) * sizeof(GxVertexOut), sizeof(v));
            corners[i] = {v.position[0], v.position[1], v.position[2]};
        }
        count = vertices;
    }
    float lo[3] = {1.0e30f, 1.0e30f, 1.0e30f};
    float hi[3] = {-1.0e30f, -1.0e30f, -1.0e30f};
    std::array<std::array<float, 3>, 6> eye{};
    for (std::size_t i = 0; i < count; ++i) {
        for (unsigned r = 0; r < 3u; ++r) {
            eye[i][r] = m[r][0] * corners[i][0] + m[r][1] * corners[i][1] + m[r][2] * corners[i][2] + m[r][3];
            lo[r] = std::min(lo[r], eye[i][r]);
            hi[r] = std::max(hi[r], eye[i][r]);
        }
    }
    const float centre[3] = {0.5f * (lo[0] + hi[0]), 0.5f * (lo[1] + hi[1]), 0.5f * (lo[2] + hi[2])};
    float reach2 = 0.0f;
    for (std::size_t i = 0; i < count; ++i) {
        const float dx = eye[i][0] - centre[0];
        const float dy = eye[i][1] - centre[1];
        const float dz = eye[i][2] - centre[2];
        reach2 = std::max(reach2, dx * dx + dy * dy + dz * dz);
    }
    const float sigma = 0.5f * std::sqrt(0.5f * reach2);
    const float opacity = std::clamp(static_cast<float>(state_.tev_register_values()[1][3]) / 255.0f, 0.0f, 1.0f);
    const RtMaterial* const material = rt_material(current_rt_material_);
    const float depth = material != nullptr && material->intensity > 0.0f ? material->intensity : 2.0f;
    // Its brightness: the row's colour (white 1).
    const float white =
        material != nullptr ? (material->color[0] + material->color[1] + material->color[2]) / 3.0f : 1.0f;
    if (opacity > 1e-3f) {
        renderer_.rt_note_mist(centre, sigma, depth * opacity, white);
    }
}

bool GxBackend::begin_rt_batch(const DrawCall& call, RtDrawInfo& info) {
    // A spray sprite under the engine's mist: its puff, never traced or drawn (rt_hidden_draw).
    if (rt_mist_draw()) {
        note_rt_mist(call);
        return false;
    }
    // An untextured surface (no texture coordinates) takes its stage's `untextured` row, for this draw only: the
    // texture binding that sets current_rt_material_ runs only when the textures change.
    struct RestoreMaterial {
        std::uint16_t& slot;
        std::uint16_t value;
        ~RestoreMaterial() { slot = value; }
    } restore_material{current_rt_material_, current_rt_material_};
    if (vs_key_.num_texgens == 0u && rt_geometry_draw()) {
        if (const std::uint16_t untextured = rt_stage_untextured_material(); untextured != 0u) {
            current_rt_material_ = untextured;
        }
    }
    if (rt_material_census() && scene_phase_ && side_layer_ == kSceneLayer &&
        state_.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 6u)) == 0u) [[unlikely]] {
        note_rt_material();
    }
    // GALAXY_RT_UNTRACED=1: once per table material, phase, outcome and reason, what became of a perspective
    // scene draw (why a character casts no traced shadow): traced as geometry or a card, a glow, water, a
    // lens, late geometry, or left out (no window open, hidden, flat, or untraced with its reasons: blend
    // bits, colour and depth writes, cull-all, z texture, the alpha test, eligible, scene open).
    static const bool untraced_log = read_env_u64("GALAXY_RT_UNTRACED", 0u) != 0u;
    const bool log_outcome = untraced_log && scene_phase_ && side_layer_ == kSceneLayer && current_rt_material_ != 0u &&
                             state_.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 6u)) == 0u;
    const auto outcome = [&](const char* what) {
        if (!log_outcome) [[likely]] {
            return;
        }
        const std::uint32_t bb = render_state_key_.blend_bits;
        const std::uint32_t reasons =
            ((bb & ((1u << 0u) | (1u << 8u))) != 0u ? 1u : 0u) | ((bb & (1u << 25u)) == 0u ? 2u : 0u) |
            ((render_state_key_.zmode_bits & (1u << 4u)) == 0u ? 4u : 0u) |
            (render_state_key_.cull == static_cast<std::uint8_t>(CullMode::All) ? 8u : 0u) |
            ((ps_key_.flags & 0x02u) != 0u ? 16u : 0u) | (!alpha_test_keeps_all(ps_key_) ? 32u : 0u) |
            (rt_state_eligible_ ? 64u : 0u) | (renderer_.rt_scene_open() ? 128u : 0u);
        static std::unordered_set<std::uint64_t> logged;
        const bool water_open = renderer_.rt_water_open();
        const std::uint64_t key = static_cast<std::uint64_t>(current_rt_material_) |
                                  (static_cast<std::uint64_t>(rt_phase_) << 16u) |
                                  (static_cast<std::uint64_t>(reasons) << 24u) |
                                  (static_cast<std::uint64_t>(static_cast<unsigned char>(what[0])) << 32u) |
                                  (static_cast<std::uint64_t>(static_cast<unsigned char>(what[1])) << 40u) |
                                  (static_cast<std::uint64_t>(water_open ? 1u : 0u) << 48u);
        static std::unordered_set<std::uint64_t> identities;
        const bool new_identity =
            identities.size() < 65536u &&
            identities.insert(key ^ (static_cast<std::uint64_t>(rt_identity_seen_) * 0x9E3779B97F4A7C15ull)).second;
        if (logged.size() < 8192u && (logged.insert(key).second || new_identity)) {
            const RtMaterial* const logged_material = rt_material(current_rt_material_);
            std::fprintf(stderr,
                         "[rt-untraced] material=%u phase=%u buffer=%u%s reasons=0x%02X outcome=%s identity=%08X "
                         "water-open=%d tint=%.3f\n",
                         static_cast<unsigned>(current_rt_material_), static_cast<unsigned>(rt_phase_),
                         static_cast<unsigned>(rt_draw_buffer_ & 0xFFFFu), (rt_draw_buffer_ & 0x10000u) != 0u ? "x" : "o",
                         reasons, what, rt_identity_seen_, water_open ? 1 : 0,
                         logged_material != nullptr ? static_cast<double>(logged_material->tint_kept) : 1.0);
        }
    };
    note_scene_ao_view(call);
    // The game's sky lists: the traced sky's geometry, whatever their viewport (the sky and air layers draw at the far
    // plane through a flattened one).
    if (rt_sun_buffer_ && scene_phase_ && side_layer_ == kSceneLayer &&
        (enhanced_ps_flags_ & kEnhancedRtPhysical) != 0u) {
        outcome("sun");
        return false;
    }
    const bool sky = rt_sky_buffer_ && scene_phase_ && side_layer_ == kSceneLayer && renderer_.rt_sky_open() &&
                     state_.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 6u)) == 0u && !rt_hidden_draw();
    if (!scene_phase_ || !(sky || renderer_.rt_scene_open() || renderer_.rt_glow_open() || renderer_.rt_water_open() ||
                           renderer_.rt_late_open())) {
        outcome("closed");
        return false;
    }
    // What the traced lighting hides from the picture (the game's drop-shadow volumes, the table's faked
    // light cones) stays out of the traced scene too, or an invisible shadow volume would cast its own shadow.
    if (rt_hidden_draw() && !rt_traced_clear_draw() && !rt_trace_only_) {
        outcome("hidden");
        return false;
    }
    // GALAXY_RT_DEBUG_UNTRACE=id[,id...] (diagnostics): those table materials' draws stay out of the traced scene, still
    // drawn; with GALAXY_RT_DEBUG_UNTRACE_AB=1 on odd presents only, so two snapshots in a row differ by what they add
    // to the traced picture (a character's image in a crystal, a lamp's light).
    static const std::vector<std::uint16_t> debug_untrace = [] {
        std::vector<std::uint16_t> ids;
        char value[512]{};
        std::size_t length = 0u;
        if (getenv_s(&length, value, sizeof(value), "GALAXY_RT_DEBUG_UNTRACE") == 0 && length > 1u) {
            for (const char* p = value; *p != '\0';) {
                char* end = nullptr;
                const unsigned long id = std::strtoul(p, &end, 10);
                if (end == p) {
                    break;
                }
                ids.push_back(static_cast<std::uint16_t>(id));
                p = *end == ',' ? end + 1 : end;
            }
        }
        return ids;
    }();
    static const bool debug_untrace_ab = read_env_u64("GALAXY_RT_DEBUG_UNTRACE_AB", 0u) != 0u;
    if (!debug_untrace.empty() && (!debug_untrace_ab || ((g_snapshot_presents + 1u) & 1u) != 0u) &&
        std::find(debug_untrace.begin(), debug_untrace.end(), current_rt_material_) != debug_untrace.end())
        [[unlikely]] {
        outcome("debug untraced");
        return false;
    }
    const ScaledGxViewport viewport = compute_scaled_gx_viewport(call.xf_viewport, renderer_.efb_scale());
    const bool flat = !call.has_viewport || !viewport.valid() || viewport.max_depth - viewport.min_depth < 0.5f;
    static const bool log_viewports = read_env_u64("GALAXY_RT_LOG_VIEWPORTS", 0u) != 0u;
    if (log_viewports) [[unlikely]] {
        static std::unordered_set<std::uint64_t> seen;
        const std::uint64_t key = static_cast<std::uint64_t>(current_rt_material_) |
                                  (static_cast<std::uint64_t>(rt_phase_) << 16u) |
                                  (static_cast<std::uint64_t>(static_cast<std::uint32_t>(viewport.x) & 0xFFFu) << 24u) |
                                  (static_cast<std::uint64_t>(static_cast<std::uint32_t>(viewport.width) & 0xFFFu) << 36u) |
                                  (static_cast<std::uint64_t>(static_cast<std::uint32_t>(viewport.height) & 0xFFFu) << 48u);
        if (seen.size() < 4096u && seen.insert(key).second) {
            std::fprintf(stderr, "[rt-vp] material=%u phase=%u buffer=%u vp=%.0f,%.0f,%.0f,%.0f depth=%.2f,%.2f\n",
                         static_cast<unsigned>(current_rt_material_), static_cast<unsigned>(rt_phase_),
                         static_cast<unsigned>(rt_draw_buffer_ & 0xFFFFu), viewport.x, viewport.y, viewport.width,
                         viewport.height, viewport.min_depth, viewport.max_depth);
        }
    }
    // MR::clearZBuffer: a depth write through a flattened viewport puts the far plane under
    // everything drawn before (the sky and air layers); that geometry occludes nothing.
    // After the scene's trace a depth clear starts no new traced scene: the water window drawn since stays open for its
    // trace (some stages clear the depth late in the frame).
    if (flat && (render_state_key_.zmode_bits & (1u << 4u)) != 0u) {
        if (!rt_scene_traced_) {
            renderer_.rt_begin_scene();
            rt_view_set_ = false;
        }
        if (!sky) {
            return false;
        }
    }
    if (sky) {
        begin_rt_sky_batch(info);
        return true;
    }
    if (flat) {
        outcome("flat");
        return false;
    }
    // The engine's traced glass goes by the water window whatever its state (never solid geometry); its clear solids
    // are solid geometry no camera ray takes.
    const bool translucent = !rt_geometry_draw() || (rt_traced_clear_draw() && !rt_traced_solid_draw());
    // A trace-only draw is solid geometry or a card: a translucent one would be a glow or water no picture shows.
    if (rt_trace_only_ && translucent) {
        outcome("trace-only translucent");
        return false;
    }
    // A black hole's lens (the table's `lens`): captured with the glows for the finish's bending.
    const RtMaterial* const lens_material = translucent ? rt_material(current_rt_material_) : nullptr;
    const bool lens = lens_material != nullptr && lens_material->lens && renderer_.rt_glow_open();
    bool clear_added = false;
    if (translucent && !lens && renderer_.rt_water_open() && rt_glow_draw()) {
        const RtMaterial* const clear = rt_material(current_rt_material_);
        const bool position_indexed = draw_positions_indexed();
        clear_added = clear != nullptr && !clear->particle && !clear->waterfall && clear->emission == RtEmission::None &&
                      (clear->surface_class == static_cast<std::uint8_t>(RtSurfaceClass::Crystal) ||
                       clear->surface_class == static_cast<std::uint8_t>(RtSurfaceClass::Water)) &&
                      position_indexed;
    }
    // A helper row (an environment map) is the draw's own surface when it is the only texture the draw samples (clear
    // materials made of a highlight map alone), or when every texture it samples is a helper (glass spheres mapping an
    // indirect lens and an environment map).
    bool own_helper = std::popcount(current_texture_map_mask_) == 1;
    if (!own_helper && current_texture_map_mask_ != 0u) {
        own_helper = true;
        for (unsigned m = 0; m < kMaxTextureMaps && own_helper; ++m) {
            if ((current_texture_map_mask_ & (1u << m)) != 0u) {
                const RtMaterial* const helper = rt_material(current_texture_handles_[m].rt_material);
                own_helper = helper != nullptr && helper->helper;
            }
        }
    }
    // A blended clear model the table names glass or crystal, whatever its blend (the air bubble: source alpha over the
    // source colour): a surface of the water pass, its reflections over what it drew. One the table also names an
    // emitter (the rotating steps' soft glow) stays glass: blended, it is in no traced geometry that could emit.
    bool clear_blended = false;
    if (translucent && !lens && !clear_added && renderer_.rt_water_open() && scene_phase_ && side_layer_ == kSceneLayer &&
        (rt_phase_ == static_cast<std::uint8_t>(motion::ScenePhase::Xlu) ||
         rt_phase_ == static_cast<std::uint8_t>(motion::ScenePhase::ListXlu) ||
         rt_phase_ == static_cast<std::uint8_t>(motion::ScenePhase::AfterIndirect))) {
        const RtMaterial* const clear = rt_material(current_rt_material_);
        const BlendMode blend = state_.blend_mode();
        const bool position_indexed = draw_positions_indexed();
        clear_blended = clear != nullptr && !clear->particle && (!clear->helper || own_helper) && !clear->waterfall &&
                        clear->surface_class == static_cast<std::uint8_t>(RtSurfaceClass::Crystal) &&
                        blend.blend_enable && !blend.logic_op_enable && !blend.subtract && blend.color_update &&
                        blend.dst_factor != BlendFactor::One &&
                        position_indexed;
    }
    // A blended coat the table names metal (the Observatory's gold rings: an environment-mapped layer the translucent
    // phase draws over the model's dark base, after the composite): a surface of the water pass, its traced reflection
    // in place of the game's painted one.
    bool metal_coat = false;
    if (translucent && !lens && !clear_added && !clear_blended && renderer_.rt_water_open() && scene_phase_ &&
        side_layer_ == kSceneLayer &&
        (rt_phase_ == static_cast<std::uint8_t>(motion::ScenePhase::Xlu) ||
         rt_phase_ == static_cast<std::uint8_t>(motion::ScenePhase::ListXlu))) {
        const RtMaterial* const metal = rt_material(current_rt_material_);
        const BlendMode blend = state_.blend_mode();
        const bool position_indexed = draw_positions_indexed();
        metal_coat = metal != nullptr && !metal->particle && (!metal->helper || own_helper) && metal->metallic >= 0.5f &&
                     metal->emission == RtEmission::None && blend.blend_enable && !blend.logic_op_enable &&
                     !blend.subtract && blend.color_update && position_indexed;
    }
    // The table's traced clear surfaces join the water window under the engine whatever their blend (their draw is
    // hidden: the engine traces them, rt_engine.hlsl EngineClearCS).
    const RtMaterial* const traced_clear = translucent && !lens ? rt_material(current_rt_material_) : nullptr;
    const bool traced = traced_clear != nullptr && traced_clear->traced && !traced_clear->solid &&
                        renderer_.rt_engine_clear() &&
                        renderer_.rt_water_open() && scene_phase_ && side_layer_ == kSceneLayer &&
                        draw_positions_indexed();
    const bool ocean = translucent && !lens && rt_ocean_water_draw();
    const bool glow = translucent && !lens && !clear_added && !clear_blended && !metal_coat && !traced && !ocean &&
                      (rt_glow_draw() || rt_sprite_light_draw());
    if (glow && current_rt_material_ == 0u) {
        const unsigned map = ps_key_.stages[0].order & 7u;
        const std::uint8_t bank = map >= 4u ? bp::kTexHighBankOffset : 0u;
        note_rt_unnamed_glow((state_.bp(static_cast<std::uint8_t>(bp::kTexImage3Base + bank + (map & 3u))) & 0xFFFFFFu)
                             << 5u);
    }
    bool water =
        translucent && !lens && !glow && (clear_added || clear_blended || metal_coat || traced || rt_water_draw() || ocean);
    // An opaque late draw the table names water or crystal (ice drawn after the indirect capture with the game's own
    // refraction): a surface of the water pass, its reflections laid over what it drew.
    bool opaque_clear = false;
    if (!water && translucent && !glow && !lens && rt_late_draw() && renderer_.rt_water_open()) {
        const RtMaterial* const clear = rt_material(current_rt_material_);
        const bool position_indexed = draw_positions_indexed();
        opaque_clear = clear != nullptr && !clear->particle && (!clear->helper || own_helper) &&
                       (clear->surface_class == static_cast<std::uint8_t>(RtSurfaceClass::Crystal) ||
                        clear->surface_class == static_cast<std::uint8_t>(RtSurfaceClass::Water)) &&
                       position_indexed;
        water = opaque_clear;
    }
    const bool late = translucent && !glow && !water && !lens && rt_late_draw();
    if (const RtMaterial* clear = rt_material(current_rt_material_);
        clear != nullptr && rt_log_glows() &&
        (clear->surface_class == static_cast<std::uint8_t>(RtSurfaceClass::Water) ||
         clear->surface_class == static_cast<std::uint8_t>(RtSurfaceClass::Crystal))) [[unlikely]] {
        static std::unordered_set<std::uint32_t> logged;
        if (logged.insert(current_rt_material_ | (static_cast<std::uint32_t>(rt_phase_) << 16u) |
                          (water ? 1u << 24u : 0u) | (renderer_.rt_water_open() ? 1u << 25u : 0u) |
                          (side_layer_ == kSceneLayer ? 1u << 26u : 0u)).second) {
            const BlendMode blend = state_.blend_mode();
            const bool position_indexed = draw_positions_indexed();
            std::fprintf(stderr,
                         "[rt-clear] material=%u class=%u phase=%u blend=%d src=%u dst=%u indexed=%d geometry=%d "
                         "water-eligible=%d glow=%d water=%d late=%d water-open=%d scene-layer=%d maps=%02X\n",
                         static_cast<unsigned>(current_rt_material_), static_cast<unsigned>(clear->surface_class),
                         static_cast<unsigned>(rt_phase_), blend.blend_enable ? 1 : 0,
                         static_cast<unsigned>(blend.src_factor), static_cast<unsigned>(blend.dst_factor),
                         position_indexed ? 1 : 0, translucent ? 0 : 1,
                         rt_water_eligible_ ? 1 : 0, glow ? 1 : 0, water ? 1 : 0, late ? 1 : 0,
                         renderer_.rt_water_open() ? 1 : 0, side_layer_ == kSceneLayer ? 1 : 0,
                         static_cast<unsigned>(current_texture_map_mask_));
        }
    }
    outcome(translucent ? (glow ? "glow" : water ? "water" : lens ? "lens" : late ? "late" : "untraced")
                        : rt_state_eligible_ ? "geometry" : "card");
    if (translucent && !glow && !water && !lens && !late) {
        return false;
    }
    const auto projection = [this](unsigned i) {
        return std::bit_cast<float>(state_.xf(static_cast<std::uint16_t>(xf::kProjectionBase + i)));
    };
    for (unsigned i = 0; i < 6u; ++i) {
        info.projection[i] = projection(i);
    }
    info.min_depth = viewport.min_depth;
    info.max_depth = viewport.max_depth;
    info.cull = render_state_key_.cull;
    info.phase = rt_phase_;
    // The surface class (rt_surfaces.h) for reflections; diagnostics for GALAXY_RT_DUMP.
    RtSurface surface = classify_rt_surface(vs_key_, ps_key_);
    if (surface.surface_class == RtSurfaceClass::Metal || surface.surface_class == RtSurfaceClass::Glossy) {
        // Clear materials the GX state shows as metal or gloss go by their object's name.
        char name[48];
        const std::uint32_t pointer = rt_object_name(name, sizeof(name));
        if (pointer != 0u) {
            if (rt_named_.size() > 4096u) {
                rt_named_.clear();
            }
            auto [it, inserted] = rt_named_.try_emplace(pointer, std::uint8_t{0});
            if (inserted) {
                it->second = static_cast<std::uint8_t>(rt_named_class(name, surface.surface_class)) + 1u;
            }
            if (it->second == static_cast<std::uint8_t>(RtSurfaceClass::Crystal) + 1u) {
                surface.surface_class = RtSurfaceClass::Crystal;
            }
        }
    }
    // The material table (rt_materials.h) gives this texture's class where it names one; particles
    // are never traced geometry or water (glows still light, and so do the table's emitting ones).
    if (const RtMaterial* material = rt_material(current_rt_material_); material != nullptr) {
        if (material->particle && material->emission == RtEmission::None && !glow) {
            return false;
        }
        // Sky domes and backdrops drawn among the scene (after its depth clear): never traced, or every
        // ray towards the sun meets them.
        if (material->sky && !glow) {
            return false;
        }
        if (material->surface_class != 0xFFu) {
            surface.surface_class = static_cast<RtSurfaceClass>(material->surface_class);
        }
    }
    info.rt_material = current_rt_material_;
    info.light_key = rt_light_key();
    // (An untextured draw's map register holds an earlier draw's texture: not logged.)
    if (current_rt_material_ == 0u && !glow && current_texture_map_mask_ != 0u && rt_log_glows()) {
        const unsigned map = ps_key_.stages[0].order & 7u;
        const std::uint8_t bank = map >= 4u ? bp::kTexHighBankOffset : 0u;
        note_rt_unnamed_surface((state_.bp(static_cast<std::uint8_t>(bp::kTexImage3Base + bank + (map & 3u))) & 0xFFFFFFu)
                                << 5u);
    }
    // High quality's textured hits: the material texture (the scene keeps its own view of it).
    if (current_texture_table_.ptr != 0u && current_rt_map_ < kMaxTextureMaps) {
        info.texture = rt_texture_source(current_rt_map_);
        info.texture_map = current_rt_map_;
        const TexMode mode = state_.tex_mode(current_rt_map_);
        info.texture_wrap = static_cast<std::uint32_t>(mode.wrap_s) | (static_cast<std::uint32_t>(mode.wrap_t) << 2u);
        info.texgen = rt_texgen_of_map(current_rt_map_);
    }
    if ((water && !metal_coat && surface.surface_class == RtSurfaceClass::Metal) || clear_added) {
        // A blended environment map is glass, not metal (unless the table names it a metal coat); an added one over
        // ice is the ice's gloss.
        surface.surface_class = RtSurfaceClass::Crystal;
    }
    if (water && !metal_coat && surface.surface_class != RtSurfaceClass::Water &&
        surface.surface_class != RtSurfaceClass::Crystal) {
        return false;
    }
    info.shading = {ps_key_.num_tev_stages | (static_cast<std::uint32_t>(vs_key_.num_texgens) << 8u) |
                        (static_cast<std::uint32_t>(ps_key_.num_ind_stages) << 16u) |
                        (static_cast<std::uint32_t>(surface.features) << 24u),
                    (surface.env_stage < 16u ? ps_key_.stages[surface.env_stage].color_env & 0xFFFFFFu : 0u) |
                        (static_cast<std::uint32_t>(surface.env_stage) << 24u),
                    vs_key_.channel[0], vs_key_.channel[1]};
    info.channel = state_.xf(xf::kChannelCtrlBase);
    info.ambient = state_.xf(xf::kAmbientColorBase);
    info.material = state_.xf(xf::kMaterialColorBase);
    {
        const FogParams fog = state_.fog();
        info.fog = {fog_param_float_bits(fog.a_raw & 0xFFFFFu),
                    (fog.b_magnitude_raw & 0xFFFFFFu) | ((fog.b_shift_raw & 0x1Fu) << 24u) |
                        ((static_cast<std::uint32_t>(ps_key_.fog_type) & 7u) << 29u),
                    fog_param_float_bits(fog.c_raw), (fog.color_rgb & 0xFFFFFFu) | (fog.projection ? 1u << 24u : 0u)};
    }
    {
        const XfChannelCtrl color = state_.channel_ctrl(0u);
        info.surface = color.lighting_enable && color.light_mask != 0u ? kRtDrawLit : 0u;
        info.surface |= color.lighting_enable && (color.light_mask & 0x10u) != 0u ? kRtDrawPointLit : 0u;
        // The lighting reads its ambient register and light 0 colour (RtDrawInfo).
        if (info.surface != 0u && (color.light_mask & 1u) != 0u && !color.ambient_from_vertex &&
            (rt_draw_lights_ & 1u) != 0u && rt_light_aim_.blend >= 1.0f) {
            info.surface |= kRtDrawSunKey;
        }
        if (info.surface != 0u && (ps_key_.reserved & kPsKeyEnhancedLighting) != 0u &&
            (ps_key_.ind_reserved & kEnhancedLit) != 0u && (enhanced_ps_flags_ & kEnhancedRtLighting) != 0u) {
            info.surface |= kRtDrawDelit;
        }
        if (lens) {
            info.surface = kRtDrawGlow | kRtDrawLens;
        } else if (glow) {
            info.surface = kRtDrawGlow;
            // How much of its colour the picture takes (a glow light source emits that; RtDrawInfo.blend).
            const BlendMode blend = state_.blend_mode();
            info.blend = (blend.blend_enable ? 1u : 0u) | (static_cast<std::uint32_t>(blend.src_factor) << 1u) |
                         (static_cast<std::uint32_t>(blend.dst_factor) << 4u) | (blend.subtract ? 0x80u : 0u) |
                         (blend.color_update ? 0x100u : 0u) | (blend.logic_op_enable ? 0x200u : 0u);
        } else if (water) {
            const RtMaterial* const fall = rt_material(current_rt_material_);
            const RtSurfaceClass water_class = metal_coat ? RtSurfaceClass::Metal : surface.surface_class;
            info.surface = kRtDrawWater | (static_cast<std::uint32_t>(water_class) << kRtDrawClassShift) |
                           (fall != nullptr && fall->waterfall ? kRtDrawWaterfall : 0u) |
                           (fall != nullptr && fall->mirror ? kRtDrawWaterMirror : 0u) |
                           (fall != nullptr && fall->ice && !metal_coat ? kRtDrawWaterIce : 0u) |
                           (opaque_clear ? kRtDrawWaterOpaque : 0u);
        } else {
            info.surface |= static_cast<std::uint32_t>(surface.surface_class) << kRtDrawClassShift;
            info.surface |= rt_trace_only_ || rt_traced_solid_draw() ? kRtDrawUnseen : 0u;
            if (!rt_state_eligible_) {
                info.surface |= rt_card_draw() ? kRtDrawCard : kRtDrawAlphaTest;
                // The alpha its test passes from (GREATER ref: ref + 1, GEQUAL ref: ref, the first such compare;
                // other tests keep it solid) and the alpha map: TEV stage 0's texture.
                const AlphaCompare test = state_.alpha_compare();
                std::uint32_t from = 0u;
                if (test.comp0 == CompareFunc::Greater || test.comp0 == CompareFunc::GEqual) {
                    from = test.ref0 + (test.comp0 == CompareFunc::Greater ? 1u : 0u);
                } else if (test.comp1 == CompareFunc::Greater || test.comp1 == CompareFunc::GEqual) {
                    from = test.ref1 + (test.comp1 == CompareFunc::Greater ? 1u : 0u);
                }
                const unsigned map = ps_key_.stages[0].order & 7u;
                if (from != 0u && from <= 255u && current_texture_table_.ptr != 0u && map < kMaxTextureMaps) {
                    info.alpha = rt_texture_source(map);
                    info.alpha_from = info.alpha.resource != nullptr ? from : 0u;
                }
                if (log_outcome) [[unlikely]] {
                    static std::unordered_set<std::uint32_t> logged_alpha;
                    if (logged_alpha.insert(current_rt_material_).second) {
                        const std::uint32_t order = ps_key_.stages[0].order;
                        std::fprintf(stderr,
                                     "[rt-alpha] material=%u card=%d comp0=%u ref0=%u comp1=%u ref1=%u logic=%u from=%u "
                                     "map=%u coord=%u enable=%d mask=%02X texgens=%u stages=%u alpha-map=%d rt-map=%u\n",
                                     static_cast<unsigned>(current_rt_material_), rt_card_draw() ? 1 : 0,
                                     static_cast<unsigned>(test.comp0), static_cast<unsigned>(test.ref0),
                                     static_cast<unsigned>(test.comp1), static_cast<unsigned>(test.ref1),
                                     static_cast<unsigned>(test.logic), from, map, (order >> 3u) & 7u,
                                     (order >> 6u) & 1u, static_cast<unsigned>(current_texture_map_mask_),
                                     static_cast<unsigned>(vs_key_.num_texgens),
                                     static_cast<unsigned>(ps_key_.num_tev_stages), info.alpha.resource != nullptr ? 1 : 0,
                                     current_rt_map_);
                    }
                }
            }
            // No traced shadow from a material the table marks shadow 0 (a helper row, an environment map or
            // a mask, speaks for its texture, not for the draw), nor from a clear dielectric (crystal, ice,
            // glass), which passes about 91 % of the light ((1 - F)^2 at F0 0.047), nor from water (F0 0.02:
            // 96 % passes): the far side of Bonefin's ocean sphere, drawn opaque with the whole sphere's triangles,
            // closed its inside off from the sun and the sky and read black.
            if (const RtMaterial* shade = rt_material(current_rt_material_);
                (shade != nullptr && !shade->shadow && !shade->helper) ||
                surface.surface_class == RtSurfaceClass::Crystal || surface.surface_class == RtSurfaceClass::Water) {
                info.surface |= kRtDrawNoShadow;
            }
        }
    }
    {
        const std::uint8_t mask = state_.channel_ctrl(0u).light_mask;
        unsigned found = 0u;
        for (unsigned n = 0; n < 8u && found < 2u; ++n) {
            if ((mask & (1u << n)) == 0u) {
                continue;
            }
            const auto word = [&](unsigned k) {
                return state_.xf(static_cast<std::uint16_t>(xf::kLightsBase + 16u * n + k));
            };
            info.light_index[found] = n;
            for (unsigned k = 0; k < 6u; ++k) {
                info.light[found][k] = std::bit_cast<float>(word(10u + k));
            }
            info.light_color[found] = word(3u);
            ++found;
        }
    }
    fill_rt_tev(info, renderer_.rt_physical() ? 1.0f : 0.55f);
    // A water draw's waves: the first TEV stage that moves its texture coordinates by an indirect texture (the game's
    // wave map). Its texture slots become the wave map's coordinates and the water texture's (rt_reflections.hlsl
    // water_wave); RtDrawInfo.wave_params: flags (bit 0 waves, bits 1-3 the s t u bias, bits 4-5 the format), the wave
    // map's coordinate scale (slot 0 to its 0-1 coordinates, as the uber shader samples it), the indirect matrix's
    // direction part (s from x, s from y, t from x, t from y; unit scale), the water texture's coordinate scale (slot 1).
    if ((info.surface & kRtDrawWater) != 0u && ps_key_.num_ind_stages != 0u && vs_key_.num_texgens != 0u &&
        current_texture_table_.ptr != 0u) {
        const unsigned texgens = vs_key_.num_texgens;
        for (unsigned s = 0; s < ps_key_.num_tev_stages && s < 16u; ++s) {
            const std::uint32_t cmd = ps_key_.ind_cmd[s / 4u][s & 3u];
            const unsigned ind_stage = cmd & 3u;
            const unsigned matrix = (cmd >> 9u) & 3u;
            if (cmd == 0u || ind_stage >= ps_key_.num_ind_stages || matrix == 0u || ((cmd >> 11u) & 3u) != 0u) {
                continue;
            }
            const unsigned wave_map = (ps_key_.ind_ref >> (ind_stage * 6u)) & 7u;
            const unsigned wave_coord_raw = (ps_key_.ind_ref >> (ind_stage * 6u + 3u)) & 7u;
            const unsigned wave_coord = wave_coord_raw < texgens ? wave_coord_raw : 0u;
            const unsigned main_map = ps_key_.stages[s].order & 7u;
            const unsigned main_coord_raw = (ps_key_.stages[s].order >> 3u) & 7u;
            const unsigned main_coord = main_coord_raw < texgens ? main_coord_raw : 0u;
            if ((current_texture_map_mask_ & (1u << wave_map)) == 0u) {
                break;
            }
            const auto coord_size = [&](unsigned coord, unsigned axis) {
                return static_cast<float>(coordinate_scales(state_, scales_from_textures_)[coord & 7u][axis]);
            };
            const std::uint32_t scale_word = ps_key_.ind_scale[ind_stage / 2u] >> ((ind_stage & 1u) != 0u ? 8u : 0u);
            const TexImage wave_image = state_.tex_image(wave_map);
            const TexImage main_image = state_.tex_image(main_map);
            const auto sign11 = [](std::uint32_t value) {
                value &= 0x7FFu;
                return static_cast<float>((value & 0x400u) != 0u ? static_cast<std::int32_t>(value | ~0x7FFu)
                                                                  : static_cast<std::int32_t>(value));
            };
            const unsigned row = (matrix - 1u) * 3u;
            const std::uint32_t a = state_.bp(static_cast<std::uint8_t>(bp::kIndMtxBase + row));
            const std::uint32_t b = state_.bp(static_cast<std::uint8_t>(bp::kIndMtxBase + row + 1u));
            // s = ma x + mc y (+ me z), t = mb x + md y (+ mf z): the map's x and y as a slope, z left out.
            float m[4] = {sign11(a), sign11(b), sign11(a >> 11u), sign11(b >> 11u)};
            const float norm = std::sqrt(0.5f * (m[0] * m[0] + m[1] * m[1] + m[2] * m[2] + m[3] * m[3]));
            if (norm <= 0.0f) {
                break;
            }
            info.wave = rt_texture_source(wave_map);
            if (info.wave.resource == nullptr) {
                break;
            }
            const std::uint32_t flags = 1u | (((cmd >> 4u) & 7u) << 1u) | (((cmd >> 2u) & 3u) << 4u);
            info.wave_params = {std::bit_cast<float>(flags),
                                coord_size(wave_coord, 0u) / static_cast<float>(1u << (scale_word & 0xFu)) /
                                    static_cast<float>(std::max<unsigned>(wave_image.width, 1u)),
                                coord_size(wave_coord, 1u) / static_cast<float>(1u << ((scale_word >> 4u) & 0xFu)) /
                                    static_cast<float>(std::max<unsigned>(wave_image.height, 1u)),
                                m[0] / norm,
                                m[1] / norm,
                                m[2] / norm,
                                m[3] / norm,
                                coord_size(main_coord, 0u) / static_cast<float>(std::max<unsigned>(main_image.width, 1u)),
                                coord_size(main_coord, 1u) / static_cast<float>(std::max<unsigned>(main_image.height, 1u)),
                                0.0f,
                                0.0f,
                                0.0f};
            info.texgen = wave_coord;
            info.texgen2 = main_coord;
            break;
        }
    }
    if (!rt_view_set_) {
        renderer_.rt_set_view(scene_depth_view(viewport));
        rt_view_set_ = true;
        note_interp_rt_projection();
    }
    return true;
}

// The recording scene's projection as the ray tracer took its view (scene_depth_view, before the jitter): its
// sub-frames rebuild their own from it and the scene before's.
void GxBackend::note_interp_rt_projection() {
    if (!interp_recording_ || side_layer_ != kSceneLayer ||
        state_.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 6u)) != 0u) {
        return;
    }
    InterpScene& scene = interp_scenes_[interp_scene_];
    if (!scene.valid) {
        return;
    }
    for (unsigned i = 0; i < 6u; ++i) {
        scene.rt_projection[i] = std::bit_cast<float>(state_.xf(static_cast<std::uint16_t>(xf::kProjectionBase + i)));
    }
    const float scale = static_cast<float>(renderer_.efb_scale());
    scene.rt_viewport[0] = 2.0f * std::fabs(std::bit_cast<float>(state_.xf(static_cast<std::uint16_t>(xf::kViewportBase)))) * scale;
    scene.rt_viewport[1] =
        2.0f * std::fabs(std::bit_cast<float>(state_.xf(static_cast<std::uint16_t>(xf::kViewportBase + 1u)))) * scale;
    scene.rt_projection_known = true;
}

RtView GxBackend::scene_depth_view(const ScaledGxViewport& viewport) const {
    const auto projection = [this](unsigned i) {
        return std::bit_cast<float>(state_.xf(static_cast<std::uint16_t>(xf::kProjectionBase + i)));
    };
    // fill_vs_projection: D3D clip z = -(z E + F) (1 - 1e-7).
    constexpr float kFarEps = 1.0f - 1e-7f;
    RtView view{};
    view.projection = {projection(0u), projection(1u), projection(2u), projection(3u)};
    // The scene's depth is drawn with the temporal upscaler's jitter (apply_scene_jitter: clip x + jx w, so the
    // projection's x offset B becomes B - jx; y likewise). Passes rebuild every sample's position from that depth, so
    // the view carries the jitter too, or each sample would land up to half a pixel off its surface.
    if (temporal_scene_ && state_.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 6u)) == 0u) {
        const float scale = static_cast<float>(renderer_.efb_scale());
        const float width = 2.0f * std::fabs(std::bit_cast<float>(state_.xf(static_cast<std::uint16_t>(xf::kViewportBase)))) * scale;
        const float height = 2.0f * std::fabs(std::bit_cast<float>(state_.xf(static_cast<std::uint16_t>(xf::kViewportBase + 1u)))) * scale;
        if (width > 0.0f && height > 0.0f) {
            view.projection[1] -= 2.0f * scene_jitter_px_[0] / width;
            view.projection[3] -= -2.0f * scene_jitter_px_[1] / height;
            view.jitter = {scene_jitter_px_[0], scene_jitter_px_[1]};
        }
    }
    view.z_scale = -projection(4u) * kFarEps;
    view.z_offset = -projection(5u) * kFarEps;
    view.viewport = {viewport.x, viewport.y, viewport.width, viewport.height};
    view.min_depth = viewport.min_depth;
    view.max_depth = viewport.max_depth;
    return view;
}

// The first perspective scene draw with depth since the scene began or the game last cleared its depth
// (MR::clearZBuffer draws through a flattened viewport after the sky and air layers, which may project otherwise).
void GxBackend::note_scene_ao_view(const DrawCall& call) {
    if (ao_scene_applied_ || !scene_phase_ || side_layer_ != kSceneLayer ||
        state_.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 6u)) != 0u || !call.has_viewport) {
        return;
    }
    const ScaledGxViewport viewport = compute_scaled_gx_viewport(call.xf_viewport, renderer_.efb_scale());
    if (!viewport.valid()) {
        return;
    }
    if (viewport.max_depth - viewport.min_depth < 0.5f) {
        if ((render_state_key_.zmode_bits & (1u << 4u)) != 0u) {
            ao_view_set_ = false;
        }
        return;
    }
    if (!ao_view_set_) {
        renderer_.set_scene_ao_view(scene_depth_view(viewport));
        ao_view_set_ = true;
    }
}

void GxBackend::apply_scene_ao() {
    if (ao_scene_applied_ || side_layer_ != kSceneLayer) {
        return;
    }
    ao_scene_applied_ = true;
    if (!ao_view_set_) {
        return;
    }
    flush_pending_draw_batch();
    renderer_.record_scene_ao();
}

// The scene's key light (actor_lights.h): the game's visible sun (GALAXY_RT_SUN overrides it), else the sun its sky
// shows (rt_sky_sun.h), else the world-fixed GX light 0 most of the previous scene's lit draws took (the stage's last
// one while they take none), else none. The ray tracer lights a scene without the game's sun from that key.
// GALAXY_RT_ACTOR_LIGHTS=1 also turns each scene's follow-camera lights towards the key (over kActorLightRampScenes
// scenes when a key comes or goes, over a few when it changes; without a key or the scene camera they stay the
// game's); =2 does that and keys the ray tracer in odd scenes only, for frame-to-frame comparison. Off by default:
// the characters keep the game's lights, with the traced shadows, occlusion and lights on top.
void GxBackend::aim_rt_scene_lights(const std::array<float, 12>* view) {
    static const std::uint64_t mode = read_env_u64("GALAXY_RT_ACTOR_LIGHTS", 0u);
    const std::array<RtWorldKey, 8> candidates = rt_world_keys_;
    const std::uint32_t candidate_count = rt_world_key_count_;
    const RtWorldKey* world = nullptr;
    for (std::uint32_t i = 0; i < candidate_count; ++i) {
        if (world == nullptr || candidates[i].draws > world->draws) {
            world = &candidates[i];
        }
    }
    // The stage's last world key lights its scenes whose lit draws take none (the game's
    // follow-camera lights only).
    if (world != nullptr) {
        rt_world_held_ = *world;
    }
    const RtWorldKey* world_key = world != nullptr ? world : rt_world_held_.draws != 0u ? &rt_world_held_ : nullptr;
    rt_world_key_count_ = 0u;
    rt_light_aim_ = {};
    rt_known_lights_ = 0u;
    rt_follow_lights_ = 0u;
    rt_light_pending_ = 0u;
    ++rt_light_serial_;
    rt_light_tracking_ = view != nullptr && mode != 0u;
    // The ray tracer's key (a stage without the game's sun: its table sun, the sun its sky shows, the bright
    // side of a daylight sky) holds whatever the actor lights do, so stages lit only by a table sun keep their
    // shadows; GALAXY_RT_KEY=0: none.
    static const bool key_on = read_env_u64("GALAXY_RT_KEY", 1u) != 0u;
    const bool keying = view != nullptr && (mode != 0u || key_on);
    if (view != nullptr) {
        rt_scene_view_ = *view;
    }
    {
        // The player's light rig (LightFunction::loadActorLightInfo: its ambient, lights 0 and 1) as XF colour words.
        GameAreaLight rig{};
        rt_player_rig_known_ = game_area_light(GameAreaLightKind::Player, rig, 2000u);
        const auto word = [](const std::array<float, 3>& rgb) {
            std::uint32_t w = 0u;
            for (unsigned k = 0; k < 3u; ++k) {
                w |= static_cast<std::uint32_t>(std::lround(std::clamp(rgb[k], 0.0f, 1.0f) * 255.0f)) << (24u - 8u * k);
            }
            return w;
        };
        rt_player_rig_ = {word(rig.ambient), word(rig.lights[0].color), word(rig.lights[1].color)};
    }
    // The game's sun holds from its first arrival after a camera cut to the next cut (a stage
    // load cuts): it stops arriving while the game pauses BrightSun, its direction stays.
    const std::uint64_t sun_updates = game_sun_updates();
    if (rt_scene_cut_) {
        if (trace_actor_lights_enabled()) [[unlikely]] {
            std::fprintf(stderr, "[rt-key] cut scene=%u held=%d objects=%u\n", rt_light_serial_,
                         rt_sun_held_ ? 1 : 0, rt_cut_objects_);
        }
        rt_scene_cut_ = false;
        rt_cut_objects_ = 0u;
        rt_sun_held_ = false;
        rt_sun_cut_updates_ = sun_updates;
    } else if (sun_updates != rt_sun_cut_updates_) {
        rt_sun_held_ = true;
    }
    renderer_.rt_hold_sun(rt_sun_held_);
    // Sources: 1 the game's visible sun, 2 the sun its sky shows, 3 a world-fixed light, 4 the
    // bright side of a daylight sky.
    std::uint8_t source = 0u;
    std::array<float, 3> target{};
    std::array<float, 3> sky_color{};
    float sky_strength = 1.0f;
    const RtStageSun* const stage_sun = renderer_.rt_stage_sun();
    if (stage_sun != nullptr && stage_sun->source == RtStageSun::Source::None) {
        // The stage has no sun or sky key (rt-suns.tsv): its own lights carry it.
    } else if (keying && stage_sun != nullptr &&
               (stage_sun->source == RtStageSun::Source::Table || stage_sun->source == RtStageSun::Source::Moon)) {
        // The stage's sun where its sky shows it, or its moon (rt-suns.tsv): lit as the sky's own sun.
        source = 2u;
        target = {stage_sun->direction[0], stage_sun->direction[1], stage_sun->direction[2]};
        rt_light_key_color_ = 0xFFF6E600u;
    } else if (keying && renderer_.rt_sun_direction(target)) {
        source = 1u;
    } else if (keying && mode != 0u && renderer_.rt_sky_sun(target, sky_color)) {
        source = 2u;
        const auto byte = [](float v) { return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
        rt_light_key_color_ = byte(sky_color[0]) << 24u | byte(sky_color[1]) << 16u | byte(sky_color[2]) << 8u;
    } else if (keying && world_key != nullptr) {
        source = 3u;
        target = world_key->direction;
        rt_light_key_color_ = world_key->color;
    } else if (keying && mode != 0u && renderer_.rt_sky_key(target, sky_strength)) {
        source = 4u;
        rt_light_key_color_ = 0xFFF6E600u;   // daylight: warm white
    } else if (keying && mode == 0u) {
        // A stage without the game's sun or a table row: its key is the game's own world-fixed white light
        // (700000 700000 700000, the direction it shades its characters from), decided once per stage: on when
        // the sky detectors find a sun or a daylight sky in its first 120 scenes, fixed in the world from then
        // on, so the key does not swing with the view.
        if (rt_fixed_key_stage_ != rt_stage_hash_) {
            rt_fixed_key_stage_ = rt_stage_hash_;
            rt_fixed_key_on_ = false;
            rt_fixed_key_scenes_ = 0u;
        }
        if (!rt_fixed_key_on_ && rt_fixed_key_scenes_ < 120u) {
            ++rt_fixed_key_scenes_;
            std::array<float, 3> probe{};
            std::array<float, 3> probe_color{};
            float probe_strength = 1.0f;
            rt_fixed_key_on_ = renderer_.rt_sky_sun(probe, probe_color) || renderer_.rt_sky_key(probe, probe_strength);
        }
        if (rt_fixed_key_on_) {
            source = 3u;
            target = {0.57735027f, 0.57735027f, 0.57735027f};
            rt_light_key_color_ = 0xFFF6E600u;
        }
    }
    const bool found = source != 0u;
    const float step = mode == 2u ? 1.0f : 1.0f / kActorLightRampScenes;
    if (found) {
        const float length = std::sqrt(target[0] * target[0] + target[1] * target[1] + target[2] * target[2]);
        for (float& v : target) v /= length > 1e-6f ? length : 1.0f;
        const float turn = rt_light_world_[0] * target[0] + rt_light_world_[1] * target[1] +
                           rt_light_world_[2] * target[2];
        if (rt_light_blend_ <= 0.0f || mode == 2u || turn > 0.99996f) {
            rt_light_world_ = target;
        } else {
            // A changed key: an eighth of the way per scene.
            float moved[3]{};
            float moved_length = 0.0f;
            for (unsigned i = 0; i < 3u; ++i) {
                moved[i] = rt_light_world_[i] + (target[i] - rt_light_world_[i]) * 0.125f;
                moved_length += moved[i] * moved[i];
            }
            moved_length = std::sqrt(moved_length);
            for (unsigned i = 0; i < 3u && moved_length > 1e-6f; ++i) {
                rt_light_world_[i] = moved[i] / moved_length;
            }
        }
        rt_key_source_ = source;
        rt_light_blend_ = std::min(rt_light_blend_ + step, 1.0f);
    } else {
        rt_light_blend_ = std::max(rt_light_blend_ - step, 0.0f);
    }
    if (!keying) {
        rt_light_blend_ = 0.0f;
    }
    const bool enabled = mode == 1u || (mode == 2u && (rt_light_serial_ & 1u) != 0u);
    if (enabled && rt_light_blend_ > 0.0f) {
        rt_light_aim_ = aim_at_key(*view, rt_light_world_);
        rt_light_aim_.blend = rt_light_blend_;
    }
    // The ray tracer's sun when the game shows none: the same key.
    const std::uint32_t c = rt_light_key_color_;
    const std::array<float, 3> key_color{static_cast<float>(c >> 24u) / 255.0f,
                                         static_cast<float>((c >> 16u) & 0xFFu) / 255.0f,
                                         static_cast<float>((c >> 8u) & 0xFFu) / 255.0f};
    renderer_.rt_set_key_light((enabled || (mode == 0u && key_on)) && rt_key_source_ >= 2u && rt_light_blend_ > 0.0f,
                               rt_light_world_, key_color,
                               rt_light_blend_ * (rt_key_source_ == 4u ? sky_strength : 1.0f),
                               rt_key_source_ == 2u || rt_key_source_ == 4u);
    static constexpr const char* kSources[5] = {"none", "sun", "sky", "world", "daylight"};
    if (trace_actor_lights_enabled()) [[unlikely]] {
        // Key changes: its source, or a target more than a degree away.
        static int traced_source = -1;
        static std::array<float, 3> traced_target{};
        const float same = traced_target[0] * target[0] + traced_target[1] * target[1] + traced_target[2] * target[2];
        if (source != traced_source || (found && same < 0.99985f)) {
            std::fprintf(stderr, "[rt-key] change scene=%u source=%s target=(%.3f %.3f %.3f) color=%08x held=%d\n",
                         rt_light_serial_, kSources[source], target[0], target[1], target[2],
                         source >= 2u ? rt_light_key_color_ : 0u, rt_sun_held_ ? 1 : 0);
            traced_source = source;
            traced_target = target;
        }
    }
    if (trace_actor_lights_enabled() && rt_light_serial_ % 240u == 1u) [[unlikely]] {
        for (std::uint32_t i = 0; i < candidate_count; ++i) {
            const RtWorldKey& k = candidates[i];
            const float n = static_cast<float>(std::max(k.ambient_draws, 1u));
            std::fprintf(stderr,
                         "[rt-key] scene=%u candidate %u world=(%.3f %.3f %.3f) color=%08x draws=%u "
                         "ambient=(%.3f %.3f %.3f)/%u%s\n",
                         rt_light_serial_, i, k.direction[0], k.direction[1], k.direction[2], k.color, k.draws,
                         k.ambient[0] / n, k.ambient[1] / n, k.ambient[2] / n, k.ambient_draws,
                         &k == world ? " key" : "");
        }
        std::fprintf(stderr, "[rt-key] scene=%u key=%s world=(%.3f %.3f %.3f) color=%08x blend=%.3f\n",
                     rt_light_serial_, kSources[rt_light_blend_ > 0.0f ? rt_key_source_ : 0u], rt_light_world_[0],
                     rt_light_world_[1], rt_light_world_[2], rt_light_key_color_, rt_light_blend_);
    }
    const bool report =
        actor_light_trace_level() >= 2u || (trace_actor_lights_enabled() && rt_light_serial_ % 600u == 1u);
    if (report) [[unlikely]] {
        // Running totals: snapshots with moved lights, snapshots added for them, TSC cycles of the
        // actor-light work and the timed regions they span, palette snapshots and their cycles
        // (allocation and copy), TSC and clock; `pair`: the mean cycles of one empty timing pair.
        static const std::uint64_t pair = [] {
            std::uint64_t total = 0u;
            for (unsigned i = 0; i < 4096u; ++i) {
                const std::uint64_t a = trace_cycles();
                total += trace_cycles() - a;
            }
            return total / 4096u;
        }();
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch());
        std::fprintf(stderr,
                     "[rt-actor-light] scene=%u aim=%s key=(%.3f %.3f %.3f) moved=%llu added=%llu cycles=%llu "
                     "regions=%llu pair=%llu snapshots=%llu snapshot-cycles=%llu tsc=%llu ns=%lld blend=%.3f\n",
                     rt_light_serial_, kSources[rt_light_aim_.valid ? rt_key_source_ : 0u],
                     rt_light_aim_.key[0], rt_light_aim_.key[1], rt_light_aim_.key[2],
                     static_cast<unsigned long long>(rt_light_uploads_),
                     static_cast<unsigned long long>(rt_light_forced_),
                     static_cast<unsigned long long>(rt_light_cycles_),
                     static_cast<unsigned long long>(rt_light_regions_), static_cast<unsigned long long>(pair),
                     static_cast<unsigned long long>(rt_snapshot_count_),
                     static_cast<unsigned long long>(rt_snapshot_cycles_),
                     static_cast<unsigned long long>(trace_cycles()),
                     static_cast<long long>(ns.count()), rt_light_blend_);
    }
}

// A draw lit by the game's player light rig: its channel 0 ambient and lights 0 and 1 hold the rig's colours. The
// rig's hue is the game's character grading (the prologue's night: a cyan key, a blue fill and a blue ambient on
// Mario); under physical light the traced light gives the colour, the rig its level only (shader_gen, fill_rt_tev).
bool GxBackend::rt_player_rig_draw() const noexcept {
    if (!rt_player_rig_known_ || state_.gen_mode().num_color_chans == 0u) {
        return false;
    }
    const XfChannelCtrl color = state_.channel_ctrl(0u);
    if (!color.lighting_enable || (color.light_mask & 3u) != 3u) {
        return false;
    }
    const auto rgb = [this](std::uint16_t address) { return state_.xf(address) & 0xFFFFFF00u; };
    return rgb(xf::kAmbientColorBase) == rt_player_rig_[0] &&
           rgb(static_cast<std::uint16_t>(xf::kLightsBase + 3u)) == rt_player_rig_[1] &&
           rgb(static_cast<std::uint16_t>(xf::kLightsBase + 16u + 3u)) == rt_player_rig_[2];
}

// The lights a lit scene draw has moved: its follow-camera lights 0 and 1 that still hold the
// positions their load gave them.
std::uint32_t GxBackend::rt_aimed_lights() const noexcept {
    if (!rt_light_aim_.valid || !scene_phase_ || side_layer_ != kSceneLayer) {
        return 0u;
    }
    std::uint32_t lights = 0u;
    for (unsigned n = 0; n < 2u; ++n) {
        const std::uint32_t* words = state_.xf_raw() + xf::kLightsBase + kXfLightWords * n + kXfLightPosition;
        if ((rt_follow_lights_ & (1u << n)) != 0u &&
            std::memcmp(words, rt_light_words_[n].data(), sizeof(rt_light_words_[n])) == 0) {
            lights |= 1u << n;
        }
    }
    return lights != 0u ? (rt_light_serial_ << 8u) | lights : 0u;
}

// Moves the flushed draw's lights in a palette snapshot (write-only: the upload is write-combined
// memory).
void GxBackend::write_rt_lights(std::uint32_t* palette) noexcept {
    if (!rt_light_aim_.valid) {
        return;
    }
    const std::uint64_t start = trace_actor_lights_enabled() ? trace_cycles() : 0u;
    aim_actor_lights(palette, state_.xf_raw(), static_cast<std::uint8_t>(rt_draw_lights_ & 0x01u),
                     static_cast<std::uint8_t>(rt_draw_lights_ & 0x02u), rt_light_aim_);
    if (start != 0u) [[unlikely]] {
        rt_light_cycles_ += trace_cycles() - start;
        ++rt_light_regions_;
    }
}

// A lit scene draw's world-fixed GX light 0 (loaded by an ActorLightInfo or coin without the
// follow-camera flag, still holding that load's position, far enough to be a direction): a key
// light candidate, by its world direction from the camera and its colour.
void GxBackend::note_rt_world_key() noexcept {
    if ((rt_known_lights_ & ~rt_follow_lights_ & 1u) == 0u || !scene_phase_ || side_layer_ != kSceneLayer) {
        return;
    }
    const XfChannelCtrl channel = state_.channel_ctrl(0u);
    if (!channel.lighting_enable || (channel.light_mask & 1u) == 0u) {
        return;
    }
    const std::uint32_t* light = state_.xf_raw() + xf::kLightsBase;
    if (std::memcmp(light + kXfLightPosition, rt_light_words_[0].data(), sizeof(rt_light_words_[0])) != 0) {
        return;
    }
    const float p[3]{std::bit_cast<float>(light[kXfLightPosition]), std::bit_cast<float>(light[kXfLightPosition + 1u]),
                     std::bit_cast<float>(light[kXfLightPosition + 2u])};
    const float distance2 = p[0] * p[0] + p[1] * p[1] + p[2] * p[2];
    if (!(distance2 > kActorLightFar * kActorLightFar) || !std::isfinite(distance2)) {
        return;
    }
    // Eye to world: the transposed rotation of the scene camera.
    const std::array<float, 12>& v = rt_scene_view_;
    const float inverse = 1.0f / std::sqrt(distance2);
    const std::array<float, 3> direction{(v[0] * p[0] + v[4] * p[1] + v[8] * p[2]) * inverse,
                                         (v[1] * p[0] + v[5] * p[1] + v[9] * p[2]) * inverse,
                                         (v[2] * p[0] + v[6] * p[1] + v[10] * p[2]) * inverse};
    const std::uint32_t color = light[3];
    RtWorldKey* found = nullptr;
    for (std::uint32_t i = 0; i < rt_world_key_count_ && found == nullptr; ++i) {
        RtWorldKey& key = rt_world_keys_[i];
        if (key.color == color && key.direction[0] * direction[0] + key.direction[1] * direction[1] +
                                          key.direction[2] * direction[2] > 0.9995f) {
            found = &key;
        }
    }
    if (found == nullptr) {
        if (rt_world_key_count_ == rt_world_keys_.size()) {
            return;
        }
        found = &rt_world_keys_[rt_world_key_count_++];
        *found = {direction, color};
    }
    ++found->draws;
    if (!channel.ambient_from_vertex) {
        const std::uint32_t a = state_.xf(xf::kAmbientColorBase);
        found->ambient[0] += static_cast<float>(a >> 24u) / 255.0f;
        found->ambient[1] += static_cast<float>((a >> 16u) & 0xFFu) / 255.0f;
        found->ambient[2] += static_cast<float>((a >> 8u) & 0xFFu) / 255.0f;
        ++found->ambient_draws;
    }
}

// SceneFunction phases run opaque first (0, 1, the shadows 6 and 7, then 2 and 4), then the
// translucent lists (3, 5), indirect (8) and image effects (9).
void GxBackend::note_rt_phase(std::uint8_t phase) {
    if (!scene_phase_) {
        return;
    }
    rt_phase_ = phase;
    using motion::ScenePhase;
    if (phase == static_cast<std::uint8_t>(ScenePhase::Xlu) ||
        phase == static_cast<std::uint8_t>(ScenePhase::ListXlu) ||
        phase == static_cast<std::uint8_t>(ScenePhase::AfterIndirect) ||
        phase == static_cast<std::uint8_t>(ScenePhase::ImageEffect)) {
        trace_rt_scene();
        apply_scene_ao();
    }
    if (phase == static_cast<std::uint8_t>(ScenePhase::ImageEffect)) {
        trace_rt_water();
    }
}

// The water drawn since the trace reflects, before the image effects (or at the scene's end).
void GxBackend::trace_rt_water() {
    if (renderer_.rt_log_seq()) {
        std::fprintf(stderr, "[rt-seq] backend water request open=%d phase=%u\n", renderer_.rt_water_open() ? 1 : 0,
                     static_cast<unsigned>(rt_phase_));
    }
    if (!renderer_.rt_water_open()) {
        return;
    }
    flush_pending_draw_batch();
    renderer_.rt_trace_water();
}

void GxBackend::trace_rt_scene() {
    if (rt_scene_traced_) {
        return;
    }
    rt_scene_traced_ = true;
    flush_pending_draw_batch();
    renderer_.rt_trace_scene();
}

void GxBackend::on_fifo_marker(FifoMarker marker, const std::array<float, 12>* view, std::uint32_t sky_up,
                               bool sky_up_known, const SceneStage* stage) {
    flush_pending_draw_batch();
    if (marker == FifoMarker::SceneBegin) {
        end_side_layer();
        // A frame recorded as screen-only (its frame before drew no scene) draws one after all: the scene's record
        // replaces it.
        frame_scene_seen_ = true;
        if (screen_frame_open_) {
            cancel_screen_frame();
        }
    }
    scene_phase_ = marker == FifoMarker::SceneBegin;
    if (marker == FifoMarker::SceneBegin) {
        ao_view_set_ = false;
        ao_scene_applied_ = !ambient_occlusion_;
        // The game names its stage: a new one starts the tracer's stage state over.
        if (stage != nullptr && (*stage)[0] != 0 && (!scene_stage_known_ || *stage != scene_stage_)) {
            scene_stage_ = *stage;
            scene_stage_known_ = true;
            begin_rt_stage();
        }
        // The file select stays the game's own picture: there is no stage light or world to trace, and its menu
        // lights colour the heads.
        {
            static constexpr char kFileSelect[] = "FileSelect";
            renderer_.rt_set_scene_untraced(scene_stage_known_ &&
                                            std::strncmp(scene_stage_.data(), kFileSelect, sizeof(kFileSelect)) == 0);
        }
        // The enhanced sky's up for the scene's draws (the view's up without a player).
        const std::uint32_t known = sky_up_known ? 1u : 0u;
        if (cpu_vs_constants_.sky_up != sky_up || cpu_vs_constants_.sky_up_known != known) {
            cpu_vs_constants_.sky_up = sky_up;
            cpu_vs_constants_.sky_up_known = known;
            sky_up_dirty_ = true;
        }
        // The same up for the ray tracer's physical camera (the ground under the player).
        {
            float up[3] = {0.0f, 1.0f, 0.0f};
            if (sky_up_known) {
                decode_octahedral(sky_up, up);
            }
            renderer_.rt_set_scene_up({up[0], up[1], up[2]});
        }
        rt_mirror_pass_ = false;
        rt_trace_only_ = false;
        renderer_.set_scene_view(view);
        // GALAXY_RT_AB: the game's own picture on odd presents (the snapshots' numbering).
        renderer_.rt_set_ab_original(((g_snapshot_presents + 1u) & 1u) != 0u);
        renderer_.rt_set_presents(g_snapshot_presents + 1u);
        rt_phase_ = 0xFFu;
        rt_view_set_ = false;
        rt_scene_traced_ = !renderer_.rt_active() || renderer_.rt_scene_untraced();
        rt_sky_buffer_ = false;
        rt_sun_buffer_ = false;
        rt_fake_shadow_list_ = false;
        if (!rt_scene_traced_) {
            renderer_.rt_begin_scene();
            renderer_.rt_begin_sky();
            renderer_.rt_set_camera(view);
        }
        // GALAXY_RT_AB: the game's own frames keep the aim's state and leave the lights the game's.
        aim_rt_scene_lights(renderer_.rt_lighting() || renderer_.rt_ab_original() ? view : nullptr);
        if (renderer_.rt_ab_original()) {
            rt_light_aim_ = {};
        }
        // This scene's ray-tracing state for the enhanced pipelines (and the de-lit draws: under
        // traced lighting every lit and environment-mapped scene draw takes the enhanced path).
        enhanced_ps_flags_ = (enhanced_ps_flags_ & ~(kEnhancedRtReflections | kEnhancedRtLighting | kEnhancedRtPhysical)) |
                             (renderer_.rt_reflections_active() ? kEnhancedRtReflections : 0u) |
                             (renderer_.rt_lighting() ? kEnhancedRtLighting : 0u) |
                             (renderer_.rt_physical() ? kEnhancedRtPhysical : 0u);
    } else {
        {
            if (renderer_.rt_log_seq()) {
                std::fprintf(stderr, "[rt-seq] scene-end layer=%u scene-phase=%d traced=%d water-open=%d\n",
                             side_layer_, scene_phase_ ? 1 : 0, rt_scene_traced_ ? 1 : 0,
                             renderer_.rt_water_open() ? 1 : 0);
            }
        }
        if (side_layer_ == kMirrorLayer) {
            end_side_layer();
        }
        trace_rt_scene();
        apply_scene_ao();
        if (renderer_.rt_active() && side_layer_ == kSceneLayer) {
            trace_rt_water();
            renderer_.rt_capture_picture();
        }
        // The camera (in rt_capture_picture with the lighting; else here) brings the RGBA8 EFB back.
        renderer_.end_hdr_scene();
    }
    const bool was_temporal = temporal_scene_;
    const bool was_motion = motion_scene_;
    if (marker == FifoMarker::SceneBegin) {
        temporal_scene_ = renderer_.scene_temporal_begin(
            scene_jitter_px_[0], scene_jitter_px_[1]);
        // A temporal scene writes the motion, and so does one the ray tracing engine lights (its histories follow
        // moving surfaces by it) without a temporal upscaler: unjittered, the transform history behind it.
        motion_scene_ = renderer_.begin_motion_scene();
        scene_camera_captured_ = false;
        history_scene_ = temporal_scene_ || motion_scene_ || interp_recording_;
        if (interp_recording_) {
            renderer_.note_replay_scene();
            if (!std::exchange(mirror_opened_scene_, false)) {
                open_interp_scene();
            }
            interp_scenes_[interp_scene_].layers[kSceneLayer].first = interp_scenes_[interp_scene_].constants.size();
            scene_3d_recorded_ = true;
            ++interp_scenes_in_chunk_;
            // A fresh palette shadow for the scene's first palette draw.
            matrix_palette_stale_ = true;
            current_palette_shadow_ = -1;
            current_replay_record_ = -1;
            current_ps_record_ = -1;
            ps_record_restart_ = true;
        }
        if (history_scene_) {
            if (transform_history_.current().capacity() == 0u) {
                transform_history_.reserve(8192u);
            }
            Affine34 camera{};
            if (view != nullptr) {
                for (unsigned i = 0; i < 12u; ++i) camera[i] = (*view)[i];
            }
            transform_history_.begin_scene(renderer_.matrix_segment_serial(),
                                           view != nullptr ? &camera : nullptr);
            begin_identity_scene();
            state_.set_matrix_load_tracking(true);
            if (interp_recording_) {
                InterpScene& scene = interp_scenes_[interp_scene_];
                scene.camera = transform_history_.camera_delta();
                scene.view = camera;
                scene.view_known = view != nullptr;
            }
        } else {
            transform_history_.reset();
            state_.set_matrix_load_tracking(false);
        }
    } else {
        if (history_scene_) {
            transform_history_.end_scene();
            if (interp_recording_ && interp_scenes_[interp_scene_].valid) {
                end_record_layer(kSceneLayer);
            }
            const TransformHistoryStats stats = transform_history_.stats();
            identity_records_ += stats.identified;
            identity_matched_ += stats.identity_matched;
            identity_rejected_ += stats.identity_rejected;
            identity_scene_records_ += stats.records;
            identity_signature_rejected_ += stats.signature_rejected;
            identity_continuity_rejected_ += stats.continuity_rejected;
            for (std::size_t i = 0; i < particle_age_steps_.size(); ++i) {
                particle_age_steps_[i] += stats.particle_age_steps[i];
            }
            // A cut: most transforms have no previous counterpart.
            if (temporal_scene_ && stats.records >= 16u && stats.matched * 2u < stats.records) {
                renderer_.reset_temporal_history();
            }
        }
        temporal_scene_ = false;
        motion_scene_ = false;
        history_scene_ = false;
        current_replay_record_ = -1;
        current_ps_record_ = -1;
        state_.set_matrix_load_tracking(false);
    }
    // Projections take (or drop) the scene jitter, and scene draws switch
    // palette addressing, from the next draw on.
    if (was_temporal || was_motion || temporal_scene_ || motion_scene_ || interp_recording_) {
        frame_bindings_dirty_ = true;
    }
    // Motion keys switch on or off: resolve the pipeline again.
    if (was_motion != motion_scene_) {
        current_pipeline_ = nullptr;
    }
    // Scene end: the 2D layer renders into the output-resolution EFB. A new
    // scene that begins before a display copy restores the render EFB.
    bool switched = false;
    display_copy_on_output_ = false;
    if (marker == FifoMarker::SceneEnd) {
        renderer_.record_scene_aa();
        switched = renderer_.begin_output_efb();
    } else if (renderer_.output_efb_active()) {
        renderer_.end_output_efb();
        switched = true;
    }
    if (switched) {
        efb_copies_.set_efb_scale(renderer_.efb_scale());
        frame_bindings_dirty_ = true;
    }
    if (marker == FifoMarker::SceneEnd) {
        s_layer_2d_scale.store(renderer_.efb_scale(), std::memory_order_relaxed);
        begin_side_layer(kScreenLayer);
    }
    if (marker == FifoMarker::SceneBegin) {
        s_scene_display_scale.store(renderer_.scene_display_scale(), std::memory_order_relaxed);
    }
    // While ray tracing runs the scene layer draws into the HDR scene EFB (renderer_d3d12 begin_hdr_scene): from here,
    // with the render EFB back.
    if (marker == FifoMarker::SceneBegin && renderer_.rt_active()) {
        renderer_.begin_hdr_scene();
    }
}

void GxBackend::take_attached_pointer_depth_capture(FrameChunk& chunk, std::size_t fifo_size) {
    if (!producer_pointer_depth_capture_) return;
    if (producer_pointer_depth_capture_offset_ <= fifo_size) {
        chunk.pointer_depth_capture = std::move(producer_pointer_depth_capture_);
        chunk.pointer_depth_capture_fifo_offset = producer_pointer_depth_capture_offset_;
        chunk.pointer_depth_in_frame = true;
    } else {
        producer_pointer_depth_capture_->publish_complete();
    }
    producer_pointer_depth_capture_.reset();
    producer_pointer_depth_capture_offset_ = 0;
}

// Render thread, inside the frame's parse: the field joins this command list.
void GxBackend::record_pointer_depth_capture_in_frame(FrameChunk& chunk) {
    auto capture = std::move(chunk.pointer_depth_capture);
    chunk.pointer_depth_capture.reset();
    chunk.pointer_depth_in_frame = false;
    if (!capture) return;
    const auto start = std::chrono::steady_clock::now();
    bool recorded = false;
    try {
        recorded = renderer_.record_pointer_depth_capture_in_frame();
    } catch (...) {
        capture->error = std::current_exception();
    }
    if (!recorded) {
        capture->success = false;
        capture->publish_complete();
        return;
    }
    const std::lock_guard<std::mutex> lock(queue_mutex_);
    // The fence is assigned at end_frame; adopt_pointer_depth_fence_if_needed
    // copies it before any wait or completion poll.
    pending_pointer_depth_capture_ = {std::move(capture), 0u, start};
}

void GxBackend::adopt_pointer_depth_fence_if_needed() {
    if (pending_pointer_depth_capture_.capture &&
        pending_pointer_depth_capture_.fence_value == 0u) {
        pending_pointer_depth_capture_.fence_value = renderer_.pointer_depth_capture_fence();
    }
}

bool GxBackend::peek_efb(
    std::uint16_t x,
    std::uint16_t y,
    EfbPeekKind kind,
    std::uint32_t& value) {
    if (!initialized_) {
        return false;
    }

    EfbPeekRequest request;
    request.x = x;
    request.y = y;
    request.kind = kind;

    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (render_thread_error_ != nullptr) {
            std::exception_ptr error = render_thread_error_;
            render_thread_error_ = nullptr;
            std::rethrow_exception(error);
        }
        if (stop_render_thread_) {
            return false;
        }

        FrameChunk chunk;
        chunk.efb_peek_only = true;
        chunk.efb_peek_request = &request;
        frame_queue_.push_back(std::move(chunk));
    }
    queue_cv_.notify_one();

    {
        const GameWaitTimer game_wait{game_wait_qpc_};
        std::unique_lock<std::mutex> lock(request.mutex);
        request.cv.wait(lock, [&request] { return request.done; });
    }
    if (request.error != nullptr) {
        std::rethrow_exception(request.error);
    }
    value = request.value;
    return request.success;
}

// A display copy completed a game frame's log: its scene (the one recording
// when the copy landed) is the one to interpolate when its present arrives.
void GxBackend::note_interp_log_completion() {
    const std::uint64_t serial = renderer_.replay_serial();
    if (serial == interp_seen_serial_) {
        return;
    }
    interp_seen_serial_ = serial;
    if (!renderer_.replay_available()) {
        return;
    }
    interp_log_scene_ = interp_scene_;
    interp_scenes_[interp_log_scene_].serial = serial;
    interp_log_waiting_ = interp_scenes_[interp_log_scene_].valid &&
        !native_settings_overlay_visible();
    interp_log_xfb_ = renderer_.replay_xfb_addr();
}

void GxBackend::run_pending_interp_start() {
    if (!interp_start_pending_) {
        return;
    }
    interp_start_pending_ = false;
    if (interp_sequence_.active) {
        supersede_interp_sequence();
    }
    if (interp_log_waiting_ && renderer_.replay_available()) {
        interp_log_waiting_ = false;
        start_interp_sequence(interp_log_xfb_);
        advance_interp_sequence(false);
    }
}

void GxBackend::start_interp_sequence(std::uint32_t xfb_addr) {
    static const std::int64_t frequency = galaxy::qpc_frequency();
    const std::int64_t now = galaxy::qpc_now();
    if (!renderer_.start_replay_playback()) {
        return;
    }
    const RenderConfig cfg = get_render_config();
    double rate = std::max(61.0, present_frame_rate(
        presented_max_fps(cfg), cfg.vsync_interval, renderer_.display_refresh_hz()));
    // VSync with the display's vblanks timed: every slot is a vblank (every n-th for a rate below the refresh, kept
    // above 60), so each picture's moment is the one its vblank shows. The game's 60 Hz is not locked to the
    // display, so content time follows the slots (no lead).
    std::int64_t vblank = 0;
    std::int64_t vblank_period = 0;
    const bool on_vblanks =
        cfg.vsync_interval != 0u && renderer_.vblank_grid(vblank, vblank_period);
    std::int64_t vblanks_per_slot = 1;
    const auto fit_rate_to_vblanks = [&] {
        const double refresh = static_cast<double>(frequency) / static_cast<double>(vblank_period);
        vblanks_per_slot = galaxy::gx::vblanks_per_slot(refresh, rate);
        rate = refresh / static_cast<double>(vblanks_per_slot);
    };
    if (on_vblanks) {
        fit_rate_to_vblanks();
    }
    ++interp_sequence_serial_;
    ++interp_started_;
    // Logs completed but never played (a newer one replaced them first).
    const std::uint64_t played = renderer_.playing_serial();
    if (interp_last_played_serial_ != 0u && played > interp_last_played_serial_ + 1u &&
        played - interp_last_played_serial_ < 8u) {
        interp_logs_skipped_ += played - interp_last_played_serial_ - 1u;
    }
    interp_last_played_serial_ = played;
    note_game_frame_cadence(now);
    interp_sequence_.active = true;
    interp_sequence_.flush_requested = false;
    interp_sequence_.classified = false;
    interp_sequence_.scene = interp_log_scene_;
    select_interp_before();
    interp_sequence_.xfb_addr = xfb_addr;
    interp_sequence_.subframes = interp_gpu_governor(rate);
    if (on_vblanks) {
        fit_rate_to_vblanks();  // a rate the governors lowered
    }
    interp_sequence_.next = 0u;
    // Present slots run on one clock at the present rate. A slot shows the
    // game at t = (slot - arrival) * 60 + lead between the previous frame (0)
    // and this one (1); the first slot with t >= 1 presents this frame
    // itself. Integer multiples of 60 lock the clock to each arrival with
    // lead 60 / rate: k sub-frames at i / (k + 1), then the frame, on time.
    // Other rates keep a free-running clock with lead 0: content time is
    // then linear in present time (the next arrival ends the sequence at
    // t -> 1), where a lead would show the frame up to one slot early and
    // step the content unevenly around it. A clock left behind by a pause
    // restarts.
    interp_slot_period_qpc_ = on_vblanks ? vblanks_per_slot * vblank_period
                                         : static_cast<std::int64_t>(static_cast<double>(frequency) / rate);
    const bool locked = !on_vblanks && std::abs(rate / 60.0 - std::round(rate / 60.0)) < 0.02;
    // The arrival clock: the render thread sees a frame's present when it
    // reaches the queue (a chunk in progress delays it by milliseconds), so
    // arrivals are the game's frame cadence, smoothed: the prediction one or
    // more frame periods on, pulled 1/8 toward the measured arrival, with the
    // period tracking the game's rate. A miss by a third of a period or more
    // (pause, hitch) restarts it at the measured time.
    if (interp_frame_period_qpc_ == 0) {
        interp_frame_period_qpc_ = frequency / 60;
    }
    std::int64_t arrival = now;
    if (interp_arrival_qpc_ != 0 && now > interp_arrival_qpc_) {
        const std::int64_t periods = std::max<std::int64_t>(1,
            (now - interp_arrival_qpc_ + interp_frame_period_qpc_ / 2) /
                interp_frame_period_qpc_);
        const std::int64_t predicted = interp_arrival_qpc_ + periods * interp_frame_period_qpc_;
        const std::int64_t error = now - predicted;
        if (periods <= 4 && std::abs(error) < interp_frame_period_qpc_ / 3) {
            arrival = predicted + error / 8;
            interp_frame_period_qpc_ = std::clamp<std::int64_t>(
                interp_frame_period_qpc_ + error / (64 * periods), frequency / 62, frequency / 58);
        }
    }
    interp_arrival_qpc_ = arrival;
    interp_sequence_.frame_qpc = arrival;
    interp_sequence_.lead = locked ? 60.0 / rate : 0.0;
    if (on_vblanks) {
        // The next slot moves to the vblank nearest where it was due, so the clock follows the display without
        // adding or dropping a slot; a clock not running starts at the vblank nearest now.
        const std::int64_t due = interp_slot_due_qpc_ != 0 &&
                std::llabs(interp_slot_due_qpc_ - now) <= interp_slot_period_qpc_
            ? interp_slot_due_qpc_ : now;
        interp_slot_due_qpc_ = vblank + std::llround(static_cast<double>(due - vblank) /
                                                     static_cast<double>(vblank_period)) * vblank_period;
    } else if (locked || interp_slot_due_qpc_ == 0 ||
        now - interp_slot_due_qpc_ > interp_slot_period_qpc_ ||
        interp_slot_due_qpc_ - now > interp_slot_period_qpc_) {
        // Locked rates start the slots at the (smoothed) arrival.
        interp_slot_due_qpc_ = locked && arrival <= now + interp_slot_period_qpc_
            ? arrival : now;
    }
}

// When the scene before the starting one was never on screen (its log was
// replaced before a present started it, or its sequence ended before showing
// anything), the sub-frames start from the scene shown last, records matched
// through the skipped scenes. Vertex blending needs consecutive logs
// (prepare_vertex_blend) and stays off for such a sequence.
void GxBackend::select_interp_before() {
    const unsigned scene = interp_sequence_.scene;
    interp_sequence_.before = (scene + 3u) % 4u;
    interp_sequence_.chained = false;
    if (interp_shown_scene_ < 0 ||
        static_cast<unsigned>(interp_shown_scene_) == interp_sequence_.before ||
        static_cast<unsigned>(interp_shown_scene_) == scene) {
        return;
    }
    const unsigned shown = static_cast<unsigned>(interp_shown_scene_);
    const unsigned depth = (scene + 4u - shown) % 4u;  // 2 or 3 scenes back
    const InterpScene& target = interp_scenes_[scene];
    bool consecutive = target.valid && target.serial != 0u;
    for (unsigned step = 1u; consecutive && step <= depth; ++step) {
        const InterpScene& older = interp_scenes_[(scene + 4u - step) % 4u];
        consecutive = older.valid && older.serial + step == target.serial;
    }
    if (!consecutive) {
        ++interp_unchained_skips_;
        return;
    }
    interp_chain_prev_ = target.prev_index;
    for (unsigned step = 1u; step < depth; ++step) {
        const std::vector<std::int32_t>& through = interp_scenes_[(scene + 4u - step) % 4u].prev_index;
        for (std::int32_t& m : interp_chain_prev_) {
            m = m >= 0 && static_cast<std::size_t>(m) < through.size()
                ? through[static_cast<std::size_t>(m)] : -1;
        }
    }
    interp_sequence_.before = shown;
    interp_sequence_.chained = true;
    ++interp_chained_sequences_;
}

// As many sub-frames per game frame as the GPU fits in 15 ms (of the 16.7 ms frame). A sub-frame
// replays the whole frame, so a game frame costs about (k + 1) sub-frame times. k drops at once
// when over budget and rises to the next higher present rate that fits after 10 s under it; with
// none left, one is tried again every 20 s. A capped sequence runs the slot clock at 60 * (k + 1).
unsigned GxBackend::interp_gpu_governor(double& rate) {
    constexpr double budget_ms = 15.0;
    const unsigned configured = interp_stopping_ ? 0u : interp_frames_;
    if (configured == 0u) {
        return configured;
    }
    // GALAXY_TEST_INTERP_SUBFRAMES=<k>: every sequence takes k sub-frames (at most the configured count), whatever the
    // GPU's and the cadence's governors would allow: what a capped rate shows, held for a test.
    static const std::uint64_t forced = read_env_u64("GALAXY_TEST_INTERP_SUBFRAMES", 0u);
    if (forced != 0u) [[unlikely]] {
        const auto subframes = static_cast<unsigned>(std::min<std::uint64_t>(forced, configured));
        if (subframes < configured) {
            rate = 60.0 * static_cast<double>(subframes + 1u);
        }
        return subframes;
    }
    const unsigned allowed = std::min(configured, interp_cadence_cap_);
    const std::uint64_t serial = interp_sequence_serial_;
    if (serial - interp_gpu_checked_ >= 30u && serial >= interp_gpu_warm_until_) {
        interp_gpu_checked_ = serial;
        const double sub_ms = renderer_.subframe_gpu_ema_ms();
        if (sub_ms > 0.0) {
            // With the temporal upscaler on the replays the frame at t = 1 is
            // one too; when no sub-frame fits beside it, the game frames take
            // the upscaler instead (the same frames for less GPU time).
            const bool upscaled = renderer_.temporal_interpolation();
            const double fit = std::floor(budget_ms / sub_ms) - (upscaled ? 2.0 : 1.0);
            if (upscaled && fit < 1.0) {
                renderer_.suspend_temporal_interpolation(sub_ms);
            }
            const unsigned fits = fit < 1.0 ? 0u : static_cast<unsigned>(std::min(fit, 7.0));
            if (fits < interp_gpu_cap_) {
                interp_gpu_cap_ = fits;
                interp_gpu_cap_serial_ = serial;
            } else if (fits > interp_gpu_cap_ && serial - interp_gpu_cap_serial_ >= 600u) {
                interp_gpu_cap_ = std::min(fits, interp_step_up(interp_gpu_cap_));
                interp_gpu_cap_serial_ = serial;
            }
        }
        if (interp_gpu_cap_ == 0u && serial - interp_gpu_cap_serial_ >= 1200u) {
            interp_gpu_cap_ = 1u;  // measure one sub-frame again
            interp_gpu_cap_serial_ = serial;
            renderer_.reset_subframe_gpu_ema();
        }
    }
    const unsigned subframes = std::min(allowed, interp_gpu_cap_);
    if (subframes >= configured) {
        return configured;
    }
    ++interp_gpu_capped_;
    rate = 60.0 * static_cast<double>(subframes + 1u);
    return subframes;
}

// Sub-frames must never cost the game its own frames. Each game frame shown is timed against the VI period: an
// interval above 1.5 periods missed VIs. A miss is the sub-frames' when the interval would not have missed without
// their render-thread time: the game waited on the render thread, and would have waited that much less (at most what
// it waited). The rest are the game's own, as a scene's first frames, whose decoding, uploads and pipelines keep the
// render thread far longer than sub-frames do. Two or more of the sub-frames' misses a second, and no fewer than the
// game's own, take sub-frames away to the next lower present rate (to half of them at eight). The game's own misses
// three seconds in a row above their lowest rate are probed, for threads that share a CPU's cores: the next lower
// rate for a second, kept only if the misses halve. A try of the next higher rate is undone at its first miss of the
// sub-frames'. Tries and probes follow a clean interval that doubles after each failed one (2 s to 64 s: a scene's
// first frames back the render thread up for a second or two) and halves after a try that held.
void GxBackend::restart_interp_governors() {
    const std::int64_t now = galaxy::qpc_now();
    const std::int64_t frequency = galaxy::qpc_frequency();
    interp_gpu_cap_ = 7u;
    interp_gpu_cap_serial_ = interp_sequence_serial_;
    interp_gpu_checked_ = interp_sequence_serial_;
    interp_gpu_warm_until_ = interp_sequence_serial_ + 60u;
    renderer_.reset_subframe_gpu_ema();
    interp_cadence_cap_ = 7u;
    interp_cadence_interval_s_ = 2u;
    interp_cadence_try_qpc_ = 0;
    interp_cadence_change_qpc_ = now;
    interp_cadence_last_qpc_ = 0;
    interp_cadence_grace_until_qpc_ = now + frequency * 3 / 2;
    interp_cadence_own_windows_ = 0u;
    interp_cadence_probe_ = false;
    interp_cadence_probe_after_qpc_ = now;
}

void GxBackend::note_game_frame_cadence(std::int64_t now_qpc) {
    static const std::int64_t frequency = galaxy::qpc_frequency();
    static const bool trace = read_env_u64("GALAXY_TRACE_PRESENT_STATS", 0u) != 0u;
    const std::int64_t period = frequency * 1001 / 60000;
    // Since the frame before: the game's waits on the render thread, and the render thread's sub-frame time.
    const std::uint64_t waited_total = game_wait_qpc_.load(std::memory_order_relaxed);
    const auto waited = static_cast<std::int64_t>(waited_total - interp_cadence_wait_seen_);
    const auto subframe_time = static_cast<std::int64_t>(interp_subframe_qpc_ - interp_cadence_subframe_seen_);
    interp_cadence_wait_seen_ = waited_total;
    interp_cadence_subframe_seen_ = interp_subframe_qpc_;
    const auto restart_window = [&] {
        interp_cadence_window_qpc_ = now_qpc;
        interp_cadence_frames_ = 0u;
        interp_cadence_misses_ = 0u;
        interp_cadence_own_ = 0u;
        interp_cadence_wait_qpc_ = 0u;
    };
    if (interp_cadence_last_qpc_ == 0 || now_qpc - interp_cadence_last_qpc_ > 4 * frequency) {
        // First frame, or a pause (menu, load): a new window.
        interp_cadence_last_qpc_ = now_qpc;
        restart_window();
        return;
    }
    const std::int64_t interval = now_qpc - interp_cadence_last_qpc_;
    interp_cadence_last_qpc_ = now_qpc;
    if (now_qpc < interp_cadence_grace_until_qpc_) {
        // Just after a rate or upscaler change: its one-time work is no measure.
        restart_window();
        return;
    }
    ++interp_cadence_frames_;
    interp_cadence_wait_qpc_ += static_cast<std::uint64_t>(waited);
    const auto missed_in = [period](std::int64_t span) {
        return span > period * 3 / 2 ? static_cast<std::uint32_t>((span + period / 2) / period) - 1u : 0u;
    };
    const std::uint32_t missed = missed_in(interval);
    const std::uint32_t own = missed_in(interval - std::min(waited, subframe_time));
    interp_cadence_misses_ += missed - own;
    interp_cadence_own_ += own;
    const unsigned running = std::min({interp_frames_, interp_cadence_cap_, interp_gpu_cap_});
    const bool trying = interp_cadence_try_qpc_ != 0 && now_qpc - interp_cadence_try_qpc_ < 4 * frequency;
    const auto drop = [&](unsigned cap, const char* why) {
        if (trace) {
            std::cerr << "[interp-cadence] " << why << ' ' << running << "->" << cap << '\n';
        }
        if (trying) {
            interp_cadence_interval_s_ = std::min(interp_cadence_interval_s_ * 2u, 64u);
        }
        // The replay at t = 1 alone costs the game its frames when it takes a
        // quarter of a frame or more.
        const double replay_ms = renderer_.subframe_gpu_ema_ms();
        if (cap == 0u && renderer_.temporal_interpolation() && replay_ms * 4.0 >= 1000.0 / 60.0) {
            renderer_.suspend_temporal_interpolation(replay_ms);
        }
        interp_cadence_cap_ = cap;
        interp_cadence_change_qpc_ = now_qpc;
        interp_cadence_try_qpc_ = 0;
        ++interp_cadence_drops_;
    };
    if (trying && missed != own && running > 0u) {
        drop(interp_step_down(running), "try undone");
        return;
    }
    const std::int64_t elapsed = now_qpc - interp_cadence_window_qpc_;
    if (elapsed < frequency) {
        return;
    }
    const double seconds = static_cast<double>(elapsed) / static_cast<double>(frequency);
    interp_cadence_fps_ = static_cast<double>(interp_cadence_frames_) / seconds;
    const double theirs = static_cast<double>(interp_cadence_misses_) / seconds;
    const double own_rate = static_cast<double>(interp_cadence_own_) / seconds;
    if (trace && interp_cadence_misses_ + interp_cadence_own_ != 0u) {
        std::cerr << "[interp-cadence] window sub-frame-misses=" << interp_cadence_misses_
                  << " own-misses=" << interp_cadence_own_ << " running=" << running << " game-wait-ms="
                  << static_cast<double>(interp_cadence_wait_qpc_) * 1000.0 / static_cast<double>(frequency)
                  << " seconds=" << seconds << '\n';
    }
    restart_window();
    if (interp_cadence_probe_) {
        // The probe's second: misses that halved with fewer sub-frames were theirs, and the lower rate stays;
        // otherwise they are the game's own at that level, and the sub-frames come back.
        interp_cadence_probe_ = false;
        interp_cadence_own_windows_ = 0u;
        if (own_rate <= interp_cadence_probe_misses_ * 0.5) {
            interp_cadence_change_qpc_ = now_qpc;
            ++interp_cadence_drops_;
        } else {
            if (trace) {
                std::cerr << "[interp-cadence] probe undone " << interp_cadence_cap_ << "->"
                          << interp_cadence_probe_restore_ << '\n';
            }
            interp_cadence_cap_ = interp_cadence_probe_restore_;
            interp_cadence_own_misses_ = interp_cadence_probe_misses_;
            interp_cadence_interval_s_ = std::min(interp_cadence_interval_s_ * 2u, 64u);
        }
        interp_cadence_probe_after_qpc_ = now_qpc + static_cast<std::int64_t>(interp_cadence_interval_s_) * frequency;
        return;
    }
    const bool own_excess = running > 0u && own_rate - interp_cadence_own_misses_ >= 2.0;
    interp_cadence_own_misses_ = std::min(interp_cadence_own_misses_, own_rate);
    interp_cadence_own_windows_ = own_excess ? interp_cadence_own_windows_ + 1u : 0u;
    if (running > 0u && theirs >= 2.0 && theirs >= own_rate) {
        drop(theirs >= 8.0 ? std::min(interp_step_down(running), running / 2u) : interp_step_down(running), "drop");
        return;
    }
    if (interp_cadence_own_windows_ >= 3u && now_qpc >= interp_cadence_probe_after_qpc_) {
        if (trace) {
            std::cerr << "[interp-cadence] probe " << running << "->" << interp_step_down(running) << '\n';
        }
        interp_cadence_probe_ = true;
        interp_cadence_probe_restore_ = interp_cadence_cap_;
        interp_cadence_probe_misses_ = own_rate;
        interp_cadence_cap_ = interp_step_down(running);
        interp_cadence_try_qpc_ = 0;
        return;
    }
    if (interp_cadence_try_qpc_ != 0 && now_qpc - interp_cadence_try_qpc_ >= 4 * frequency) {
        // The try held.
        interp_cadence_interval_s_ = std::max(interp_cadence_interval_s_ / 2u, 2u);
        interp_cadence_try_qpc_ = 0;
    }
    if (interp_cadence_cap_ >= interp_frames_) {
        return;
    }
    if (now_qpc - interp_cadence_change_qpc_ >= static_cast<std::int64_t>(interp_cadence_interval_s_) * frequency) {
        const unsigned up = interp_step_up(interp_cadence_cap_);
        if (trace) {
            std::cerr << "[interp-cadence] try " << interp_cadence_cap_ << "->" << up << '\n';
        }
        interp_cadence_cap_ = up;
        interp_cadence_change_qpc_ = now_qpc;
        interp_cadence_try_qpc_ = now_qpc;
    }
}

// The present rate with `subframes` sub-frames per game frame (start_interp_sequence): with all of them the target,
// else 60 a second for the frame and each sub-frame; with VSync on timed vblanks, a whole number of vblanks a slot.
double GxBackend::interp_rate_with(unsigned subframes) const {
    static const std::int64_t frequency = galaxy::qpc_frequency();
    if (subframes == 0u) {
        return 60.0;
    }
    const RenderConfig cfg = get_render_config();
    double rate = subframes < interp_frames_
        ? 60.0 * static_cast<double>(subframes + 1u)
        : std::max(61.0, present_frame_rate(presented_max_fps(cfg), cfg.vsync_interval, renderer_.display_refresh_hz()));
    std::int64_t vblank = 0;
    std::int64_t vblank_period = 0;
    if (cfg.vsync_interval != 0u && renderer_.vblank_grid(vblank, vblank_period) && vblank_period > 0) {
        const double refresh = static_cast<double>(frequency) / static_cast<double>(vblank_period);
        rate = refresh / static_cast<double>(vblanks_per_slot(refresh, rate));
    }
    return rate;
}

// The most sub-frames below `subframes` that present at a lower rate (on a 240 Hz display two show 120 Hz, as one
// does): 0 when none.
unsigned GxBackend::interp_step_down(unsigned subframes) const {
    const unsigned from = std::min(subframes, interp_frames_);
    const double rate = interp_rate_with(from);
    for (unsigned k = from; k > 0u; --k) {
        if (interp_rate_with(k - 1u) < rate - 0.5) {
            return k - 1u;
        }
    }
    return 0u;
}

// The fewest sub-frames above `subframes` that present at a higher rate: all of them when none.
unsigned GxBackend::interp_step_up(unsigned subframes) const {
    const double rate = interp_rate_with(std::min(subframes, interp_frames_));
    for (unsigned k = subframes + 1u; k < interp_frames_; ++k) {
        if (interp_rate_with(k) > rate + 0.5) {
            return k;
        }
    }
    return interp_frames_;
}

// The next frame's sequence replaces this one before its real frame (at
// rates that are not multiples of 60 that is the normal end: the next
// frame's sub-frames continue from t = 0, its predecessor's image).
void GxBackend::supersede_interp_sequence() {
    if (!interp_sequence_.active) {
        return;
    }
    interp_sequence_.active = false;
    ++interp_superseded_;
}

std::string GxBackend::interp_snapshot_path(const char* suffix) const {
    static const std::string directory = [] {
        char value[MAX_PATH]{};
        std::size_t length = 0;
        return getenv_s(&length, value, sizeof(value), "GALAXY_INTERP_SNAPSHOT_DIR") == 0 &&
                       length > 1u
            ? std::string(value) : std::string{};
    }();
    static const std::uint64_t first = read_env_u64("GALAXY_INTERP_SNAPSHOT_FIRST", 0u);
    static const std::uint64_t last = read_env_u64("GALAXY_INTERP_SNAPSHOT_LAST", 0u);
    if (directory.empty() || interp_sequence_serial_ < first || interp_sequence_serial_ > last) {
        return {};
    }
    return directory + "\\seq_" + std::to_string(interp_sequence_serial_) + "_" + suffix + ".ppm";
}

// t of the next slot in the playing sequence (see advance_interp_sequence).
double GxBackend::interp_slot_t() const {
    static const std::int64_t frequency = galaxy::qpc_frequency();
    return static_cast<double>(interp_slot_due_qpc_ - interp_sequence_.frame_qpc) * 60.0 /
               static_cast<double>(frequency) + interp_sequence_.lead;
}

bool GxBackend::overlay_idle_presents() const noexcept {
    static const std::int64_t frequency = galaxy::qpc_frequency();
    return (native_settings_overlay_visible() || home_menu_visible()) && !interp_sequence_.active &&
           interp_slot_period_qpc_ > 0 && interp_slot_period_qpc_ * 61 < frequency;
}

std::int64_t GxBackend::interp_remaining_us() const {
    const std::int64_t now = galaxy::qpc_now();
    static const std::int64_t frequency = galaxy::qpc_frequency();
    return (interp_slot_due_qpc_ - now) * 1'000'000 / frequency;
}

#if defined(_WIN32) && !defined(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

// Render thread: a short wait to a present slot, finer than the scheduler tick.
void GxBackend::interp_sleep_us(std::int64_t us) {
#if defined(_WIN32)
    struct Timer {
        HANDLE handle = CreateWaitableTimerExW(
            nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        ~Timer() {
            if (handle != nullptr) CloseHandle(handle);
        }
    };
    static thread_local Timer timer;
    LARGE_INTEGER due{};
    due.QuadPart = -us * 10;  // relative, 100 ns units
    if (timer.handle != nullptr && SetWaitableTimer(timer.handle, &due, 0, nullptr, nullptr, FALSE)) {
        (void)WaitForSingleObject(timer.handle, 4u);
        return;
    }
#endif
    std::this_thread::sleep_for(std::chrono::microseconds(us));
}

void GxBackend::advance_interp_sequence(bool flush) {
    static const std::int64_t frequency = galaxy::qpc_frequency();
    flush = flush || interp_sequence_.flush_requested;
    while (interp_sequence_.active) {
        const std::int64_t now = galaxy::qpc_now();
        InterpSequence& sequence = interp_sequence_;
        // Slots run on the QPC clock with VSync too: a Present that waits
        // for the display would hold back the frame chunks behind it.
        if (!flush && now < interp_slot_due_qpc_) {
            return;
        }
        const std::int64_t slot = interp_slot_due_qpc_;
        // A free-running slot due just before the arrival shows t = 0.
        const double t = std::max(0.0, static_cast<double>(slot - sequence.frame_qpc) * 60.0 /
                                           static_cast<double>(frequency) + sequence.lead);
        if (!flush) {
            interp_slot_due_qpc_ = slot + interp_slot_period_qpc_;
            // A stall skips the slots it missed instead of bunching them.
            if (now >= interp_slot_due_qpc_) {
                const std::int64_t missed = (now - interp_slot_due_qpc_) / interp_slot_period_qpc_ + 1;
                interp_late_slots_ += static_cast<std::uint64_t>(missed);
                interp_slot_due_qpc_ += missed * interp_slot_period_qpc_;
            }
        }
        if (!flush && t < 1.0 - 1e-3 && sequence.next < 7u && sequence.subframes != 0u &&
            renderer_.replay_playing()) {
            const auto start = std::chrono::steady_clock::now();
            std::string snapshot =
                interp_snapshot_path(("sub" + std::to_string(sequence.next + 1u)).c_str());
            if (snapshot.empty()) {
                snapshot = present_snapshot_path();  // sub-frames count as presents
            }
            // GALAXY_TEST_CAPTURE_NO_WAIT=1: a sub-frame is saved only when the last save's readback is done (else
            // skipped), never waited for: waiting holds the CPU behind the GPU and hides what a race between them shows.
            static const bool capture_no_wait = read_env_u64("GALAXY_TEST_CAPTURE_NO_WAIT", 0u) != 0u;
            if (!snapshot.empty()) {
                renderer_.set_debug_capture_paths({}, snapshot);
                renderer_.set_subframe_capture(true);
                renderer_.set_capture_waits(!capture_no_wait);
            }
            // GALAXY_INTERP_FORCE_T=<0..1>: every sub-frame at that t (replay checks: 1 must
            // reproduce the frame, 0 the frame before).
            static const double force_t = [] {
                char value[32]{};
                std::size_t length = 0u;
                return getenv_s(&length, value, sizeof(value), "GALAXY_INTERP_FORCE_T") == 0 && length > 1u
                    ? std::clamp(std::strtod(value, nullptr), 0.0, 1.0) : -1.0;
            }();
            const std::int64_t subframe_start = galaxy::qpc_now();
            const bool presented = render_interp_subframe(static_cast<float>(force_t >= 0.0 ? force_t : t));
            interp_subframe_qpc_ += static_cast<std::uint64_t>(galaxy::qpc_now() - subframe_start);
            if (!snapshot.empty()) {
                renderer_.set_subframe_capture(false);
                renderer_.set_capture_waits(false);
                renderer_.set_debug_capture_paths({}, {});
            }
            interp_cpu_ms_ += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            if (presented) {
                ++interp_presented_;
                interp_shown_scene_ = static_cast<std::int32_t>(sequence.scene);
                note_present_vblank(slot);
                note_interp_timeline(slot, force_t >= 0.0 ? force_t : t, false);
            } else {
                ++interp_failed_;
            }
            ++sequence.next;
            // Rates that are not multiples of 60 rarely present real frames.
            report_interp_stats();
            return;
        }
        sequence.active = false;
        interp_snapshot_path_ = interp_snapshot_path("real");
        renderer_.set_capture_waits(!interp_snapshot_path_.empty());
        interp_presenting_real_ = true;
        present_cached_xfb_on_thread(sequence.xfb_addr, false, true);
        note_present_vblank(slot);
        note_interp_timeline(slot, 1.0, true);
        interp_presenting_real_ = false;
        interp_shown_scene_ = static_cast<std::int32_t>(sequence.scene);
        renderer_.set_capture_waits(false);
        interp_snapshot_path_.clear();
        ++interp_sequences_;
        report_interp_stats();
        return;
    }
}

// A present while interpolating: the vblank interval it was issued in, against the one before (0: two in one
// interval, one of them never shown on its own; 2 or more at one present a vblank: a refresh shown twice), and the
// time from its slot to the vblank ending that interval. The picture shows its slot's moment, so with even motion
// that time stays the same; its spread is the motion's unevenness on the display.
void GxBackend::note_present_vblank(std::int64_t slot) {
    static const bool trace = read_env_u64("GALAXY_TRACE_PRESENT_STATS", 0u) != 0u;
    std::int64_t vblank = 0;
    std::int64_t period = 0;
    if (!trace || !renderer_.vblank_grid(vblank, period)) {
        return;
    }
    const std::int64_t now = galaxy::qpc_now();
    const std::int64_t start = vblank + static_cast<std::int64_t>(
        std::floor(static_cast<double>(now - vblank) / static_cast<double>(period))) * period;
    if (interp_last_present_vblank_ != 0) {
        const std::int64_t steps = std::llround(static_cast<double>(start - interp_last_present_vblank_) /
                                                static_cast<double>(period));
        ++interp_vblank_steps_[static_cast<std::size_t>(std::clamp<std::int64_t>(steps, 0, 3))];
    }
    interp_last_present_vblank_ = start;
    static const double frequency = static_cast<double>(galaxy::qpc_frequency());
    const double slot_to_vblank_ms = static_cast<double>(start + period - slot) * 1000.0 / frequency;
    if (interp_slot_to_vblank_samples_++ == 0u) {
        interp_slot_to_vblank_min_ms_ = slot_to_vblank_ms;
        interp_slot_to_vblank_max_ms_ = slot_to_vblank_ms;
    }
    interp_slot_to_vblank_min_ms_ = std::min(interp_slot_to_vblank_min_ms_, slot_to_vblank_ms);
    interp_slot_to_vblank_max_ms_ = std::max(interp_slot_to_vblank_max_ms_, slot_to_vblank_ms);
}

// The line: the slot's time and the present's (QPC ticks), the game frame shown (the played log's serial; t
// between the frame before, 0, and it, 1), whether it was the game's own frame, and the sequence's camera: whether
// it cut (by the game's flag or the thresholds), how far it moved and turned, and whether it chained past skipped logs.
void GxBackend::note_interp_timeline(std::int64_t slot, double t, bool real) {
    static std::FILE* const file = [] {
        char path[MAX_PATH]{};
        std::size_t length = 0u;
        if (getenv_s(&length, path, sizeof(path), "GALAXY_INTERP_TIMELINE") != 0 || length <= 1u) {
            return static_cast<std::FILE*>(nullptr);
        }
        std::FILE* opened = nullptr;
        if (fopen_s(&opened, path, "w") == 0 && opened != nullptr) {
            std::fprintf(opened, "slot_qpc,present_qpc,frame,t,real,frequency,cut,game_cut,moved,turn,chained\n");
        }
        return opened;
    }();
    if (file == nullptr) {
        return;
    }
    std::fprintf(file, "%lld,%lld,%llu,%.6f,%d,%lld,%d,%d,%.1f,%.2f,%d\n", static_cast<long long>(slot),
                 static_cast<long long>(galaxy::qpc_now()), static_cast<unsigned long long>(interp_last_played_serial_),
                 t, real ? 1 : 0, static_cast<long long>(galaxy::qpc_frequency()), interp_camera_.cut ? 1 : 0,
                 interp_camera_game_cut_ ? 1 : 0, static_cast<double>(interp_camera_.moved),
                 static_cast<double>(interp_camera_.turn_degrees), interp_sequence_.chained ? 1 : 0);
}

// A game frame presented while interpolating, without sub-frames: why.
void GxBackend::note_interp_real_present() {
    const std::int64_t now = galaxy::qpc_now();
    note_game_frame_cadence(now);
    interp_shown_scene_ = -1;                  // a frame no sequence knows
    unsigned reason = 5u;                      // log ready, present not matched
    if (native_settings_overlay_visible()) {
        reason = 0u;
    } else if (!renderer_.replay_available() && !interp_log_waiting_) {
        reason = renderer_.replay_reject() != 0u ? renderer_.replay_reject() : 4u;
    }
    ++interp_real_only_[reason];
    report_interp_stats();
}

void GxBackend::report_interp_stats() {
    static const bool trace = read_env_u64("GALAXY_TRACE_PRESENT_STATS", 0u) != 0u;
    if (!trace) {
        return;
    }
    static const std::int64_t frequency = galaxy::qpc_frequency();
    const std::int64_t now = galaxy::qpc_now();
    if (interp_stats_qpc_ == 0) interp_stats_qpc_ = now;
    if (now - interp_stats_qpc_ < 2 * frequency) {
        return;
    }
    double gpu_ms = 0.0;
    std::uint64_t gpu_samples = 0u;
    renderer_.take_subframe_gpu_ms(gpu_ms, gpu_samples);
    const std::array<double, 3> gpu_phase = renderer_.take_subframe_phase_ms();
    const double gpu_divisor = gpu_samples != 0u ? static_cast<double>(gpu_samples) : 1.0;
    double record_ms = 0.0;
    std::uint64_t record_samples = 0u;
    renderer_.take_subframe_record_ms(record_ms, record_samples);
    double replay_ms = 0.0;
    std::uint64_t replay_samples = 0u;
    std::uint64_t replays = 0u;
    renderer_.take_sequence_record_ms(replay_ms, replay_samples, replays);
    const double prepared = static_cast<double>(interp_prepared_);
    std::ostringstream phases;
    for (std::size_t i = 0; i < interp_prepare_phase_ms_.size(); ++i) {
        phases << (i != 0u ? "/" : "") << (interp_prepared_ != 0u ? interp_prepare_phase_ms_[i] / prepared : 0.0);
    }
    const double seconds = static_cast<double>(now - interp_stats_qpc_) /
                           static_cast<double>(frequency);
    const double presented = static_cast<double>(interp_presented_);
    std::uint64_t real_only = 0u;
    for (const std::uint64_t count : interp_real_only_) real_only += count;
    const Renderer::CopyRedirectStats redirect = renderer_.take_copy_redirect_stats();
    std::array<double, 3> path_ms{};
    std::array<std::uint64_t, 3> path_samples{};
    renderer_.take_subframe_path_gpu_ms(path_ms, path_samples);
    const auto per_sample = [](double ms, std::uint64_t samples) {
        return samples != 0u ? ms / static_cast<double>(samples) : 0.0;
    };
    std::ostringstream message;
    message << "[interp-stats] subframes-per-frame=" << interp_frames_
            << " sequences=" << interp_sequences_
            << " presented-hz=" << presented / seconds
            << " dropped=" << interp_dropped_
            << " late-slots=" << interp_late_slots_
            << " vblank-steps=" << interp_vblank_steps_[0] << '/' << interp_vblank_steps_[1] << '/'
            << interp_vblank_steps_[2] << '/' << interp_vblank_steps_[3]
            << " slot-to-vblank-ms=" << interp_slot_to_vblank_min_ms_ << '/' << interp_slot_to_vblank_max_ms_
            << " superseded=" << interp_superseded_
            << " started=" << interp_started_
            << " logs-skipped=" << interp_logs_skipped_
            << " chained-sequences=" << interp_chained_sequences_
            << " unchained-skips=" << interp_unchained_skips_
            << " presents-during-sequence=" << interp_present_skipped_
            << " snapped-records=" << interp_snapped_
            << " failed=" << interp_failed_
            << " history-resets=" << interp_history_resets_
            << " history-carried=" << interp_history_carried_
            << " replay-fail=" << renderer_.replay_failure()
            << " log-gap-ops=" << renderer_.take_log_gap_ops()
            << " real-only=" << real_only
            << " (overlay=" << interp_real_only_[0]
            << " no-scene=" << interp_real_only_[1]
            << " multi-scene=" << interp_real_only_[2]
            << " log-gone=" << interp_real_only_[4]
            << " unmatched=" << interp_real_only_[5] << ")"
            << " moving-per-subframe="
            << (interp_presented_ != 0u ? static_cast<double>(interp_moving_records_) / presented : 0.0)
            << " palette-decompositions=" << interp_palette_decompositions_
            << " layer-records(scene/screen/mirror)=" << layer_records_[0] << '/' << layer_records_[1] << '/'
            << layer_records_[2] << " layer-unmapped=" << layer_unmapped_[0] << '/' << layer_unmapped_[1] << '/'
            << layer_unmapped_[2]
            << " camera-cuts=" << interp_camera_cuts_
            << " identity(records/matched/rejected/scene-records)=" << identity_records_ << '/'
            << identity_matched_ << '/' << identity_rejected_ << '/' << identity_scene_records_
            << " markers(shape/obj/emitter/particle)=" << identity_markers_by_kind_[1] << '/'
            << identity_markers_by_kind_[2] << '/' << identity_markers_by_kind_[3] << '/'
            << identity_markers_by_kind_[4]
            << " motion(game-cuts/threshold-cuts/signature-rejects/continuity-rejects)=" << interp_game_cuts_
            << '/' << interp_threshold_cuts_ << '/' << identity_signature_rejected_ << '/'
            << identity_continuity_rejected_
            << " particle-age-steps(<0/0/1/2/>2)=" << particle_age_steps_[0] << '/' << particle_age_steps_[1]
            << '/' << particle_age_steps_[2] << '/' << particle_age_steps_[3] << '/' << particle_age_steps_[4]
            << " signal-markers(category/image-effect/scene/pass/light/stripe-marks)=" << motion_category_markers_
            << '/' << motion_image_effect_markers_ << '/' << motion_scene_markers_ << '/' << motion_mirror_markers_
            << '/' << motion_light_markers_ << '/' << motion_stripe_marks_
            << " layout-markers(2d-scenes/pane/actor/star-pointer)=" << window_2d_scenes_ << '/'
            << identity_markers_by_kind_[motion::kMarkerLayoutPane] << '/'
            << identity_markers_by_kind_[motion::kMarkerLayoutActor] << '/'
            << identity_markers_by_kind_[motion::kMarkerStarPointer]
            << " prepare-ms-per-sequence="
            << (interp_prepared_ != 0u ? interp_prepare_ms_ / prepared : 0.0)
            << " prepare-phase-ms(camera/records/palettes/blend/colours/program+replay)=" << phases.str()
            << " replay-record-ms-per-sequence="
            << (replay_samples != 0u ? replay_ms / static_cast<double>(replay_samples) : 0.0)
            << " replays-per-sequence="
            << (replay_samples != 0u ? static_cast<double>(replays) / static_cast<double>(replay_samples) : 0.0)
            << " eval-ops-per-sequence="
            << (interp_prepared_ != 0u ? static_cast<double>(interp_program_ops_) / prepared : 0.0)
            << " eval-kb-per-sequence="
            << (interp_prepared_ != 0u ? static_cast<double>(interp_program_words_) * 4.0 / 1024.0 / prepared : 0.0)
            << " submit-ms-per-subframe="
            << (record_samples != 0u ? record_ms / static_cast<double>(record_samples) : 0.0)
            << " wall-ms-per-subframe="
            << (interp_presented_ != 0u ? interp_cpu_ms_ / presented : 0.0)
            << " gpu-ms-per-subframe="
            << (gpu_samples != 0u ? gpu_ms / static_cast<double>(gpu_samples) : 0.0)
            << " gpu-phase-ms(eval/replay/present)=" << gpu_phase[0] / gpu_divisor << '/'
            << gpu_phase[1] / gpu_divisor << '/' << gpu_phase[2] / gpu_divisor
            << " chunk-continued-records=" << std::exchange(interp_chunk_continues_, 0u)
            << " colour-records-per-subframe="
            << (interp_presented_ != 0u
                    ? static_cast<double>(std::exchange(interp_ps_moving_total_, 0u)) / presented : 0.0)
            << " vertex-blend-draws-per-subframe="
            << (interp_presented_ != 0u
                    ? static_cast<double>(renderer_.take_vertex_blend_draws()) / presented : 0.0)
            << " guarded-copies=" << renderer_.take_subframe_guarded()
            << " redirected-copies=" << redirect.redirected
            << " patched-tables=" << redirect.patched_tables
            << " copy-snapshots=" << redirect.snapshots
            << " redirect-fallbacks=" << redirect.fallback_logs
            << " gpu-subframe-cap=" << interp_gpu_cap_
            << " cadence-subframe-cap=" << interp_cadence_cap_
            << " cadence-drops=" << std::exchange(interp_cadence_drops_, 0u)
            << " game-fps=" << interp_cadence_fps_
            << " capped-sequences=" << std::exchange(interp_gpu_capped_, 0u)
            << " subframe-gpu-ema-ms=" << renderer_.subframe_gpu_ema_ms()
            << " gpu-ms-guard-path=" << per_sample(path_ms[0], path_samples[0])
            << " gpu-ms-redirect-path=" << per_sample(path_ms[1], path_samples[1])
            << " gpu-ms-unguarded-path=" << per_sample(path_ms[2], path_samples[2])
            << " path-samples(guard/redirect/unguarded)=" << path_samples[0] << '/' << path_samples[1]
            << '/' << path_samples[2]
            << " held-textures=" << texture_cache_.take_held_texture_count()
            << " held-vertex-blocks=" << interp_held_vertex_blocks_
            << " ring-peak-kb(v/i/c/m)="
            << renderer_.vertex_ring().take_peak_bytes_per_frame() / 1024u << '/'
            << renderer_.index_ring().take_peak_bytes_per_frame() / 1024u << '/'
            << renderer_.constant_ring().take_peak_bytes_per_frame() / 1024u << '/'
            << renderer_.matrix_ring().take_peak_bytes_per_frame() / 1024u
            << " measured-presents-hz=" << renderer_.measured_present_hz() << '\n';
    std::cerr << message.str();
    interp_presented_ = 0u;
    interp_dropped_ = 0u;
    interp_late_slots_ = 0u;
    interp_vblank_steps_ = {};
    interp_slot_to_vblank_samples_ = 0u;
    interp_superseded_ = 0u;
    interp_started_ = 0u;
    interp_logs_skipped_ = 0u;
    interp_chained_sequences_ = 0u;
    interp_unchained_skips_ = 0u;
    interp_present_skipped_ = 0u;
    interp_snapped_ = 0u;
    interp_failed_ = 0u;
    interp_history_resets_ = 0u;
    interp_history_carried_ = 0u;
    interp_sequences_ = 0u;
    interp_real_only_ = {};
    interp_cpu_ms_ = 0.0;
    interp_prepare_ms_ = 0.0;
    interp_prepare_phase_ms_ = {};
    interp_prepared_ = 0u;
    interp_program_ops_ = 0u;
    interp_program_words_ = 0u;
    interp_held_vertex_blocks_ = 0u;
    interp_moving_records_ = 0u;
    interp_palette_decompositions_ = 0u;
    interp_camera_cuts_ = 0u;
    layer_records_ = {};
    layer_unmapped_ = {};
    identity_records_ = 0u;
    identity_matched_ = 0u;
    identity_rejected_ = 0u;
    identity_scene_records_ = 0u;
    identity_markers_by_kind_ = {};
    interp_game_cuts_ = 0u;
    interp_threshold_cuts_ = 0u;
    identity_signature_rejected_ = 0u;
    identity_continuity_rejected_ = 0u;
    particle_age_steps_ = {};
    motion_category_markers_ = 0u;
    motion_image_effect_markers_ = 0u;
    motion_scene_markers_ = 0u;
    motion_mirror_markers_ = 0u;
    motion_light_markers_ = 0u;
    window_2d_scenes_ = 0u;
    motion_stripe_marks_ = 0u;
    interp_stats_qpc_ = now;
}

// One interpolated frame at t (0 = previous game frame, 1 = the latest):
// the camera at t, and every scene record whose constants change rebuilt
// from its matched previous record (frame_interpolation.h): position and
// normal matrices in world space under the camera at t, texture matrices,
// lights and projection; then the latest frame's operations replayed.
bool GxBackend::render_interp_subframe(float t) {
    const InterpScene& scene = interp_scenes_[interp_sequence_.scene];
    const InterpScene& before_scene = interp_scenes_[interp_sequence_.before];
    // Matched records of the scene before (through skipped scenes: select_interp_before).
    const std::vector<std::int32_t>& matched_index =
        interp_sequence_.chained ? interp_chain_prev_ : scene.prev_index;
    // The screen layer (HUD, titles, fades, cinema bars) moves like the scene: the game names each layout pane,
    // layout actor and star pointer (layout markers), so a panel that appears or goes is its own identity and
    // never pairs the panels after it with others.
    const std::vector<std::int32_t>& prev_index = matched_index;
    if (!scene.valid) {
        renderer_.note_replay_failure(9u);
        return false;
    }
    const auto build_start = std::chrono::steady_clock::now();
    const std::size_t count = scene.constants.size();
    constexpr std::size_t kWords = kMatrixPaletteWords;
    static_assert(kMatrixPaletteWords == kXfPaletteWords);
    // The scene before is the frame presented before this one: a log in between that was not recorded (shown as it
    // was: two 3D scenes, or the first of a run of frames without one) leaves it older, and sub-frames from it would
    // step back behind the frame shown last. A chained sequence maps its records through the skipped scenes.
    const bool before_valid = before_scene.valid &&
                              (interp_sequence_.chained || before_scene.serial == 0u || scene.serial == 0u ||
                               before_scene.serial + 1u == scene.serial);
    const auto before_palette = [&](std::int32_t shadow) -> const std::uint32_t* {
        return before_valid && shadow >= 0 && static_cast<std::size_t>(shadow) < before_scene.palette_count
            ? before_scene.palettes.data() + static_cast<std::size_t>(shadow) * kWords
            : nullptr;
    };
    if (!interp_sequence_.classified) {
        // Once per sequence: the camera between the two scenes, then every
        // record whose constants differ at some t, with its decomposed motion.
        interp_sequence_.classified = true;
        interp_moving_.clear();
        interp_pending_.clear();
        interp_palette_motions_.clear();
        interp_palette_first_.assign(scene.palette_count, -1);
        static const CameraCutLimits cut_limits{};
        camera_motion(before_valid && before_scene.view_known ? &before_scene.view : nullptr,
                      scene.view_known ? &scene.view : nullptr, interp_camera_move_, cut_limits,
                      interp_camera_);
        // The game's camera epoch changes when the view was set without blending:
        // a hard cut whatever the thresholds say. The thresholds catch cuts the
        // game does not flag.
        const bool game_cut = before_valid && scene.camera_epoch_known && before_scene.camera_epoch_known &&
                              scene.camera_epoch != before_scene.camera_epoch;
        if (game_cut && interp_camera_.known) {
            interp_camera_.cut = true;
        }
        interp_camera_move_ = interp_camera_.known && !game_cut ? interp_camera_.moved : 0.0f;
        interp_camera_game_cut_ = game_cut;
        if (interp_camera_.cut) {
            ++interp_camera_cuts_;
            ++(game_cut ? interp_game_cuts_ : interp_threshold_cuts_);
        }
        // Mirror-pass records move with their own camera: no world split; a cut
        // of the scene camera cuts them too.
        interp_mirror_camera_ = CameraMotion{};
        interp_mirror_camera_.known = interp_camera_.known && interp_camera_.cut;
        interp_mirror_camera_.cut = interp_mirror_camera_.known;
        const bool camera_moves = interp_camera_.known && !interp_camera_.cut &&
            std::memcmp(interp_camera_.a.data(), interp_camera_.b.data(), sizeof(Affine34)) != 0;
        const auto records_start = std::chrono::steady_clock::now();
        for (std::size_t r = 0; r < count; ++r) {
            const std::int32_t m = prev_index[r];
            const bool prev_data = before_valid && m >= 0 &&
                static_cast<std::size_t>(m) < before_scene.constants.size();
            const GxVsConstants& now = scene.constants[r];
            const GxVsConstants* before = prev_data ? &before_scene.constants[static_cast<std::size_t>(m)] : nullptr;
            // The screen layer moves in screen space, whatever its projection,
            // and the scene camera's cuts do not apply to it.
            const unsigned layer = scene.in_layer(kScreenLayer, r) ? kScreenLayer
                : scene.in_layer(kMirrorLayer, r)                  ? kMirrorLayer
                                                                   : kSceneLayer;
            const bool orthographic = layer == kScreenLayer || now.projection[3][3] != 0.0f;
            const CameraMotion& camera = layer_camera(layer);
            InterpRecordMotion motion{};
            motion.record = static_cast<std::uint32_t>(r);
            motion.prev = prev_data ? m : -1;
            if (scene.per_vertex[r] == 0u) {
                Affine34 b{};
                std::memcpy(b.data(), now.inline_pos_matrix, sizeof(Affine34));
                Affine34 a{};
                if (before != nullptr) {
                    std::memcpy(a.data(), before->inline_pos_matrix, sizeof(Affine34));
                }
                eye_motion(before != nullptr ? &a : nullptr, b, camera, orthographic, motion.position);
                motion.position_moves =
                    !motion.position.still || (motion.position.split && camera_moves);
                interp_snapped_ += motion.position.snapped ? 1u : 0u;
            }
            if (before != nullptr && !(camera.known && camera.cut)) {
                motion.colour_moves = channel_colours_move(*before, now);
                motion.tex_moves =
                    std::memcmp(before->inline_tex_matrices, now.inline_tex_matrices,
                                sizeof(now.inline_tex_matrices)) != 0 ||
                    std::memcmp(before->inline_post_matrices, now.inline_post_matrices,
                                sizeof(now.inline_post_matrices)) != 0;
                motion.projection_moves =
                    before->projection[3][3] == now.projection[3][3] &&
                    std::memcmp(before->projection, now.projection, sizeof(now.projection)) != 0;
            }
            const std::int32_t shadow = scene.palette[r];
            if (shadow >= 0 && static_cast<std::size_t>(shadow) < scene.palette_count) {
                const std::int32_t before_shadow =
                    prev_data ? before_scene.palette[static_cast<std::size_t>(m)] : -1;
                const std::uint32_t* before_words = before_palette(before_shadow);
                const std::int32_t pair_before = before_words != nullptr ? before_shadow : -1;
                std::int32_t index = interp_palette_first_[static_cast<std::size_t>(shadow)];
                while (index >= 0 &&
                       interp_palette_motions_[static_cast<std::size_t>(index)].before != pair_before) {
                    index = interp_palette_motions_[static_cast<std::size_t>(index)].next;
                }
                if (index < 0) {
                    index = static_cast<std::int32_t>(interp_palette_motions_.size());
                    InterpPaletteMotion& entry = interp_palette_motions_.emplace_back();
                    entry.shadow = shadow;
                    entry.before = pair_before;
                    entry.next = interp_palette_first_[static_cast<std::size_t>(shadow)];
                    entry.orthographic = orthographic;
                    entry.layer = static_cast<std::uint8_t>(layer);
                    interp_palette_first_[static_cast<std::size_t>(shadow)] = index;
                }
                // The slots this record reads: every one for per-vertex
                // matrices; for an inline draw, the normal matrix of its
                // matrix index when it is lit; the lights its channels and
                // emboss texgens evaluate (texture rows are taken whole).
                InterpPaletteMotion& entry = interp_palette_motions_[static_cast<std::size_t>(index)];
                entry.light_mask |= used_lights(now);
                if (scene.per_vertex[r] != 0u) {
                    entry.position_mask = kAllPaletteSlots;
                    entry.normal_mask = kAllPaletteSlots;
                } else if ((now.flags & 0x02u) != 0u) {
                    const std::uint32_t slot = (now.matrix_index_a & 0x3Fu) / 3u;
                    if (slot < 10u) entry.normal_mask |= static_cast<std::uint16_t>(1u << slot);
                }
                motion.palette = index;
            }
            if (motion.position_moves || motion.tex_moves || motion.projection_moves ||
                motion.colour_moves || motion.palette >= 0) {
                interp_pending_.push_back(motion);
            }
        }
        const auto palettes_start = std::chrono::steady_clock::now();
        // Each palette pair once, with the slots its records read.
        for (InterpPaletteMotion& entry : interp_palette_motions_) {
            const std::uint32_t* now_words =
                scene.palettes.data() + static_cast<std::size_t>(entry.shadow) * kWords;
            palette_motion(before_palette(entry.before), now_words, layer_camera(entry.layer),
                           entry.orthographic,
                           entry.motion, entry.position_mask, entry.normal_mask, entry.light_mask);
            interp_snapped_ += entry.motion.snapped;
            ++interp_palette_decompositions_;
        }
        for (InterpRecordMotion& motion : interp_pending_) {
            if (motion.palette >= 0 &&
                !interp_palette_motions_[static_cast<std::size_t>(motion.palette)].motion.any_changed) {
                motion.palette = -1;
            }
            if (motion.position_moves || motion.tex_moves || motion.projection_moves ||
                motion.colour_moves || motion.palette >= 0) {
                interp_moving_.push_back(motion);
            }
        }
        const auto blend_start = std::chrono::steady_clock::now();
        // Vertex blending pairs draws with the log run before: not for a
        // sequence chained past a skipped scene (select_interp_before).
        renderer_.prepare_vertex_blend(before_valid && !interp_sequence_.chained
                                           ? std::span<const std::int32_t>(prev_index)
                                           : std::span<const std::int32_t>{});
        const auto colours_start = std::chrono::steady_clock::now();
        // Pixel-constant records pair through the paired draws.
        interp_ps_moving_.clear();
        if (before_valid && !(interp_camera_.known && interp_camera_.cut)) {
            const std::span<const std::int32_t> pairs = renderer_.ps_record_pairs();
            const std::size_t records = std::min(pairs.size(), scene.ps_constants.size());
            for (std::size_t p = 0; p < records; ++p) {
                const std::int32_t q = pairs[p];
                if (q >= 0 && static_cast<std::size_t>(q) < before_scene.ps_constants.size() &&
                    ps_colours_move(before_scene.ps_constants[static_cast<std::size_t>(q)],
                                    scene.ps_constants[p])) {
                    interp_ps_moving_.push_back({static_cast<std::uint32_t>(p), static_cast<std::uint32_t>(q)});
                }
            }
        }
        const auto program_start = std::chrono::steady_clock::now();
        interp_sequence_.ready = prepare_interp_sequence(scene, before_scene, before_valid);
        const auto prepare_end = std::chrono::steady_clock::now();
        const auto ms = [](auto from, auto to) { return std::chrono::duration<double, std::milli>(to - from).count(); };
        interp_prepare_phase_ms_[0] += ms(build_start, records_start);
        interp_prepare_phase_ms_[1] += ms(records_start, palettes_start);
        interp_prepare_phase_ms_[2] += ms(palettes_start, blend_start);
        interp_prepare_phase_ms_[3] += ms(blend_start, colours_start);
        interp_prepare_phase_ms_[4] += ms(colours_start, program_start);
        interp_prepare_phase_ms_[5] += ms(program_start, prepare_end);
        interp_prepare_ms_ += ms(build_start, prepare_end);
        ++interp_prepared_;
    }
    if (!interp_sequence_.ready) {
        return false;
    }
    const Affine34 camera_t = camera_at(interp_camera_, t);
    interp_moving_records_ += interp_moving_.size();
    interp_ps_moving_total_ += interp_ps_moving_.size();
    // The frame presented before: earlier in this sequence; this sequence's t = 0 when it was the scene before at
    // t = 1; or the last sub-frame of the scene before's sequence, at t' of that pair, when the scene arrived first
    // (on the display's vblanks every sequence ends so: no slot meets t = 1). That frame sits at t' - 1 on this
    // pair's line: the motion vectors take this pair's motion back over the last 1 - t' of the one before, exact for
    // steady motion and off by the change in velocity over that part. Anything else (a frame presented alone, a cut,
    // a gap, skipped scenes) starts the upscaler's history again.
    Renderer::SubframeTime time{};
    time.t = t;
    time.t_prev = t;
    const bool shown_before = interp_shown_scene_ == static_cast<std::int32_t>(interp_sequence_.before) &&
                              !(interp_camera_.known && interp_camera_.cut);
    if (interp_shown_serial_ == interp_sequence_serial_ && interp_shown_t_ < t) {
        time.t_prev = interp_shown_t_;
    } else if (interp_shown_t_ >= 1.0f && shown_before) {
        time.t_prev = 0.0f;
    } else if (shown_before && !interp_sequence_.chained && interp_shown_serial_ + 1u == interp_sequence_serial_) {
        time.t_prev = interp_shown_t_ - 1.0f;
        ++interp_history_carried_;
    } else {
        time.reset = true;
        ++interp_history_resets_;
    }
    // The projection this frame's scene draws with (the evaluation's: the records' projections interpolated at t, none
    // across a cut), so the ray tracer's composites rebuild its points from its own depth. The traced frame's projection
    // differs from it wherever the game moves the projection between frames (a galaxy's opening camera slides its
    // vertical offset, up to a third of the screen; camera shake is an offset too; a zoom changes its scale) and by the
    // temporal upscaler's jitter, the renderer's to add.
    if (scene.rt_projection_known) {
        time.projection = scene.rt_projection;
        time.viewport_width = scene.rt_viewport[0];
        time.viewport_height = scene.rt_viewport[1];
        time.projection_known = true;
        if (t < 1.0f && before_valid && before_scene.rt_projection_known && !(interp_camera_.known && interp_camera_.cut)) {
            for (unsigned i = 0; i < 6u; ++i) {
                const float a = before_scene.rt_projection[i];
                const float b = scene.rt_projection[i];
                if (std::isfinite(a) && std::isfinite(b)) {
                    time.projection[i] = a + (b - a) * t;
                }
            }
        }
    }
    const Affine34 camera_prev = time.t_prev == t ? camera_t : camera_at(interp_camera_, time.t_prev);
    if (!renderer_.present_replay_subframe(time, camera_t, camera_prev)) {
        return false;
    }
    interp_shown_serial_ = interp_sequence_serial_;
    interp_shown_t_ = t;
    return true;
}

// The playing sequence's evaluation program (interp_gpu.h): an output block
// per moving record, palette pair and pixel record, the ops that move them,
// and the replay recorded once over them.
bool GxBackend::prepare_interp_sequence(const InterpScene& scene, const InterpScene& before_scene,
                                        bool before_valid) {
    constexpr std::size_t kWords = kMatrixPaletteWords;
    const std::size_t count = scene.constants.size();
    const auto before_palette = [&](std::int32_t shadow) -> const std::uint32_t* {
        return before_valid && shadow >= 0 && static_cast<std::size_t>(shadow) < before_scene.palette_count
            ? before_scene.palettes.data() + static_cast<std::size_t>(shadow) * kWords
            : nullptr;
    };
    interp_program_.clear();
    interp_program_.set_endpoint_cameras(camera_at(interp_camera_, 0.0f), camera_at(interp_camera_, 1.0f));
    interp_vs_slot_.assign(count, -1);
    interp_record_palette_slot_.assign(count, -1);
    interp_ps_slot_.assign(scene.ps_constants.size(), -1);
    interp_palette_slot_.assign(interp_palette_motions_.size(), -1);
    interp_palette_source_.clear();
    interp_palette_entry_.clear();
    for (const InterpRecordMotion& moving : interp_moving_) {
        if (moving.palette < 0) continue;
        std::int32_t& slot = interp_palette_slot_[static_cast<std::size_t>(moving.palette)];
        if (slot < 0) {
            slot = static_cast<std::int32_t>(interp_palette_source_.size());
            interp_palette_source_.push_back(scene.constants[moving.record].mtx_palette_base);
            interp_palette_entry_.push_back(moving.palette);
        }
    }
    // Temporal upscaling of the replays: the motion vector fields of each
    // moving block at the frame presented before (blocks the program leaves
    // alone carry their own transform there: no motion).
    const bool motion = renderer_.temporal_interpolation();
    interp_layout_ = {static_cast<std::uint32_t>(interp_moving_.size()),
                      static_cast<std::uint32_t>(interp_palette_source_.size()),
                      static_cast<std::uint32_t>(interp_ps_moving_.size()),
                      motion ? static_cast<std::uint32_t>(interp_palette_source_.size()) : 0u};
    const interp_gpu::Layout& layout = interp_layout_;
    for (std::size_t s = 0; s < interp_moving_.size(); ++s) {
        const InterpRecordMotion& moving = interp_moving_[s];
        interp_vs_slot_[moving.record] = static_cast<std::int32_t>(s);
        const std::uint32_t base = layout.vs_offset(static_cast<std::uint32_t>(s)) / 4u;
        const GxVsConstants& recorded = scene.constants[moving.record];
        const GxVsConstants* before = moving.prev >= 0
            ? &before_scene.constants[static_cast<std::size_t>(moving.prev)] : nullptr;
        // The inline matrix's ends: the frame before's and this one's.
        const float* const position_a = before != nullptr ? &before->inline_pos_matrix[0][0] : nullptr;
        const float* const position_b = &recorded.inline_pos_matrix[0][0];
        if (moving.position_moves) {
            interp_program_.eye(base + interp_gpu::kVsPosition, moving.position, position_a, position_b);
        }
        if (moving.tex_moves && before != nullptr) {
            // Per texgen: projection and environment maps lerp whole; texture
            // coordinate matrices scroll (wrap-aware) or step.
            for (unsigned i = 0; i < kMaxTexGens; ++i) {
                const bool spatial = texgen_spatial(recorded.texgen_config[i][0]);
                interp_program_.tex_matrix(base + interp_gpu::kVsTex + 12u * i, &before->inline_tex_matrices[i][0][0],
                                           &recorded.inline_tex_matrices[i][0][0], spatial);
                interp_program_.tex_matrix(base + interp_gpu::kVsPost + 12u * i,
                                           &before->inline_post_matrices[i][0][0],
                                           &recorded.inline_post_matrices[i][0][0], spatial);
            }
        }
        if (moving.colour_moves && before != nullptr) {
            interp_program_.channel_colours(base, *before, recorded);
        }
        if (moving.projection_moves && before != nullptr) {
            interp_program_.projection(base, *before, recorded);
        }
        std::int32_t palette_slot = -1;
        if (moving.palette >= 0) {
            const std::int32_t q = interp_palette_slot_[static_cast<std::size_t>(moving.palette)];
            if (q >= 0) {
                palette_slot = q;
                interp_record_palette_slot_[moving.record] = q;
                const std::uint32_t word =
                    (layout.palette_offset(static_cast<std::uint32_t>(q)) - layout.palette_base()) / 16u;
                interp_program_.store(base + interp_gpu::kVsPaletteBase, &word, 1u);
            }
        }
        if (motion) {
            interp_program_.at_previous(true);
            if (moving.position_moves) {
                interp_program_.eye(base + interp_gpu::kVsMotionPrevPos, moving.position, position_a, position_b);
            }
            if (moving.projection_moves && before != nullptr) {
                interp_program_.projection_params(base, *before, recorded);
            }
            if (palette_slot >= 0) {
                // Mode 0: the palette's rows at the frame before.
                const std::uint32_t ctrl[2]{
                    0u, (layout.prev_palette_offset(static_cast<std::uint32_t>(palette_slot)) - layout.palette_base()) /
                            16u};
                interp_program_.store(base + interp_gpu::kVsMotionCtrl, ctrl, 2u);
            }
            interp_program_.at_previous(false);
        }
    }
    for (std::size_t q = 0; q < interp_palette_entry_.size(); ++q) {
        const InterpPaletteMotion& entry = interp_palette_motions_[static_cast<std::size_t>(interp_palette_entry_[q])];
        const std::uint32_t* now_words = scene.palettes.data() + static_cast<std::size_t>(entry.shadow) * kWords;
        interp_program_.palette(layout.palette_offset(static_cast<std::uint32_t>(q)) / 4u, entry.motion,
                                before_palette(entry.before), now_words,
                                layer_camera(entry.layer));
        if (q < layout.prev_palette_count) {
            interp_program_.at_previous(true);
            interp_program_.palette_positions(layout.prev_palette_offset(static_cast<std::uint32_t>(q)) / 4u,
                                              entry.motion, before_palette(entry.before), now_words);
            interp_program_.at_previous(false);
        }
    }
    for (std::size_t k = 0; k < interp_ps_moving_.size(); ++k) {
        const auto [p, q] = interp_ps_moving_[k];
        interp_ps_slot_[p] = static_cast<std::int32_t>(k);
        interp_program_.ps_colours(layout.ps_offset(static_cast<std::uint32_t>(k)) / 4u,
                                   before_scene.ps_constants[q], scene.ps_constants[p]);
    }
    interp_program_ops_ += interp_program_.ops.size();
    interp_program_words_ += interp_program_.payload.size();
    Renderer::ReplaySequence sequence{};
    sequence.layout = layout;
    sequence.record_vs_slot = interp_vs_slot_;
    sequence.record_palette_slot = interp_record_palette_slot_;
    sequence.ps_slot = interp_ps_slot_;
    sequence.palette_source = interp_palette_source_;
    sequence.program = &interp_program_;
    sequence.vertex_blend = true;
    return renderer_.begin_replay_sequence(sequence);
}

bool GxBackend::skip_for_present_rate() {
    const RenderConfig cfg = get_render_config();
    const double rate = present_frame_rate(presented_max_fps(cfg), cfg.vsync_interval, renderer_.display_refresh_hz());
    if (rate >= 59.0) {
        next_rate_present_qpc_ = 0;
        return false;
    }
    static const std::int64_t frequency = galaxy::qpc_frequency();
    const std::int64_t now = galaxy::qpc_now();
    const auto period = static_cast<std::int64_t>(static_cast<double>(frequency) / rate);
    // A stall restarts the cadence instead of releasing a burst of presents.
    if (next_rate_present_qpc_ == 0 || now - next_rate_present_qpc_ > period) {
        next_rate_present_qpc_ = now;
    }
    if (now + period / 4 < next_rate_present_qpc_) return true;
    next_rate_present_qpc_ += period;
    return false;
}

void GxBackend::present_cached_xfb(
    std::uint32_t displayed_xfb_addr,
    bool present_swap_chain) {
    if (!initialized_ || !present_swap_chain) {
        return;
    }

    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (render_thread_error_ != nullptr) {
            std::exception_ptr error = render_thread_error_;
            render_thread_error_ = nullptr;
            std::rethrow_exception(error);
        }
        if (stop_render_thread_) {
            return;
        }
        if (!frame_queue_.empty()) {
            FrameChunk& back = frame_queue_.back();
            if (back.present_only) {
                back.displayed_xfb_addr = displayed_xfb_addr;
                queue_cv_.notify_one();
                return;
            }
            if (!back.efb_peek_only &&
                !back.present_only && !back.present_swap_chain) {
                back.present_swap_chain = true;
                back.displayed_xfb_addr = displayed_xfb_addr;
                queue_cv_.notify_one();
                return;
            }
        }
        FrameChunk chunk;
        chunk.present_only = true;
        chunk.present_swap_chain = true;
        chunk.displayed_xfb_addr = displayed_xfb_addr;
        frame_queue_.push_back(std::move(chunk));
    }
    queue_cv_.notify_one();
}

// Consumer loop (render thread): frames execute in submission order; queued
// frames are drained before a stop so the last presents land.
void GxBackend::render_thread_main() {
    galaxy::host::name_current_thread(L"Nebula GX Render");
    galaxy::host::perf_hint_add_current_thread();
#if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif
    {
        // Opt the process out of EcoQoS throttling, so hybrid CPUs do not park the
        // simulation, render or DSP threads on efficiency cores.
        static std::once_flag power_throttling_once;
        std::call_once(power_throttling_once, [] {
            galaxy::host::opt_out_of_power_throttling_process();
#if defined(_WIN32)
            // 1 ms timer granularity: otherwise sleeps, condition-variable timeouts and
            // waitable timers round to the 15.6 ms scheduler tick, delaying fence
            // polling and every sub-frame wait.
            if (HMODULE winmm = LoadLibraryW(L"winmm.dll")) {
                using TimeBeginPeriodFn = UINT(WINAPI*)(UINT);
                if (auto begin = reinterpret_cast<TimeBeginPeriodFn>(
                        GetProcAddress(winmm, "timeBeginPeriod"))) {
                    (void)begin(1);
                }
            }
#endif
        });
    }
    EfbPeekRequest* active_peek = nullptr;
    std::shared_ptr<PointerDepthCapture> active_pointer_depth;
    // Called with queue_mutex_ held. Never leave a stack-owned utility request
    // queued after its worker has failed. Notify while holding its mutex, so a
    // waiter cannot destroy the request before the final cv access finishes.
    const auto fail_queued_utilities = [&](std::exception_ptr error) {
        render_thread_error_ = error;
        stop_render_thread_ = true;
        const auto fail_request = [&](EfbPeekRequest* request) {
            if (request != nullptr) {
                const std::lock_guard<std::mutex> request_lock(request->mutex);
                request->error = error;
                request->success = false;
                request->done = true;
                request->cv.notify_one();
            }
        };
        fail_request(active_peek);
        active_peek = nullptr;
        if (active_pointer_depth) {
            active_pointer_depth->error = error;
            active_pointer_depth->success = false;
            active_pointer_depth->publish_complete();
            active_pointer_depth.reset();
        }
        if (pending_pointer_depth_capture_.capture) {
            auto capture = std::move(pending_pointer_depth_capture_.capture);
            pending_pointer_depth_capture_ = {};
            capture->error = error;
            capture->success = false;
            capture->publish_complete();
        }
        for (FrameChunk& queued : frame_queue_) {
            if (queued.efb_peek_only) {
                EfbPeekRequest* request = queued.efb_peek_request;
                queued.efb_peek_request = nullptr;
                fail_request(request);
                if (queued.pointer_depth_capture) {
                    queued.pointer_depth_capture->error=error;
                    queued.pointer_depth_capture->success=false;
                    queued.pointer_depth_capture->publish_complete();
                }
            }
        }
        queue_space_cv_.notify_all();
    };
    try {
    for (;;) {
        try {
            poll_completed_frame_effects();
            poll_completed_pointer_depth_capture();
            poll_async_peek_fields();
        } catch (...) {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            fail_queued_utilities(std::current_exception());
            break;
        }
        FrameChunk chunk;
        bool have_chunk = false;
        bool idle_present = false;
        bool overlay_idle = false;
        std::uint64_t shutdown_fence = 0;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            const RenderConfig cfg = get_render_config();
            idle_present = cfg.frame_rate_decouple &&
                (cfg.vsync_interval != 0u || presented_max_fps(cfg) > 0.0f);
            const bool poll_frame_effects = !pending_frame_effects_.empty();
            const bool poll_pointer_depth =
                static_cast<bool>(pending_pointer_depth_capture_.capture) ||
                renderer_.any_async_peek_field_pending();
            // Draining for a stop: queued frames present without sub-frames.
            interp_stopping_ = stop_render_thread_;
            // A native overlay over a game showing no interpolated frames (the HOME Menu's paused game draws no 3D scene;
            // interpolation waits while the settings are open): the latest frame again at each display slot, so the
            // overlay keeps the rate play had instead of the video retrace's 60 Hz.
            overlay_idle = !stop_render_thread_ && overlay_idle_presents();
            if (!overlay_idle) {
                overlay_idle_due_qpc_ = 0;
            } else if (overlay_idle_due_qpc_ == 0) {
                overlay_idle_due_qpc_ = galaxy::qpc_now() + interp_slot_period_qpc_;
            }
            if (interp_sequence_.active && frame_queue_.empty() && !stop_render_thread_) {
                // Coarse and interruptible to about 1.5 ms before the slot
                // (the condition variable wakes on the 1 ms scheduler tick),
                // then a high-resolution timer for the rest.
                const std::int64_t remaining_us = interp_remaining_us();
                if (remaining_us > 2000) {
                    queue_cv_.wait_for(lock, std::chrono::microseconds(remaining_us - 1500), [this] {
                        return stop_render_thread_ || !frame_queue_.empty();
                    });
                } else if (remaining_us > 0) {
                    lock.unlock();
                    interp_sleep_us(remaining_us);
                    lock.lock();
                }
            } else if (overlay_idle && frame_queue_.empty()) {
                static const std::int64_t frequency = galaxy::qpc_frequency();
                const std::int64_t remaining_us = (overlay_idle_due_qpc_ - galaxy::qpc_now()) * 1'000'000 / frequency;
                if (remaining_us > 2000) {
                    queue_cv_.wait_for(lock, std::chrono::microseconds(remaining_us - 1500), [this] {
                        return stop_render_thread_ || !frame_queue_.empty();
                    });
                } else if (remaining_us > 0) {
                    lock.unlock();
                    interp_sleep_us(remaining_us);
                    lock.lock();
                }
            } else if (idle_present || poll_frame_effects || poll_pointer_depth) {
                // Fences are polled at the top of the loop, outside queue_mutex_:
                // park for at most 1 ms, woken early by new work or a stop.
                queue_cv_.wait_for(lock, std::chrono::milliseconds(1), [this] {
                    return stop_render_thread_ || !frame_queue_.empty();
                });
            } else {
                queue_cv_.wait(lock, [this] {
                    return stop_render_thread_ || !frame_queue_.empty();
                });
            }
            // A due slot goes before queued chunks, so presents keep their
            // cadence while a game frame's chunks stream in; except a slot
            // that would show the frame itself (t >= 1) while the next
            // frame's present is queued: that present starts its sequence.
            const auto due_slot_first = [this] {
                if (interp_remaining_us() > 0) return false;
                if (interp_slot_t() < 1.0 - 1e-3) return true;
                for (const FrameChunk& queued : frame_queue_) {
                    if (queued.present_only || queued.present_swap_chain) return false;
                }
                return true;
            };
            if (interp_sequence_.active && !stop_render_thread_ &&
                (frame_queue_.empty() || due_slot_first())) {
                lock.unlock();
                try {
                    advance_interp_sequence(false);
                } catch (...) {
                    std::lock_guard<std::mutex> relock(queue_mutex_);
                    fail_queued_utilities(std::current_exception());
                    break;
                }
                continue;
            }
            if (frame_queue_.empty()) {
                if (stop_render_thread_) {
                    if (pending_frame_effects_.empty() &&
                        !pending_pointer_depth_capture_.capture) {
                        break;  // stop requested, queue and PE fences drained
                    }
                    shutdown_fence = pending_frame_effects_.empty() ? 0u :
                        pending_frame_effects_.back().fence_value;
                    shutdown_fence = std::max(shutdown_fence,
                        pending_pointer_depth_capture_.fence_value);
                }
            } else {
                chunk = std::move(frame_queue_.front());
                frame_queue_.pop_front();
                if (!chunk.efb_peek_only &&
                    !chunk.present_only && !chunk.present_swap_chain &&
                    !frame_queue_.empty() &&
                    frame_queue_.front().present_only) {
                    FrameChunk present = std::move(frame_queue_.front());
                    frame_queue_.pop_front();
                    chunk.present_swap_chain = present.present_swap_chain;
                    chunk.displayed_xfb_addr = present.displayed_xfb_addr;
                }
                if (chunk.efb_peek_only) {
                    active_peek = chunk.efb_peek_request;
                    active_pointer_depth = chunk.pointer_depth_capture;
                    ++utility_chunks_in_flight_;
                } else if (chunk.present_only) {
                    ++present_chunks_in_flight_;
                } else {
                    galaxy::timeline::record(galaxy::timeline::Event::RenderBegin,
                                        chunk.pe_completion_token.value,
                                        chunk.fifo.size());
                    render_chunk_started_ns_ = static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count());
                    ++render_chunks_in_flight_;
                    ++render_cpu_reads_in_flight_;
                    chunk.cpu_reads_pending = true;
                    if (chunk.pe_completion_requires_resolution) {
                        ++render_fifo_effects_in_flight_;
                        chunk.fifo_effects_pending = true;
                    }
                }
                have_chunk = true;
            }
        }

        try {
            if (shutdown_fence != 0u) {
                // Shutdown is not allowed to discard a submitted PE event.
                // Wait for only the last outstanding effects fence; queue
                // ordering then proves every earlier token is also complete.
                renderer_.wait_for_fence_value(shutdown_fence);
                poll_completed_frame_effects();
                poll_completed_pointer_depth_capture();
            } else if (have_chunk) {
                queue_space_cv_.notify_all();
                if (chunk.efb_peek_only) {
                    execute_efb_peek_on_thread(chunk);
                    active_peek = nullptr;
                    active_pointer_depth.reset();
                } else if (chunk.present_only) {
                    // An interpolation sequence presents the latest frame itself.
                    if (chunk.displayed_xfb_addr == kNoDisplayedXfbAddr) {
                        supersede_interp_sequence();
                        interp_log_waiting_ = false;
                        present_cached_xfb_on_thread(chunk.displayed_xfb_addr, false);
                    } else if (interp_log_waiting_ && renderer_.replay_available()) {
                        supersede_interp_sequence();
                        interp_log_waiting_ = false;
                        start_interp_sequence(interp_log_xfb_);
                        advance_interp_sequence(false);
                    } else if (!interp_sequence_.active) {
                        present_cached_xfb_on_thread(
                            chunk.displayed_xfb_addr,
                            false);
                    } else {
                        ++interp_present_skipped_;
                    }
                } else {
                    if (interp_sequence_.active && interp_sequence_.flush_requested) {
                        advance_interp_sequence(true);
                    }
                    if (const RenderStallInjection& stall = render_stall_injection(); stall.ms != 0u) {
                        static std::uint32_t chunks = 0u;
                        if (++chunks % stall.every == 0u) {
                            std::this_thread::sleep_for(std::chrono::milliseconds(stall.ms));
                        }
                    }
                    render_frame_on_thread(chunk);
                    run_pending_interp_start();
                    {
                        std::lock_guard<std::mutex> lock(queue_mutex_);
                        render_chunk_started_ns_ = 0u;
                    }
                    submit_async_peek_fields_if_requested();
                }
            } else if (idle_present) {
                present_latest_frame_on_thread();
            } else if (overlay_idle) {
                // A due slot presents again the frame last presented (never another: the game's two display copies
                // may differ, and alternating them would flicker) unless a present of the game's own landed in it.
                const std::int64_t now = galaxy::qpc_now();
                if (now >= overlay_idle_due_qpc_) {
                    if (renderer_.last_present_qpc() < overlay_idle_due_qpc_ - interp_slot_period_qpc_) {
                        const std::uint32_t shown = last_presented_xfb_addr_.load(std::memory_order_relaxed);
                        if (shown != 0u) {
                            present_cached_xfb_on_thread(shown, false, false);
                        }
                    }
                    overlay_idle_due_qpc_ += interp_slot_period_qpc_;
                    if (now >= overlay_idle_due_qpc_) {
                        overlay_idle_due_qpc_ += ((now - overlay_idle_due_qpc_) / interp_slot_period_qpc_ + 1) *
                                                 interp_slot_period_qpc_;
                    }
                }
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (have_chunk) {
                if (chunk.efb_peek_only) {
                    if (utility_chunks_in_flight_ != 0u) {
                        --utility_chunks_in_flight_;
                    }
                } else if (chunk.present_only) {
                    if (present_chunks_in_flight_ != 0u) {
                        --present_chunks_in_flight_;
                    }
                } else if (render_chunks_in_flight_ != 0u) {
                    if (chunk.cpu_reads_pending &&
                        render_cpu_reads_in_flight_ != 0u) {
                        --render_cpu_reads_in_flight_;
                        chunk.cpu_reads_pending = false;
                    }
                    if (chunk.fifo_effects_pending &&
                        render_fifo_effects_in_flight_ != 0u) {
                        --render_fifo_effects_in_flight_;
                        chunk.fifo_effects_pending = false;
                    }
                    --render_chunks_in_flight_;
                }
            }
            fail_queued_utilities(std::current_exception());
            break;
        }

        if (have_chunk && chunk.efb_peek_only) {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (utility_chunks_in_flight_ != 0u) {
                --utility_chunks_in_flight_;
            }
            queue_space_cv_.notify_all();
        } else if (have_chunk && chunk.present_only) {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (present_chunks_in_flight_ != 0u) {
                --present_chunks_in_flight_;
            }
            queue_space_cv_.notify_all();
        } else if (have_chunk) {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            // Every chunk returns its buffers here. Past kChunkPoolRetainedBuffers a buffer is
            // dropped (one re-allocation later) rather than kept at its peak capacity.
            if (fifo_pool_.size() < kChunkPoolRetainedBuffers) {
                fifo_pool_.push_back(std::move(chunk.fifo));
            }
            if (chunk.dirty_ranges.capacity() != 0u &&
                dirty_range_pool_.size() < kChunkPoolRetainedBuffers) {
                dirty_range_pool_.push_back(std::move(chunk.dirty_ranges));
            }
            if (!chunk.memory_ranges.empty()) {
                if (memory_range_pool_.size() < kChunkPoolRetainedBuffers) {
                    memory_range_pool_.push_back(std::move(chunk.memory_ranges));
                }
            }
            if (chunk.memory_snapshot_active) {
                // Guest bytes are consumed while command lists are recorded; GPU work references
                // only upload and default-heap resources, which retire on their own fences.
                if (memory_snapshot_pool_.size() < kChunkPoolRetainedBuffers) {
                    memory_snapshot_pool_.push_back(
                        std::move(chunk.memory_snapshot));
                }
            }
            if (render_chunks_in_flight_ != 0u) {
                --render_chunks_in_flight_;
            }
            queue_space_cv_.notify_all();
        }
    }
    } catch (...) {
        // Queue setup and pool returns can fail too; terminal publication must
        // cover the entire worker, not only command recording/fence polling.
        const std::lock_guard<std::mutex> lock(queue_mutex_);
        fail_queued_utilities(std::current_exception());
    }
}

std::size_t GxBackend::async_peek_channel(EfbPeekKind kind, std::uint16_t token) {
    const std::uint32_t key = async_peek_key(kind, token);
    std::size_t oldest = 0;
    for (std::size_t index = 0; index < async_peek_.size(); ++index) {
        if (async_peek_[index].key.load(std::memory_order_relaxed) == key) {
            return index;
        }
        if (async_peek_[index].last_request < async_peek_[oldest].last_request) {
            oldest = index;
        }
    }
    AsyncPeekFieldState& state = async_peek_[oldest];
    state.generation.fetch_add(1u, std::memory_order_acq_rel);
    state.key.store(key, std::memory_order_release);
    return oldest;
}

bool GxBackend::read_async_peek_field(
    AsyncPeekFieldState& state, std::uint16_t x, std::uint16_t y, EfbPeekKind kind, std::uint32_t& value) {
    const std::uint64_t published = state.published.load(std::memory_order_acquire);
    if ((published & 0xFFFFFFFFu) == 0u ||
        (published >> 32u) != state.generation.load(std::memory_order_acquire)) {
        return false;
    }
    const std::uint8_t* field = state.rgba[static_cast<std::size_t>((published & 0xFFFFFFFFu) - 1u)];
    if (field == nullptr) {
        return false;
    }
    // Same packing the full-field conversion produced: 0x00RRGGBB for depth,
    // 0xAARRGGBB for colour.
    const std::uint8_t* pixel =
        field + (static_cast<std::size_t>(y) * kEfbWidth + x) * 4u;
    const std::uint32_t rgb = (std::uint32_t{pixel[0]} << 16u) |
        (std::uint32_t{pixel[1]} << 8u) | pixel[2];
    value = kind == EfbPeekKind::Depth ? rgb : (std::uint32_t{pixel[3]} << 24u) | rgb;
    state.published_answers.fetch_add(1u, std::memory_order_relaxed);
    return true;
}

bool GxBackend::peek_efb_published(
    std::uint16_t x,
    std::uint16_t y,
    EfbPeekKind kind,
    std::uint16_t token,
    std::uint32_t& value) {
    if (!initialized_ || x >= kEfbWidth || y >= kEfbHeight) {
        return false;
    }
    AsyncPeekFieldState& state = async_peek_[async_peek_channel(kind, token)];
    state.last_request = ++async_peek_requests_;
    state.requested.store(true, std::memory_order_release);
    if (read_async_peek_field(state, x, y, kind, value)) {
        return true;
    }
    // A channel just bound: the latest field of the kind, so a new token never stalls on the
    // synchronous path.
    AsyncPeekFieldState* latest = nullptr;
    for (AsyncPeekFieldState& other : async_peek_) {
        const std::uint32_t key = other.key.load(std::memory_order_relaxed);
        if (&other != &state && key != kAsyncPeekUnbound && async_peek_key_kind(key) == kind &&
            (latest == nullptr || other.last_request > latest->last_request) &&
            (other.published.load(std::memory_order_acquire) & 0xFFFFFFFFu) != 0u) {
            latest = &other;
        }
    }
    return latest != nullptr &&
        read_async_peek_field(*latest, x, y, kind, value);
}

void GxBackend::poll_async_peek_fields() {
    for (std::size_t index = 0; index < async_peek_.size(); ++index) {
        if (!renderer_.async_peek_field_pending(index)) {
            continue;
        }
        AsyncPeekFieldState& state = async_peek_[index];
        const int slot = state.pending_slot;
        if (slot < 0) {
            continue;
        }
        if (renderer_.try_complete_async_peek_field(index)) {
            state.pending_slot = -1;
            ++state.completed;
            state.published.store(
                (std::uint64_t{state.pending_generation} << 32u) | static_cast<std::uint32_t>(slot + 1),
                std::memory_order_release);
        }
    }
}

void GxBackend::submit_async_peek_fields_if_requested() {
    for (std::size_t index = 0; index < async_peek_.size(); ++index) {
        AsyncPeekFieldState& state = async_peek_[index];
        if (!state.requested.load(std::memory_order_acquire)) {
            continue;
        }
        // Token channels record at their token (record_async_peek_fields_at_token).
        const std::uint32_t key = state.key.load(std::memory_order_acquire);
        if (key == kAsyncPeekUnbound || (key & 0xFFFFu) != 0u) {
            continue;
        }
        if (renderer_.async_peek_field_pending(index)) {
            // Keep the request alive for the next rendered chunk.
            continue;
        }
        state.requested.store(false, std::memory_order_relaxed);
        // Never the published slot: a reader may be converting its pixel.
        const std::uint64_t published = state.published.load(std::memory_order_relaxed);
        const int slot = (published & 0xFFFFFFFFu) == 0u
            ? 0 : static_cast<int>(published & 0xFFFFFFFFu) % 3;
        const std::uint32_t generation = state.generation.load(std::memory_order_acquire);
        if (renderer_.submit_async_peek_field(
                index, async_peek_key_kind(key), static_cast<std::size_t>(slot)) != 0u) {
            // The slot pointers are fixed once the resources exist; record
            // them before the first publication so readers never race a write.
            if (state.rgba[0] == nullptr) {
                for (std::size_t i = 0; i < state.rgba.size(); ++i) {
                    state.rgba[i] = renderer_.async_peek_field_rgba(index, i);
                }
            }
            state.pending_slot = slot;
            state.pending_generation = generation;
        }
    }
}

void GxBackend::poll_completed_pointer_depth_capture() {
    if (!pending_pointer_depth_capture_.capture) {
        return;
    }
    adopt_pointer_depth_fence_if_needed();
    auto capture = pending_pointer_depth_capture_.capture;
    const std::uint8_t* rgba = nullptr;
    if (!renderer_.try_complete_pointer_depth_capture_in_place(rgba)) {
        return;
    }
    capture->rgba = rgba;
    if (rgba == nullptr) {
        throw std::runtime_error("pointer depth readback is not mapped");
    }
    capture->success = true;
    capture->worker_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() -
            pending_pointer_depth_capture_.started).count());
    // The mutex protects the idle predicate. Completion publication protects
    // the pixel/status stores; no guest code can run on this render owner.
    const std::lock_guard<std::mutex> lock(queue_mutex_);
    pending_pointer_depth_capture_ = {};
    capture->publish_complete();
    queue_space_cv_.notify_all();
}

void GxBackend::drain_pending_pointer_depth_capture() {
    adopt_pointer_depth_fence_if_needed();
    if (pending_pointer_depth_capture_.capture) {
        // Scalar game peeks and a second field share the utility allocator and
        // descriptors. Preserve the first field before either can reuse them.
        renderer_.wait_for_fence_value(
            pending_pointer_depth_capture_.fence_value);
        poll_completed_pointer_depth_capture();
        if (pending_pointer_depth_capture_.capture) {
            throw std::logic_error("completed pointer depth fence was not published");
        }
    }
}

void GxBackend::execute_efb_peek_on_thread(FrameChunk& chunk) {
    drain_pending_pointer_depth_capture();
    if (chunk.pointer_depth_capture) {
        auto& capture=*chunk.pointer_depth_capture;
        const auto start=std::chrono::steady_clock::now();
        try {
            capture.success=renderer_.capture_pointer_depth(capture.pixels);
            if (!capture.success) throw std::runtime_error("owned pointer depth capture failed");
        } catch (...) {
            capture.error=std::current_exception();
            capture.success=false;
            // render_thread_main owns the active request and publishes failure
            // once, together with all pending/queued requests, before stopping.
            throw;
        }
        capture.worker_ns=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());
        capture.publish_complete();
        return;
    }
    EfbPeekRequest* request = chunk.efb_peek_request;
    if (request == nullptr) {
        return;
    }

    try {
        std::uint32_t value = 0;
        const bool success =
            request->depth_pixels.empty()
                ? renderer_.peek_efb(request->x, request->y, request->kind, value)
                : renderer_.capture_pointer_depth(request->depth_pixels);
        {
            std::lock_guard<std::mutex> lock(request->mutex);
            request->value = value;
            request->success = success;
            request->done = true;
            request->cv.notify_one();
        }
    } catch (...) {
        {
            std::lock_guard<std::mutex> lock(request->mutex);
            request->error = std::current_exception();
            request->done = true;
            request->cv.notify_one();
        }
    }
}

void GxBackend::mark_frame_cpu_reads_done(FrameChunk& chunk) {
    if (!chunk.cpu_reads_pending) {
        return;
    }
    galaxy::timeline::record(galaxy::timeline::Event::RenderReadsDone, chunk.pe_completion_token.value);
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (render_cpu_reads_in_flight_ != 0u) {
            --render_cpu_reads_in_flight_;
        }
        chunk.cpu_reads_pending = false;
    }
    queue_space_cv_.notify_all();
}

void GxBackend::mark_frame_fifo_effects_done(FrameChunk& chunk) {
    if (!chunk.fifo_effects_pending) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (!chunk.pe_completion_token) {
            throw std::logic_error(
                "GX frame FIFO effects completed without a frame token");
        }
        frame_pe_completions_.resolve_on_submission(
            chunk.pe_completion_token);
        if (render_fifo_effects_in_flight_ != 0u) {
            --render_fifo_effects_in_flight_;
        }
        chunk.fifo_effects_pending = false;
    }
    queue_space_cv_.notify_all();
}

void GxBackend::queue_frame_fifo_effects_fence(
    FrameChunk& chunk,
    std::uint64_t fence_value) {
    if (!chunk.fifo_effects_pending) {
        return;
    }
    if (chunk.pe_events.empty()) {
        // No PE event in this capture: its token resolves at this submission boundary (after close,
        // submit and signal; before any present). Later work is queue-ordered, so no GPU-idle wait.
        mark_frame_fifo_effects_done(chunk);
        return;
    }
    if (get_gx_timing_config().pe_events_at_consumption) {
        // The capture has been consumed: parsed, every texture decode joined
        // and its command list submitted in queue order. Raise its PE events
        // now, in FIFO order, then resolve the token at this submission
        // boundary exactly like an event-free capture.
        for (const PeEvent& event : chunk.pe_events) {
            deliver_pe_event(chunk.services, event);
            galaxy::timeline::record(
                galaxy::timeline::Event::PeDelivered,
                event.kind == PeEvent::Kind::Finish ? 1u : 2u, event.token);
        }
        chunk.pe_events.clear();
        mark_frame_fifo_effects_done(chunk);
        return;
    }
    PendingFrameEffects pending;
    pending.pe_completion_token = chunk.pe_completion_token;
    pending.fence_value = fence_value;
    pending.diagnostic_fence_bound_ns =
        cadence::steady_now_ns_if_enabled(cadence_diagnostics_);
    pending.services = chunk.services;
    pending.pe_events = std::move(chunk.pe_events);
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (!chunk.pe_completion_token) {
            throw std::logic_error(
                "GX frame GPU effects bound without a frame token");
        }
        pending_frame_effects_.push_back(std::move(pending));
        try {
            frame_pe_completions_.resolve_to_fence(
                chunk.pe_completion_token,
                fence_value);
        } catch (...) {
            pending_frame_effects_.pop_back();
            throw;
        }
        chunk.fifo_effects_pending = false;
    }
    queue_cv_.notify_one();
}

void GxBackend::poll_completed_frame_effects() {
    std::vector<PendingFrameEffects> ready;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (pending_frame_effects_.empty()) {
            return;
        }
        const std::uint64_t completed = renderer_.completed_fence_value();
        const std::uint64_t fence_observed_ns =
            cadence::steady_now_ns_if_enabled(cadence_diagnostics_);
        while (!pending_frame_effects_.empty() &&
               pending_frame_effects_.front().fence_value <= completed) {
            pending_frame_effects_.front().diagnostic_fence_observed_ns =
                fence_observed_ns;
            ready.push_back(std::move(pending_frame_effects_.front()));
            pending_frame_effects_.pop_front();
        }
    }
    for (const PendingFrameEffects& frame : ready) {
        if (cadence_diagnostics_ != nullptr &&
            frame.diagnostic_fence_bound_ns != 0u) {
            cadence_diagnostics_->record_phase(
                cadence::TimingPhase::PeFenceWait,
                frame.diagnostic_fence_observed_ns >=
                        frame.diagnostic_fence_bound_ns
                    ? frame.diagnostic_fence_observed_ns -
                          frame.diagnostic_fence_bound_ns
                    : 0u);
        }
        galaxy::timeline::record(galaxy::timeline::Event::FenceObserved, frame.fence_value,
                                 frame.pe_completion_token.value);
        for (const PeEvent& event : frame.pe_events) {
            deliver_pe_event(frame.services, event);
            galaxy::timeline::record(galaxy::timeline::Event::PeDelivered,
                                 event.kind == PeEvent::Kind::Finish ? 1u : 2u,
                                 event.token);
        }
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            (void)frame_pe_completions_.observe_completed_fence(
                frame.fence_value);
            if (render_fifo_effects_in_flight_ != 0u) {
                --render_fifo_effects_in_flight_;
            }
        }
        queue_space_cv_.notify_all();
    }
}

void GxBackend::deliver_pe_event(
    const GxServices* services,
    const PeEvent& event) {
    if (services == nullptr) {
        throw std::runtime_error(
            "GX PE event reached completion without native services");
    }
    switch (event.kind) {
    case PeEvent::Kind::Finish:
        if (services->gx_pe_finish == nullptr) {
            throw std::runtime_error(
                "GX PE_FINISH reached completion without gx_pe_finish");
        }
        services->gx_pe_finish(services->user);
        break;
    case PeEvent::Kind::Token:
        if (services->gx_pe_token == nullptr) {
            throw std::runtime_error(
                "GX PE_TOKEN reached completion without gx_pe_token");
        }
        services->gx_pe_token(
            services->user,
            event.token,
            event.interrupt);
        break;
    }
}

void GxBackend::present_vi_black(bool in_frame) {
    const XfbTexture* layout = efb_copies_.latest();
    if (layout == nullptr || (vi_black_shown_ && !native_settings_overlay_visible())) {
        return;
    }
    const std::string snapshot_path = present_snapshot_path();
    if (!snapshot_path.empty()) {
        renderer_.set_debug_capture_paths({}, snapshot_path);
    }
    if (!in_frame) {
        renderer_.begin_frame(true);
    }
    renderer_.present(layout, !snapshot_path.empty(), true);
    if (!snapshot_path.empty()) {
        renderer_.set_debug_capture_paths({}, {});
    }
    if (!in_frame) {
        renderer_.end_frame();
    }
    vi_black_shown_ = true;
    // The frame the VI shows next is new to the display, even when it repeats the last XFB.
    last_presented_xfb_serial_.store(~std::uint64_t{0}, std::memory_order_relaxed);
}

void GxBackend::present_latest_frame_on_thread() {
    present_cached_xfb_on_thread(
        kUnknownDisplayedXfbAddr,
        true,
        false);
}

void GxBackend::present_cached_xfb_on_thread(
    std::uint32_t displayed_xfb_addr,
    bool sleep_if_missing,
    bool count_game_xfb_present) {
    if (count_game_xfb_present) {
        ++stat_xfb_present_requests_;
    }
    const std::uint64_t current_present_epoch =
        present_epoch_ + (count_game_xfb_present ? 1u : 0u);
    const bool display_xfb_unknown =
        displayed_xfb_addr == kUnknownDisplayedXfbAddr;
    const bool display_xfb_none =
        displayed_xfb_addr == kNoDisplayedXfbAddr;
    if (display_xfb_none) {
        if (count_game_xfb_present) {
            present_epoch_ = current_present_epoch;
            present_vi_black(false);
        }
        return;
    }
    vi_black_shown_ = false;
    const bool requested_specific_xfb =
        displayed_xfb_addr != 0u && !display_xfb_unknown &&
        !display_xfb_none;
    if (requested_specific_xfb) {
        note_vi_named_xfb(displayed_xfb_addr);
    }
    const XfbTexture* latest = efb_copies_.latest();
    const std::uint32_t last_presented_addr =
        last_presented_xfb_addr_.load(std::memory_order_relaxed);
    const XfbTexture* last_valid = last_presented_addr != 0u
        ? efb_copies_.find_xfb(last_presented_addr)
        : nullptr;
    const XfbTexture* selected = display_xfb_none
        ? nullptr
        : (requested_specific_xfb
            ? displayed_xfb(efb_copies_.find_xfb(displayed_xfb_addr), latest,
                            latest != nullptr && is_vi_named_xfb(latest->guest_addr), subframes_shown())
            : latest);
    const std::uint64_t current_xfb_stamp =
        latest != nullptr
            ? std::max(latest->frame_stamp, current_present_epoch)
            : std::max(frame_index_, current_present_epoch);
    const XfbPresentSelection selection = display_xfb_none
        ? XfbPresentSelection{}
        : select_xfb_for_present(
            requested_specific_xfb,
            selected,
            latest,
            last_valid,
            current_xfb_stamp,
            kMaxXfbPreserveAge);
    selected = selection.selected;
    if (count_game_xfb_present && selection.stale_preserved) {
        ++stat_xfb_present_stale_preserved_;
    }
    if (count_game_xfb_present && selection.stale_expired) {
        ++stat_xfb_present_stale_fallbacks_;
    }
    if (count_game_xfb_present && selection.missing) {
        ++stat_xfb_present_misses_;
    }
    if (count_game_xfb_present && selection.missing_preserved) {
        ++stat_xfb_present_missing_preserved_;
    }
    if (count_game_xfb_present && selection.missing_expired) {
        ++stat_xfb_present_missing_fallbacks_;
    }
    if (selected == nullptr) {
        if (sleep_if_missing) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return;
    }
    const bool duplicate_stamp =
        selected->copy_serial ==
        last_presented_xfb_serial_.load(std::memory_order_relaxed);
    const bool skip_duplicate_present =
        count_game_xfb_present &&
        duplicate_stamp &&
        !native_settings_overlay_visible();
    if (count_game_xfb_present && !skip_duplicate_present &&
        !native_settings_overlay_visible() && skip_for_present_rate()) {
        return;
    }
    if (skip_duplicate_present) {
        record_skipped_duplicate_xfb_present(*selected);
        return;
    }
    const std::string snapshot_path = !interp_snapshot_path_.empty() ? interp_snapshot_path_
        : !count_game_xfb_present ? std::string{} : present_snapshot_path();
    if (!snapshot_path.empty()) {
        renderer_.set_debug_capture_paths({}, snapshot_path);
    }
    const bool game_frame_only =
        interp_recording_ && count_game_xfb_present && !interp_presenting_real_ && !duplicate_stamp;
    if (game_frame_only) {
        note_interp_real_present();
    }
    renderer_.set_present_game_frame(game_frame_only);
    bool replayed = false;
    if (interp_presenting_real_ && renderer_.temporal_interpolation()) {
        // The temporal upscaler takes every presented frame from the replays:
        // the sequence's frame at t = 1 too (one history).
        renderer_.set_subframe_capture(!snapshot_path.empty());
        replayed = renderer_.replay_playing() && render_interp_subframe(1.0f);
        renderer_.set_subframe_capture(false);
        if (!replayed) {
            // Shown without the upscaler: the next sequence starts its history again.
            interp_shown_serial_ = 0u;
            interp_shown_t_ = 0.0f;
        }
    } else if (interp_presenting_real_) {
        interp_shown_serial_ = interp_sequence_serial_;
        interp_shown_t_ = 1.0f;
    }
    if (!replayed) {
        renderer_.begin_frame(true);
        // Present-only frames acquire no XFB textures, so EfbCopyManager retirement advances with
        // render frames only; otherwise render-then-present would age XFBs twice per VI.
        (void)renderer_.present(selected, !snapshot_path.empty());
    }
    if (!snapshot_path.empty()) {
        renderer_.set_debug_capture_paths({}, {});
    }
    if (!replayed) {
        renderer_.end_frame();
    }
    if (count_game_xfb_present) {
        present_epoch_ = current_present_epoch;
        record_presented_xfb_copy(*selected);
        last_presented_xfb_addr_.store(
            selected->guest_addr, std::memory_order_relaxed);
    }
    renderer_.debug_log_backbuffer_readback();
}

void GxBackend::render_frame_on_thread(FrameChunk& chunk) {
    render_pointer_response_=chunk.pointer_response;
    render_movie_content_=chunk.prerecorded_movie;
    render_safety_surround_=chunk.safety_surround;
    render_movie_frame_=chunk.movie_frame;
    const bool trace_gx_stalls = trace_gx_stalls_enabled();
    const bool defer_pe_events_to_gpu_fence =
        get_gx_timing_config().pe_events_gpu_fence;
    // Per-draw clock reads run on stall-traced and sampled frames only.
    frame_time_sample_ = frame_gx_timing_sample_enabled(frame_index_);
    frame_time_detail_ = trace_gx_stalls || frame_time_sample_;
    // Thread CPU time and cycles of the frame and of its PSO resolution, on request or with the stall trace.
    pso_cycles_enabled = pso_cycles_requested() || trace_gx_stalls;
    // Per-frame phase timing takes a few clock reads and is always on.
    constexpr bool frame_timing_enabled = true;
    const ThreadCounterSample frame_cpu_start = pso_cycles_enabled
        ? thread_cpu_time_100ns() : ThreadCounterSample{};
    const ThreadCounterSample frame_cycles_start = pso_cycles_enabled
        ? thread_cycle_count() : ThreadCounterSample{};
    const auto render_start = frame_timing_enabled
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    frame_draws_issued_ = 0;
    frame_draw_batches_ = 0;
    frame_draws_no_pso_ = 0;
    frame_xfb_copies_ = 0;
    frame_tex_copies_ = 0;
    frame_efb_alias_updates_ = 0;
    frame_efb_alias_noops_ = 0;
    frame_texture_binding_cache_hits_ = 0;
    frame_texture_binding_cache_misses_ = 0;
    frame_texture_binding_cache_prunes_ = 0;
    frame_nontri_skips_ = 0;
    frame_scissor_skips_ = 0;
    frame_flush_us_ = 0;
    frame_flush_pso_us_ = 0;
    frame_flush_pso_cycles_ = 0;
    frame_flush_pso_cycles_valid_ = pso_cycles_enabled;
    frame_flush_total_us_ = 0;
    frame_flush_timed_samples_ = 0;
    // frame_flush_timed_calls_ runs across frames (ScopedFlushTiming).
    frame_pso_memo_hits_ = 0;
    frame_pso_get_calls_ = 0;
    frame_pso_get_us_ = 0;
    frame_pso_get_max_us_ = 0;
    frame_flush_calls_ = 0;
    frame_pso_resolutions_ = 0;
    frame_pso_route_early_ = 0;
    // Baseline for the per-frame pipeline-compile deltas on the [gx-frame-timing] line.
    frame_compile_stats_baseline_ = pipeline_cache_.compile_stats();
    frame_replay_prepared_batches_ = 0;
    frame_replay_packet_batches_ = 0;
    frame_native_shape_draws_ = 0;
    frame_native_shape_misses_ = 0;
    frame_replay_simple_batches_ = 0;
    frame_replay_generic_draws_ = 0;
    for (PipelineMemoEntry& entry : pipeline_memo_) {
        entry.hash = 0u;
    }
    frame_flush_matrix_us_ = 0;
    frame_matrix_upload_bytes_ = 0;
    frame_flush_constants_us_ = 0;
    frame_flush_texture_us_ = 0;
    frame_texture_decode_join_us_ = 0;
    frame_vertex_load_us_ = 0;
    frame_cached_vertex_validation_us_ = 0;
    frame_cached_vertex_upload_us_ = 0;
    frame_draw_record_us_ = 0;
    frame_efb_copy_us_ = 0;
    frame_resident_vertex_hits_ = 0;
    frame_resident_vertex_bytes_ = 0;
    frame_curved_flat_triangles_ = 0;
    frame_curved_patch_triangles_ = 0;
    frame_curved_split_draws_ = 0;
    frame_clear_quads_ = 0;
    pending_draw_batch_ = {};
    frame_fifo_profile_.reset();
    const std::byte* fifo_data =
        chunk.fifo.empty() ? nullptr : chunk.fifo.data();
    const std::size_t fifo_size = chunk.fifo.size();
    GuestMemory* memory = chunk.memory_snapshot_active
        ? &chunk.memory_snapshot.memory
        : chunk.memory;
    const GxServices* services = chunk.services;
    coalesce_dirty_ranges(chunk.dirty_ranges);
    chunk.dirty_range_count = chunk.dirty_ranges.size();
    chunk.dirty_bytes = dirty_range_bytes(chunk.dirty_ranges);

    // begin_frame() drains PSO completions and rewinds ring segments.
    {
        const cadence::ScopedPhaseTimer render_begin_timer{
            cadence_diagnostics_, cadence::TimingPhase::RenderBegin};
        {
            // GALAXY_TEST_RT_EFFECT_AB: the frame's parity by the present it ends in (the snapshots' numbering, as
            // GALAXY_RT_AB), set for all its chunks before anything asks rt_effect_on.
            set_rt_effect_frame(g_snapshot_presents + 1u);
            const RenderConfig render = get_render_config();
            // Under ray-traced lighting the traced light replaces enhanced lighting (de-lit characters relit by
            // the trace wash out), and the draws it does not relight (water, glass) are lit per pixel: the
            // ray-traced picture is tuned with it, and the menu hides LIGHTING while ray tracing is on.
            const bool enhanced = render.enhanced_lighting && !renderer_.rt_lighting();
            const bool per_pixel = render.per_pixel_lighting || renderer_.rt_lighting();
            // GALAXY_TEST_CURVED_AB=1: curved surfaces off in frames that end in an odd present (same-run pictures).
            static const bool curved_ab = read_env_u64("GALAXY_TEST_CURVED_AB", 0u) != 0u;
            curved_surfaces_ = render.curved_surfaces && curved_surfaces_offered() &&
                               !(curved_ab && ((g_snapshot_presents + 1u) & 1u) != 0u);
            // GALAXY_TEST_CURVED_SPLIT_AB=1: a curved draw's flat triangles stay patches in those frames.
            static const bool split_ab = read_env_u64("GALAXY_TEST_CURVED_SPLIT_AB", 0u) != 0u;
            curved_split_ = !(split_ab && ((g_snapshot_presents + 1u) & 1u) != 0u);
            // GALAXY_TEST_CLEAR_QUAD_AB=1: the game's clear quads draw as written in those frames.
            static const bool clear_ab = read_env_u64("GALAXY_TEST_CLEAR_QUAD_AB", 0u) != 0u;
            clear_quads_ = !(clear_ab && ((g_snapshot_presents + 1u) & 1u) != 0u);
            rt_water_wanted_ = rt_effect_on(render, RtEffect::Water);
            ambient_occlusion_ =
                render.ambient_occlusion && !renderer_.rt_lighting() && renderer_.ambient_occlusion_ready();
            // A lighting-mode change re-keys every lit draw's pipeline.
            if (per_pixel != per_pixel_lighting_ || enhanced != enhanced_lighting_) {
                per_pixel_lighting_ = per_pixel;
                enhanced_lighting_ = enhanced;
                shader_key_hashes_valid_ = false;
                current_pipeline_ = nullptr;
            }
            enhanced_ps_flags_ = (renderer_.rt_reflections_active() ? kEnhancedRtReflections : 0u) |
                                 (renderer_.rt_lighting() ? kEnhancedRtLighting : 0u);
        }
        renderer_.begin_frame(chunk.present_swap_chain);
        xfb_copied_in_chunk_ = false;
        {
            // Interpolated redraw: present rate above 60 with the temporal
            // upscaler off. Decided per render chunk.
            const RenderConfig cfg = get_render_config();
            const double rate = present_frame_rate(
                presented_max_fps(cfg), cfg.vsync_interval, renderer_.display_refresh_hz());
            // The renderer keeps the log per game frame (interpolation_mode).
            interp_recording_ = renderer_.interpolation_mode();
            const unsigned frames = interp_recording_ ? interpolated_frames_per_game_frame(rate) : 0u;
            if (frames != interp_frames_) {
                interp_frames_ = frames;
                restart_interp_governors();
            }
            if (!interp_recording_) {
                interp_shown_scene_ = -1;
            }
            const bool wanted =
                interp_recording_ || renderer_.temporal_mode() || renderer_.rt_active();
            if (draw_identity_wanted_.load(std::memory_order_relaxed) != wanted) {
                draw_identity_wanted_.store(wanted, std::memory_order_relaxed);
            }
            // How the game's bloom goes through the renderer (NEBULA_OPTION_BLOOM): without the renderer's bloom, not
            // at all; NATIVE, its own chain over a scene the renderer keeps; ENHANCED, drawn by the renderer.
            // GALAXY_TEST_BLOOM_ROUTE=0-2 (tests): that route, 0 the console's chain as it is for comparison.
            static const std::uint64_t test_route = read_env_u64("GALAXY_TEST_BLOOM_ROUTE", 3u);
            const unsigned route = !renderer_.game_bloom_ready()                          ? 0u
                                   : test_route <= 2u                                  ? static_cast<unsigned>(test_route)
                                   : get_render_config().bloom_mode == kBloomEnhanced ? 2u
                                                                                       : 1u;
            bloom_route_.store(route, std::memory_order_relaxed);
            // The game draws for traced light: the whole stage at full detail, and out-of-view shadow casters and
            // light sources (NEBULA_OPTION_RT_SHADOW_CASTERS; GALAXY_RT_OFFSCREEN_CASTERS=0: as without it).
            static const bool offscreen_casters = read_env_u64("GALAXY_RT_OFFSCREEN_CASTERS", 1u) != 0u;
            rt_shadow_casters_wanted_.store(offscreen_casters && renderer_.rt_lighting(), std::memory_order_relaxed);
            interp_scenes_in_chunk_ = 0u;
        }
        // begin_frame() applies a pending internal-resolution change, so copies take the scale the EFB
        // has now, not the setting, which can change in between.
        if (renderer_.efb_scale() != efb_copies_.efb_scale()) {
            efb_copies_.set_efb_scale(renderer_.efb_scale());
        }
        // begin_frame() waited for this slot's previous frame: resident vertex
        // blocks released kFramesInFlight render frames ago are now unused.
        // Interpolation replays a game frame's draws until its ring segment
        // comes round again (after its fence): resident blocks and texture
        // resources then retire per game frame, over the segment count.
        {
            static_assert(kInterpSegments <= TextureCache::kMaxHoldSegments &&
                          kInterpSegments >= kFramesInFlight);
            const unsigned hold = renderer_.replay_hold_segment();
            if (hold != resident_hold_segment_) [[unlikely]] {
                // Interpolation started or stopped, or a game frame began.
                resident_hold_segment_ = hold;
                resident_vertices_.set_frames_in_flight(
                    hold == ~0u ? kFramesInFlight : kInterpSegments);
                if (hold != ~0u) {
                    resident_vertices_.advance_frame();
                }
                texture_cache_.set_retirement_hold_segment(hold);
            }
            if (hold == ~0u) [[likely]] {
                resident_vertices_.advance_frame();
            }
        }
        efb_copies_.begin_frame(renderer_.frame_slot(), kFramesInFlight);
        texture_cache_.begin_frame(renderer_.frame_slot(), kFramesInFlight);
        const auto retirement_revision = texture_cache_.retirement_revision();
        if (retirement_revision != seen_texture_retirement_revision_) {
            // No descriptor allocation has happened yet this frame. Reclaimed
            // slots still carry their retired bits, so old handles are removed
            // before reuse without discarding unrelated cross-frame hits.
            std::erase_if(texture_handle_cache_, [this](const auto& item) {
                return texture_cache_.srv_index_retired(item.second.srv_index);
            });
            seen_texture_retirement_revision_ = retirement_revision;
        }
        clear_frame_texture_binding_table_cache();
        pipeline_cache_.drain_completions();
        // Publish point for PipelineCache::published_. Every pipeline the memo
        // can hold is either already visible here or is resolved by get() on
        // this same thread later in the frame, so the memo stays valid until
        // this clear runs again at the start of the next frame.
        for (PipelineMemoEntry& entry : pipeline_memo_) {
            entry.hash = 0u;
        }
        // New decoded textures upload through this frame's command list.
        renderer_.attach_texture_uploads(texture_cache_);
        {
            const RenderConfig render = get_render_config();
            if (const std::uint64_t generation = texture_pack_mod_folders_generation();
                generation != texture_pack_mods_generation_) {
                texture_pack_mods_generation_ = generation;
                texture_pack_mod_folders_ = texture_pack_mod_folders();
            }
            static const std::filesystem::path dumps = texture_dump_directory();
            TexturePackSettings settings{};
            settings.enabled = render.texture_packs;
            settings.preload = render.texture_pack_preload;
            settings.dump = render.texture_dump;
            // Each mod's pack, the lowest in the mods list first (the last wins a texture).
            if (std::filesystem::path packs = texture_pack_test_directory(); !packs.empty()) {
                settings.packs.push_back(std::move(packs));
            }
            settings.packs.insert(settings.packs.end(), texture_pack_mod_folders_.begin(),
                                  texture_pack_mod_folders_.end());
            settings.dumps = dumps;
            settings.budget_bytes = static_cast<std::uint64_t>(render.texture_pack_budget_mb) << 20u;
            if (!texture_pack_settings_applied_ || !(settings == texture_pack_settings_)) {
                texture_cache_.configure_texture_packs(settings);
                texture_pack_settings_ = std::move(settings);
                texture_pack_settings_applied_ = true;
            }
        }
        // Finished replacements upload here and take the place of their originals.
        texture_cache_.apply_texture_packs();
        // HD movie replacements: the movies folder beside the settings, then the mods' movies/ folders.
        if (const std::uint64_t generation = movie_mod_folders_generation();
            generation != movie_mods_generation_) {
            movie_mods_generation_ = generation;
            std::vector<std::filesystem::path> folders;
            if (std::filesystem::path movies = movie_directory(); !movies.empty()) {
                folders.push_back(std::move(movies));
            }
            for (std::filesystem::path& folder : movie_mod_folders()) folders.push_back(std::move(folder));
            movie_replacements_.configure(std::move(folders), [this](std::uint64_t size) {
                return texture_cache_.backend().create_upload_chunk(size);
            });
        }
    }
    // The movie frame this submission draws: a replacement's planes in place of the THP's (each submission, so the
    // frame matches the draw that carries it). Handles cached for the planes' addresses are stale when they change.
    if (texture_cache_.backend().has_upload_list() &&
        (render_movie_frame_.frame != MovieFrameMark::kNone || texture_cache_.movie_override_active())) {
        if (++movie_stats_tick_ % 240u == 0u) {
            const MovieReplacements::Stats stats = movie_replacements_.stats();
            if (stats.decoded != 0u) {
                std::fprintf(stderr, "[movie] decoded=%llu shown=%llu late=%llu seeks=%llu convert-ms=%.2f/frame\n",
                             static_cast<unsigned long long>(stats.decoded),
                             static_cast<unsigned long long>(stats.shown),
                             static_cast<unsigned long long>(stats.late), static_cast<unsigned long long>(stats.seeks),
                             stats.convert_ms / static_cast<double>(stats.decoded));
            }
        }
        if (texture_cache_.set_movie_override(render_movie_frame_, movie_replacements_.frame(render_movie_frame_))) {
            const std::uint32_t planes[3] = {render_movie_frame_.y, render_movie_frame_.u, render_movie_frame_.v};
            const auto stale = [&](const auto& item) {
                return item.first.guest_addr == planes[0] || item.first.guest_addr == planes[1] ||
                       item.first.guest_addr == planes[2] || texture_cache_.movie_handle(item.second.srv_index);
            };
            std::erase_if(texture_handle_cache_, stale);
            std::erase_if(frame_texture_handle_cache_, stale);
            clear_texture_binding_table_cache();
            texture_bindings_dirty_ = true;
        }
    }
    // Workers may be filling this frame's texture staging from the chunk's
    // memory snapshot; no exit from this function may leave them running.
    struct TextureDecodeJoin {
        TextureCache& cache;
        ~TextureDecodeJoin() { cache.finish_pending_decodes_noexcept(); }
    } texture_decode_join{texture_cache_};
    std::size_t dirty_texture_evictions = 0u;
    std::unique_lock<std::mutex> parser_lock;
    {
        const cadence::ScopedPhaseTimer cache_invalidation_timer{
            cadence_diagnostics_,
            cadence::TimingPhase::RenderCacheInvalidation};
        {
            const cadence::ScopedPhaseTimer timer{
                cadence_diagnostics_, cadence::TimingPhase::RenderTextureInvalidation};
            dirty_texture_evictions =
                invalidate_dirty_texture_ranges(chunk.dirty_ranges);
        }
        {
            const cadence::ScopedPhaseTimer timer{
                cadence_diagnostics_, cadence::TimingPhase::RenderParserLockWait};
            parser_lock = std::unique_lock<std::mutex>(parser_cache_mutex_);
        }
    }
    // GPU addresses and tables belong to the ring segment begin_frame() selected, so every binding
    // is written again in this frame slot even when the GX state did not change.
    frame_bindings_dirty_ = true;
    current_matrix_palette_ = 0;
    matrix_palette_stale_ = true;
    inline_matrices_stale_ = true;
    // The matrix ring segment for this frame slot is reused from the previous round trip, so a
    // palette chunk uploaded then is not at a known offset. Re-arm every chunk; the first palette
    // upload of the frame writes the full bank again.
    xf_palette_valid_ = 0u;
    scene_chunk_restart_ = true;

    // Bind EFB with default (null) viewport. The XF viewport registers
    // will provide the actual values once processed from the FIFO.
    renderer_.bind_efb(nullptr);

    // GX keeps the EFB across frames; only an EFB copy with the clear bit set clears it (on_efb_copy,
    // with the latched copy-clear registers). One initial clear (depth = far) seeds the targets.
    if (!efb_seeded_) {
        efb_seeded_ = true;
        renderer_.clear_efb(0u, 0u, 0x00FFFFFFu, true, true, true);
    }

    parser_.set_profile(
        trace_gx_stalls ? &frame_fifo_profile_ : nullptr);
    const auto parse_start = frame_timing_enabled
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    const std::uint64_t diagnostic_parse_start_ns =
        cadence::steady_now_ns_if_enabled(cadence_diagnostics_);
    if (fifo_data != nullptr && fifo_size > 0) {
        const bool saved_pe_callbacks_enabled = frame_pe_callbacks_enabled_;
        std::vector<PeEvent>* const saved_pe_events = frame_pe_events_;
        frame_pe_callbacks_enabled_ = !chunk.pe_events_scanned_on_sim_thread;
        frame_pe_events_ =
            defer_pe_events_to_gpu_fence ? &chunk.pe_events : nullptr;
        frame_memory_   = memory;
        frame_memory_regions_disjoint_sorted_ = chunk.memory_snapshot_active &&
            chunk.memory_snapshot.sealed &&
            memory == &chunk.memory_snapshot.memory &&
            chunk.memory_snapshot.regions_disjoint_sorted;
        frame_services_ = services;
        // A sealed snapshot keeps texture bytes unchanged until this chunk is submitted, so first-use
        // decodes may run on workers. With live-memory reads a worker reads the bytes within the same
        // window the render thread would.
        texture_cache_.set_async_decode_allowed(
            ((chunk.memory_snapshot_active && chunk.memory_snapshot.sealed &&
              memory == &chunk.memory_snapshot.memory) ||
             (!chunk.memory_snapshot_active &&
              get_gx_timing_config().render_live_memory_reads)));

        try {
            // An in-frame pointer depth field splits the run at the marked
            // offset: everything before it is drawn, the field is recorded
            // into this frame's command list, then the rest runs. A command
            // straddling the marker simply finishes in the second run.
            // Scene markers split the run the same way, in stream order; a
            // marker never lands inside a command (the producer flushes the
            // gather pipe at the guest call boundary).
            const bool capture_in_frame =
                chunk.pointer_depth_in_frame && chunk.pointer_depth_capture != nullptr;
            const auto run_stream = [&](std::span<const std::byte> stream,
                                        std::size_t prefix) -> std::size_t {
                std::vector<StreamSplit>& splits = stream_splits_;
                splits.clear();
                if (capture_in_frame) {
                    splits.push_back({prefix + chunk.pointer_depth_capture_fifo_offset, 0u, -1, -1});
                }
                for (std::uint8_t i = 0; i < chunk.fifo_marker_count; ++i) {
                    splits.push_back({prefix + chunk.fifo_markers[i].first, chunk.fifo_marker_serials[i],
                                      static_cast<int>(chunk.fifo_markers[i].second), static_cast<int>(i)});
                }
                for (std::size_t i = 0; i < chunk.shape_records.size(); ++i) {
                    splits.push_back({prefix + chunk.shape_records[i].offset, chunk.shape_records[i].serial,
                                      kSplitShapeRecord, static_cast<int>(i)});
                }
                // In stream order; at one offset in the order the game made them (a marker before the records
                // made after it).
                std::stable_sort(splits.begin(), splits.end(), [](const StreamSplit& a, const StreamSplit& b) {
                    return a.offset != b.offset ? a.offset < b.offset
                         : a.serial != b.serial ? a.serial < b.serial
                                                : (a.marker == kSplitShapeRecord) < (b.marker == kSplitShapeRecord);
                });
                std::size_t consumed = 0;
                for (std::size_t i = 0; i < splits.size(); ++i) {
                    const StreamSplit split = splits[i];
                    if (split.offset > stream.size()) {
                        continue;
                    }
                    if (split.offset > consumed) {
                        parse_span_base_ = consumed;
                        parse_span_prefix_ = prefix;
                        consumed += parser_.run_available(
                            stream.subspan(consumed, split.offset - consumed),
                            memory, *this, state_,
                            frame_memory_regions_disjoint_sorted_);
                    }
                    if (split.marker == kSplitShapeRecord) {
                        on_shape_record(chunk.shape_records[static_cast<std::size_t>(split.view)]);
                    } else if (split.marker < 0) {
                        record_pointer_depth_capture_in_frame(chunk);
                    } else if (split.marker == static_cast<int>(FifoMarker::Scene2DBegin)) {
                        on_scene_2d_begin();
                    } else if (split.marker != static_cast<int>(FifoMarker::Scene2DEnd)) {
                        const auto index = static_cast<std::size_t>(split.view);
                        const bool sky_up = split.view >= 0 && (chunk.fifo_marker_sky_up_mask & (1u << index)) != 0u;
                        on_fifo_marker(static_cast<FifoMarker>(split.marker),
                            split.view >= 0 &&
                                    (chunk.fifo_marker_view_mask & (1u << index)) != 0u
                                ? &chunk.fifo_marker_views[index]
                                : nullptr,
                            sky_up ? chunk.fifo_marker_sky_up[index] : 0u, sky_up,
                            split.view >= 0 ? &chunk.fifo_marker_stages[index] : nullptr);
                    }
                }
                if (consumed < stream.size()) {
                    parse_span_base_ = consumed;
                    parse_span_prefix_ = prefix;
                    consumed += parser_.run_available(
                        stream.subspan(consumed), memory, *this, state_,
                        frame_memory_regions_disjoint_sorted_);
                }
                return consumed;
            };
            parse_stripes_ = interp_recording_ ? std::span<const StripeParticleMark>(chunk.stripe_particles)
                                               : std::span<const StripeParticleMark>{};
            parse_stripe_next_ = 0u;
            if (pending_fifo_.empty()) {
                const std::span<const std::byte> fifo(fifo_data, fifo_size);
                const std::size_t consumed = run_stream(fifo, 0u);
                if (consumed < fifo.size()) {
                    pending_fifo_.assign(
                        fifo.begin() + static_cast<std::ptrdiff_t>(consumed),
                        fifo.end());
                }
            } else {
                const std::size_t pending_prefix = pending_fifo_.size();
                if (fifo_data != nullptr && fifo_size > 0) {
                    pending_fifo_.insert(
                        pending_fifo_.end(), fifo_data, fifo_data + fifo_size);
                }
                const std::size_t consumed =
                    run_stream(pending_fifo_, pending_prefix);
                if (consumed == pending_fifo_.size()) {
                    pending_fifo_.clear();
                } else if (consumed > 0) {
                    pending_fifo_.erase(
                        pending_fifo_.begin(),
                        pending_fifo_.begin() +
                            static_cast<std::ptrdiff_t>(consumed));
                }
            }
            parse_stripes_ = {};
            if (!chunk.stripe_particles.empty()) [[unlikely]] {
                note_stripe_particles(chunk);
            }
        } catch (const GxFatalError& e) {
            texture_cache_.set_async_decode_allowed(false);
            texture_cache_.finish_pending_decodes_noexcept();
            parser_.set_profile(nullptr);
            frame_memory_ = nullptr;
            frame_memory_regions_disjoint_sorted_ = false;
            frame_services_ = nullptr;
            frame_pe_events_ = saved_pe_events;
            frame_pe_callbacks_enabled_ = saved_pe_callbacks_enabled;
            std::cerr << "[GxBackend] fatal FIFO error: " << e.what()
                      << " at offset 0x" << std::hex << e.fifo_offset()
                      << " opcode=0x" << static_cast<unsigned>(e.opcode())
                      << std::dec << '\n';
            throw;
        } catch (...) {
            texture_cache_.set_async_decode_allowed(false);
            texture_cache_.finish_pending_decodes_noexcept();
            parser_.set_profile(nullptr);
            frame_memory_ = nullptr;
            frame_memory_regions_disjoint_sorted_ = false;
            frame_services_ = nullptr;
            frame_pe_events_ = saved_pe_events;
            frame_pe_callbacks_enabled_ = saved_pe_callbacks_enabled;
            throw;
        }

        texture_cache_.set_async_decode_allowed(false);
        frame_memory_   = nullptr;
        frame_memory_regions_disjoint_sorted_ = false;
        frame_services_ = nullptr;
        frame_pe_events_ = saved_pe_events;
        frame_pe_callbacks_enabled_ = saved_pe_callbacks_enabled;
    }
    parser_.set_profile(nullptr);
    parser_lock.unlock();
    if (cadence_diagnostics_ != nullptr) {
        const std::uint64_t diagnostic_parse_end_ns =
            cadence::steady_now_ns_if_enabled(cadence_diagnostics_);
        cadence_diagnostics_->record_phase(
            cadence::TimingPhase::RenderFifoParse,
            diagnostic_parse_end_ns >= diagnostic_parse_start_ns
                ? diagnostic_parse_end_ns - diagnostic_parse_start_ns
                : 0u);
    }
    const auto parse_end = frame_timing_enabled
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    const auto post_parse_flush_start = frame_timing_enabled
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    {
        const cadence::ScopedPhaseTimer post_parse_flush_timer{
            cadence_diagnostics_,
            cadence::TimingPhase::RenderPostParseFlush};
        flush_pending_draw_batch();
    }
    const auto post_parse_flush_end = frame_timing_enabled
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    const std::uint64_t post_parse_flush_us = frame_timing_enabled
        ? elapsed_us(post_parse_flush_start, post_parse_flush_end)
        : 0u;
    mark_frame_cpu_reads_done(chunk);

    // Every VI presents once a valid XFB exists, including retraces that queued no FIFO, or the
    // flip-model cadence collapses into gaps. Before the first XFB the window stays hidden.
    if (chunk.displayed_xfb_addr != 0u && chunk.displayed_xfb_addr != kUnknownDisplayedXfbAddr &&
        chunk.displayed_xfb_addr != kNoDisplayedXfbAddr) {
        note_vi_named_xfb(chunk.displayed_xfb_addr);
    }
    // The chunk with a game frame's display copy presents it (present_on_copy_enabled); a VI presents the newest.
    // Not while the VI is black, the settings overlay draws over every present, or presents are paced elsewhere
    // (interpolation, a temporal upscaler, frame generation).
    const bool copy_present = !chunk.present_swap_chain && xfb_copied_in_chunk_ && present_on_copy_enabled() &&
        !interp_recording_ && !vi_black_shown_ && !native_settings_overlay_visible() && !renderer_.temporal_mode() &&
        !frame_generation_running();
    const bool presents = chunk.present_swap_chain || copy_present;
    if (presents) {
        ++stat_xfb_present_requests_;
    }
    const std::uint64_t current_present_epoch =
        presents ? present_epoch_ + 1u : present_epoch_;
    const bool display_xfb_unknown =
        chunk.displayed_xfb_addr == kUnknownDisplayedXfbAddr;
    const bool display_xfb_none =
        chunk.displayed_xfb_addr == kNoDisplayedXfbAddr;
    const bool requested_specific_xfb =
        chunk.displayed_xfb_addr != 0u && !display_xfb_unknown &&
        !display_xfb_none;
    const XfbTexture* latest_xfb = efb_copies_.latest();
    const std::uint32_t last_presented_addr =
        last_presented_xfb_addr_.load(std::memory_order_relaxed);
    const XfbTexture* last_valid_xfb = last_presented_addr != 0u
        ? efb_copies_.find_xfb(last_presented_addr)
        : nullptr;
    const XfbTexture* selected_xfb = display_xfb_none
        ? nullptr
        : (requested_specific_xfb
            ? displayed_xfb(efb_copies_.find_xfb(chunk.displayed_xfb_addr), latest_xfb,
                            latest_xfb != nullptr && is_vi_named_xfb(latest_xfb->guest_addr), subframes_shown())
            : latest_xfb);
    const std::uint64_t current_xfb_stamp =
        latest_xfb != nullptr
            ? std::max(latest_xfb->frame_stamp, current_present_epoch)
            : std::max(frame_index_, current_present_epoch);
    const XfbPresentSelection selection = display_xfb_none
        ? XfbPresentSelection{}
        : select_xfb_for_present(
            requested_specific_xfb,
            selected_xfb,
            latest_xfb,
            last_valid_xfb,
            current_xfb_stamp,
            kMaxXfbPreserveAge);
    selected_xfb = selection.selected;
    bool stale_fallback = false;
    bool missing_fallback = false;
    if (presents && selection.stale_preserved) {
        ++stat_xfb_present_stale_preserved_;
    }
    if (presents && selection.stale_expired) {
        stale_fallback = true;
        ++stat_xfb_present_stale_fallbacks_;
    }
    if (presents && selection.missing) {
        ++stat_xfb_present_misses_;
    }
    if (presents && selection.missing_preserved) {
        ++stat_xfb_present_missing_preserved_;
    }
    if (presents && selection.missing_expired) {
        missing_fallback = true;
        ++stat_xfb_present_missing_fallbacks_;
    }
    if (selection.missing) {
    }
    if (selected_xfb == nullptr && !requested_specific_xfb &&
        !display_xfb_none) {
        selected_xfb = efb_copies_.latest();
    }
    const bool duplicate_xfb_present =
        selected_xfb != nullptr &&
        selected_xfb->copy_serial ==
            last_presented_xfb_serial_.load(std::memory_order_relaxed);
    const bool skip_duplicate_present =
        presents &&
        duplicate_xfb_present &&
        !native_settings_overlay_visible();
    const bool skip_rate_present =
        presents &&
        selected_xfb != nullptr &&
        !skip_duplicate_present &&
        !native_settings_overlay_visible() &&
        skip_for_present_rate();
    bool present_swap_chain =
        presents && selected_xfb != nullptr &&
        !skip_duplicate_present && !skip_rate_present;
    const bool present_black = chunk.present_swap_chain && display_xfb_none;
    if (present_black) {
        supersede_interp_sequence();
        interp_log_waiting_ = false;
    } else if (presents) {
        vi_black_shown_ = false;
    }
    if (interp_recording_) {
        note_interp_log_completion();
        // The ready frame's own present: its sub-frames come first, the real
        // frame after them (started by the render loop after this chunk).
        // As on the VI-present path, a present while a completed log waits
        // starts that log's sequence, even one the duplicate or rate checks
        // would drop: the VI may still name the previous XFB (the guest flips
        // at the next retrace), and the waiting frame would be skipped when
        // the next one closes first.
        if (interp_log_waiting_ && chunk.present_swap_chain && selected_xfb != nullptr &&
            renderer_.replay_available()) {
            interp_start_pending_ = true;
            present_swap_chain = false;
        } else if (interp_sequence_.active && present_swap_chain) {
            // Another frame presents now: the playing one is superseded.
            supersede_interp_sequence();
        }
        if (present_swap_chain && !duplicate_xfb_present) {
            note_interp_real_present();
        }
    }
    const auto pre_present_end = frame_timing_enabled
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    const std::uint64_t pre_present_us = frame_timing_enabled
        ? elapsed_us(post_parse_flush_end, pre_present_end)
        : 0u;
    std::uint64_t present_record_us = 0;
    if (present_swap_chain) {
        const auto present_record_start = frame_timing_enabled
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        if (copy_present) {
            renderer_.wait_for_frame_latency_before_present();
            copy_presented_serial_ = selected_xfb->copy_serial;
        }
        const std::string snapshot_path = present_snapshot_path();
        if (!snapshot_path.empty()) {
            renderer_.set_debug_capture_paths({}, snapshot_path);
        }
        renderer_.present(selected_xfb, !snapshot_path.empty());
        if (!snapshot_path.empty()) {
            renderer_.set_debug_capture_paths({}, {});
        }
        if (frame_timing_enabled) {
            present_record_us =
                elapsed_us(present_record_start, std::chrono::steady_clock::now());
        }
    } else if (present_black) {
        present_vi_black(true);
    }
    // A VI naming the frame its copy already presented skipped nothing.
    if (skip_duplicate_present && selected_xfb->copy_serial != copy_presented_serial_) {
        record_skipped_duplicate_xfb_present(*selected_xfb);
    }
    const auto end_frame_start = frame_timing_enabled
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    struct SubmissionContext {
        GxBackend* backend;
        FrameChunk* chunk;
    } submission_context{this, &chunk};
    // Every staging byte the command list copies from must be written.
    {
        const auto join_start = frame_timing_enabled
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        texture_cache_.finish_pending_decodes();
        if (frame_timing_enabled) {
            frame_texture_decode_join_us_ =
                elapsed_us(join_start, std::chrono::steady_clock::now());
        }
    }
    (void)renderer_.end_frame(
        present_swap_chain,
        [](void* opaque, std::uint64_t fence_value) {
            auto& context = *static_cast<SubmissionContext*>(opaque);
            galaxy::timeline::record(
                galaxy::timeline::Event::RenderSubmitted,
                context.chunk->pe_completion_token.value,
                fence_value |
                    (context.chunk->pe_events.empty() ? 0u : (1ull << 63u)));
            context.backend->queue_frame_fifo_effects_fence(
                *context.chunk, fence_value);
            // Deliver only events whose actual signaled fence is complete.
            // One nonblocking poll can retire ready work before Present waits.
            context.backend->poll_completed_frame_effects();
        },
        &submission_context);
    if (presents && present_swap_chain &&
        selected_xfb != nullptr) {
        present_epoch_ = current_present_epoch;
        record_presented_xfb_copy(*selected_xfb);
        last_presented_xfb_addr_.store(
            selected_xfb->guest_addr, std::memory_order_relaxed);
    } else if (present_black) {
        present_epoch_ = current_present_epoch;
    }
    const auto end_frame_end = frame_timing_enabled
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    const std::uint64_t end_frame_us = frame_timing_enabled
        ? elapsed_us(end_frame_start, end_frame_end)
        : 0u;
    const auto gpu_submit_end = frame_timing_enabled
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    // Nonblocking: a later frame writes a finished present snapshot.
    renderer_.debug_log_backbuffer_readback();

    ++frame_index_;
    timing_frame_index_.store(frame_index_, std::memory_order_relaxed);
    const auto render_end = frame_timing_enabled
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    // Per-frame pipeline-compile counts for the [gx-frame-timing] line. CompileStats counters only
    // grow within a session (PipelineCache::shutdown() ends it), so the subtraction cannot underflow.
    {
        const PipelineCache::CompileStats now = pipeline_cache_.compile_stats();
        const PipelineCache::CompileStats& was = frame_compile_stats_baseline_;
        frame_pso_compiles_ = now.pso_compiles - was.pso_compiles;
        frame_pso_library_loads_ = now.pso_library_loads - was.pso_library_loads;
        frame_shader_pair_compiles_ =
            now.shader_pair_compiles - was.shader_pair_compiles;
        frame_blobs_from_cache_ = now.blobs_from_cache - was.blobs_from_cache;
        frame_blob_compile_us_ = now.blob_compile_us - was.blob_compile_us;
    }

    // Capture beside the wall endpoint, before diagnostic formatting/logging.
    const ThreadCounterSample frame_cpu_interval = thread_counter_interval(
        frame_cpu_start, pso_cycles_enabled
            ? thread_cpu_time_100ns() : ThreadCounterSample{});
    const ThreadCounterSample frame_cycles_interval = thread_counter_interval(
        frame_cycles_start, pso_cycles_enabled
            ? thread_cycle_count() : ThreadCounterSample{});



    // [gx-frame-timing]: stall-traced frames over GALAXY_TRACE_GX_STALL_US (selected=1) and frames
    // sampled by GALAXY_GX_FRAME_TIMING_SAMPLE (sample=1). The per-draw *-us fields are measured on
    // those frames only (detail=1); fifo-*-us fields only with the stall trace (the parser profile).
    if (trace_gx_stalls || frame_time_sample_) {
        const std::uint64_t total_us = elapsed_us(render_start, render_end);
        // Wall-clock time since the previous emitted line: total-us covers only this function's span,
        // so it cannot tell a busy render thread from an idle one. On a sampled run it spans the
        // sample interval.
        std::uint64_t frame_interval_us = 0u;
        if (last_frame_timing_emit_valid_) {
            frame_interval_us = elapsed_us(last_frame_timing_emit_, render_start);
        }
        last_frame_timing_emit_ = render_start;
        last_frame_timing_emit_valid_ = true;
        const std::uint64_t parse_us = elapsed_us(parse_start, parse_end);
        const std::uint64_t submit_us = elapsed_us(parse_end, gpu_submit_end);
        const std::uint64_t producer_wait_us = chunk.producer_wait_ns / 1000u;
        if (frame_time_sample_ ||
            total_us >= trace_gx_stall_threshold_us() ||
            producer_wait_us >= trace_gx_stall_threshold_us()) {
            std::ostringstream timing_message;
            timing_message << "[gx-frame-timing] frame=" << frame_index_
                  // A frame can be both selected and sampled; selected=0 rows are the unbiased sample.
                  << " selected="
                  << ((total_us >= trace_gx_stall_threshold_us() ||
                       producer_wait_us >= trace_gx_stall_threshold_us())
                          ? 1
                          : 0)
                  << " sample=" << (frame_time_sample_ ? 1 : 0)
                  << " profile=" << (trace_gx_stalls ? 1 : 0)
                  << " detail=" << (frame_time_detail_ ? 1 : 0)
                  << " fifo-bytes=" << fifo_size
                  << " total-us=" << total_us
                  << " frame-interval-us=" << frame_interval_us
                  << " thread-cpu-us=" << frame_cpu_interval.value / 10u
                  << " thread-cpu-valid=" << (frame_cpu_interval.available ? 1 : 0)
                  << " thread-cycles=" << frame_cycles_interval.value
                  << " thread-cycles-valid=" << (frame_cycles_interval.available ? 1 : 0)
                  << " parse-us=" << parse_us
                  << " submit-us=" << submit_us
                  << " post-parse-flush-us=" << post_parse_flush_us
                  << " pre-present-us=" << pre_present_us
                  << " present-record-us=" << present_record_us
                  << " end-frame-us=" << end_frame_us
                  << " begin-latency-wait-us="
                  << renderer_.last_latency_wait_us()
                  << " begin-slot-wait-us="
                  << renderer_.last_frame_slot_wait_us()
                  << " begin-timestamp-us="
                  << renderer_.last_timestamp_read_us()
                  << " begin-reset-us="
                  << renderer_.last_begin_reset_us()
                  << " producer-wait-us=" << producer_wait_us
                  << " dependency-scan-us="
                  << (chunk.dependency_scan_ns / 1000u)
                  << " dependency-cache-hit="
                  << (chunk.dependency_cache_hit ? 1 : 0)
                  << " snapshot-us=" << (chunk.snapshot_ns / 1000u)
                  << " snapshot-bytes=" << chunk.snapshot_bytes
                  << " snapshot-ranges=" << chunk.snapshot_range_count
                  << " snapshot-range-bytes=" << chunk.snapshot_range_bytes
                  << " snapshot-strict="
                  << (chunk.memory_snapshot_strict ? 1 : 0)
                  << " dirty-ranges=" << chunk.dirty_range_count
                  << " dirty-bytes=" << chunk.dirty_bytes
                  << " dirty-tex-evictions=" << dirty_texture_evictions
                  << " draws=" << frame_draws_issued_
                  << " draw-batches=" << frame_draw_batches_
                  << " no-pso=" << frame_draws_no_pso_
                  << " xfb-copies=" << frame_xfb_copies_
                  << " tex-copies=" << frame_tex_copies_
                  << " efb-alias-updates=" << frame_efb_alias_updates_
                  << " efb-alias-noops=" << frame_efb_alias_noops_
                  << " texture-binding-cache-hits="
                  << frame_texture_binding_cache_hits_
                  << " texture-binding-cache-misses="
                  << frame_texture_binding_cache_misses_
                  << " texture-binding-cache-prunes="
                  << frame_texture_binding_cache_prunes_
                  << " nontri-skips=" << frame_nontri_skips_
                  << " scissor-skips=" << frame_scissor_skips_
                  << " pending-fifo=" << pending_fifo_.size()
                  << " flush-us=" << frame_flush_us_
                  << " flush-pso-us=" << frame_flush_pso_us_
                  << " flush-pso-cycles=" << frame_flush_pso_cycles_
                  << " flush-pso-cycles-valid=" << (frame_flush_pso_cycles_valid_ ? 1 : 0)
                  << " pso-memo-hits=" << frame_pso_memo_hits_
                  << " pso-get-calls=" << frame_pso_get_calls_
                  << " pso-get-us=" << frame_pso_get_us_
                  << " pso-get-max-us=" << frame_pso_get_max_us_
                  // pso-route-early + pso-route-memo + pso-route-get = pso-resolutions. flush-us times the
                  // flush bodies (detail=1 only, early reuse excluded); flush-total-us and flush-mean-us
                  // come from ScopedFlushTiming's sampled whole calls.
                  << " flush-calls=" << frame_flush_calls_
                  << " flush-total-us=" << frame_flush_total_us_
                  << " flush-timed-samples=" << frame_flush_timed_samples_
                  << " flush-mean-us="
                  << (frame_flush_timed_samples_ != 0u
                          ? frame_flush_total_us_ /
                                frame_flush_timed_samples_
                          : 0u)
                  << " pso-resolutions=" << frame_pso_resolutions_
                  << " pso-route-early=" << frame_pso_route_early_
                  // Builds done this frame, against the resolutions requested above.
                  << " pso-compiles=" << frame_pso_compiles_
                  << " pso-library-loads=" << frame_pso_library_loads_
                  << " pso-shader-pairs=" << frame_shader_pair_compiles_
                  << " pso-blob-us=" << frame_blob_compile_us_
                  << " pso-blobs-from-cache=" << frame_blobs_from_cache_
                  << " pso-route-memo=" << frame_pso_memo_hits_
                  << " pso-route-get=" << frame_pso_get_calls_
                  // replay-generic-draws: draws the batched paths refused. The packet and simple counts
                  // are offers; fifo-call-dl-replay-packet-run-count counts accepted packet runs.
                  << " replay-prepared-batches="
                  << frame_replay_prepared_batches_
                  << " replay-packet-offers=" << frame_replay_packet_batches_
                  << " native-shape-draws=" << frame_native_shape_draws_
                  << " native-shape-misses=" << frame_native_shape_misses_
                  << " replay-simple-offers=" << frame_replay_simple_batches_
                  << " replay-generic-draws=" << frame_replay_generic_draws_
                  << " flush-matrix-us=" << frame_flush_matrix_us_
                  << " flush-matrix-bytes=" << frame_matrix_upload_bytes_
                  << " flush-constants-us=" << frame_flush_constants_us_
                  << " flush-texture-us=" << frame_flush_texture_us_
                  << " tex-decode-join-us=" << frame_texture_decode_join_us_
                  << " tex-async-decodes-total=" << texture_cache_.async_decode_jobs()
                  << " vertex-load-us=" << frame_vertex_load_us_
                  << " cached-vertex-validation-us=" << frame_cached_vertex_validation_us_
                  << " cached-vertex-upload-us=" << frame_cached_vertex_upload_us_
                  << " immutable-vertex-reused-bytes=" << renderer_.vertex_ring().reused_bytes()
                  << " immutable-vertex-copied-bytes=" << renderer_.vertex_ring().copied_bytes()
                  << " immutable-vertex-uploads=" << renderer_.vertex_ring().immutable_upload_calls()
                  << " draw-record-us=" << frame_draw_record_us_
                  << " efb-copy-us=" << frame_efb_copy_us_
                  << " resident-vtx-hits=" << frame_resident_vertex_hits_
                  << " curved-split-draws=" << frame_curved_split_draws_
                  << " curved-flat-tris=" << frame_curved_flat_triangles_
                  << " curved-patch-tris=" << frame_curved_patch_triangles_
                  << " clear-quads=" << frame_clear_quads_
                  << " resident-vtx-bytes=" << frame_resident_vertex_bytes_
                  << " resident-vtx-used-bytes="
                  << static_cast<std::uint64_t>(resident_vertices_.used_units()) *
                         sizeof(GxVertexOut)
                  << " decoded-tex-resident-bytes="
                  << texture_cache_.decoded_map_stats().resident_bytes
                  << " decoded-tex-budget-bytes="
                  << texture_cache_.decoded_map_stats().budget_bytes
                  << " decoded-tex-evicted-entries="
                  << texture_cache_.decoded_map_stats().evicted_entries
                  << " decoded-tex-evicted-bytes="
                  << texture_cache_.decoded_map_stats().evicted_bytes
                  << " decoded-tex-entries="
                  << texture_cache_.decoded_map_stats().entries
                  // EFB-copy destinations are allocated at the internal resolution (bytes grow with efb_scale^2).
                  << " efb-copy-dest-bytes="
                  << renderer_.efb_copy_dest_bytes()
                  << " efb-copy-dest-budget-bytes="
                  << renderer_.efb_copy_dest_budget_bytes()
                  << " fifo-commands=" << frame_fifo_profile_.command_count
                  << " fifo-command-us=" << frame_fifo_profile_.command_us
                  << " fifo-nop-count=" << frame_fifo_profile_.nop_count
                  << " fifo-nop-us=" << frame_fifo_profile_.nop_us
                  << " fifo-cp-count=" << frame_fifo_profile_.cp_count
                  << " fifo-cp-us=" << frame_fifo_profile_.cp_us
                  << " fifo-xf-count=" << frame_fifo_profile_.xf_count
                  << " fifo-xf-us=" << frame_fifo_profile_.xf_us
                  << " fifo-metrics-count="
                  << frame_fifo_profile_.metrics_count
                  << " fifo-metrics-us=" << frame_fifo_profile_.metrics_us
                  << " fifo-inval-vtx-count="
                  << frame_fifo_profile_.inval_vtx_count
                  << " fifo-inval-vtx-us="
                  << frame_fifo_profile_.inval_vtx_us
                  << " fifo-bp-count=" << frame_fifo_profile_.bp_count
                  << " fifo-bp-us=" << frame_fifo_profile_.bp_us
                  << " fifo-draw-count=" << frame_fifo_profile_.draw_count
                  << " fifo-draw-us=" << frame_fifo_profile_.draw_us
                  << " fifo-draw-probe-us="
                  << frame_fifo_profile_.draw_payload_probe_us
                  << " fifo-draw-vertices="
                  << frame_fifo_profile_.draw_vertices
                  << " fifo-draw-payload-bytes="
                  << frame_fifo_profile_.draw_payload_bytes
                  << " fifo-hw-noop-count="
                  << frame_fifo_profile_.hw_noop_count
                  << " fifo-hw-noop-us="
                  << frame_fifo_profile_.hw_noop_us
                  << '\n';
            std::cerr << timing_message.str();
        }
    }

    // Retire completed pipeline-cache writes without waiting on disk I/O.
    pipeline_cache_.poll_disk_flush();
    if (pipeline_cache_.has_unsaved_records()) {
        // Preserve learned configurations during play without serializing cache
        // validation, reflection or replacement writes with frame submission.
        static auto last_unsaved_flush = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        if (now - last_unsaved_flush >= std::chrono::seconds(20)) {
            last_unsaved_flush = now;
            pipeline_cache_.flush_disk_async();
        }
    }
    ReportPipelineCoverage();
}

// Pipeline-cache coverage, printed only when the on-disk coverage changes. While
// shader-records-on-disk keeps up with shader-records, the remaining misses are new configurations.
void GxBackend::ReportPipelineCoverage() {
    const PipelineCache::CompileStats stats = pipeline_cache_.compile_stats();
    if (stats.shader_records_on_disk == last_reported_shader_records_ &&
        stats.pso_keys_on_disk == last_reported_pso_records_) {
        return;
    }
    last_reported_shader_records_ = stats.shader_records_on_disk;
    last_reported_pso_records_ = stats.pso_keys_on_disk;
    std::cerr << "[pso-cache-coverage] frame=" << (frame_index_ + 1u)
              << " shader-records=" << stats.shader_records
              << " shader-records-on-disk=" << stats.shader_records_on_disk
              << " pso-keys=" << stats.pso_keys
              << " pso-keys-on-disk=" << stats.pso_keys_on_disk
              << " pso-compiles=" << stats.pso_compiles
              << " pso-library-loads=" << stats.pso_library_loads
              << " shader-pair-compiles=" << stats.shader_pair_compiles
              << " blobs-from-cache=" << stats.blobs_from_cache << '\n';
}

GxBackend::TextureBindingKey GxBackend::capture_texture_binding_key(
    std::uint8_t map_mask) const {
    TextureBindingKey key{};
    key.map_mask = map_mask;
    for (unsigned m = 0; m < kMaxTextureMaps; ++m) {
        if ((map_mask & (1u << m)) == 0u) {
            continue;
        }
        const unsigned bank_offset =
            (m >= 4u) ? bp::kTexHighBankOffset : 0u;
        const unsigned slot = m & 3u;
        const unsigned base = m * 5u;
        key.regs[base + 0u] = state_.bp(static_cast<std::uint8_t>(
            bp::kTexMode0Base + bank_offset + slot));
        key.regs[base + 1u] = state_.bp(static_cast<std::uint8_t>(
            bp::kTexMode1Base + bank_offset + slot));
        key.regs[base + 2u] = state_.bp(static_cast<std::uint8_t>(
            bp::kTexImage0Base + bank_offset + slot));
        key.regs[base + 3u] = state_.bp(static_cast<std::uint8_t>(
            bp::kTexImage3Base + bank_offset + slot));
        const TexFormat format =
            decode_tex_image(key.regs[base + 2u], key.regs[base + 3u]).format;
        if (format == TexFormat::C4 || format == TexFormat::C8 ||
            format == TexFormat::C14X2) {
            key.regs[base + 4u] = state_.bp(static_cast<std::uint8_t>(
                bp::kTexTlutBase + bank_offset + slot));
        }
    }
    return key;
}

GxBackend::TextureHandleKey GxBackend::texture_handle_key(
    const TexImage& image,
    const TexMode& mode,
    const TlutRef& tlut) const {
    unsigned levels = 1u;
    bool generated_mips = false;
    const unsigned mip_mode = static_cast<unsigned>(mode.min_filter) & 0x3u;
    const unsigned full_chain =
        snapshot_full_mip_chain_levels(image.width, image.height);
    if (mip_mode != 0u) {
        const unsigned wanted =
            1u + (static_cast<unsigned>(mode.max_lod_x16) + 15u) / 16u;
        levels = std::min(wanted, full_chain);
    } else if (snapshot_should_generate_native_mips(image, mode)) {
        levels = full_chain;
        generated_mips = levels > 1u;
    }
    const bool palettized = image.format == TexFormat::C4 ||
        image.format == TexFormat::C8 || image.format == TexFormat::C14X2;
    return TextureHandleKey{
        image.guest_addr,
        image.width,
        image.height,
        static_cast<std::uint8_t>(image.format),
        static_cast<std::uint8_t>(palettized ? tlut.format : TlutFormat{}),
        static_cast<std::uint16_t>(palettized ? tlut.tmem_offset : 0u),
        static_cast<std::uint8_t>(std::min<unsigned>(levels, 255u)),
        static_cast<std::uint8_t>(generated_mips ? 1u : 0u),
    };
}

std::array<std::uint32_t, kMaxTextureMaps> GxBackend::capture_sampler_keys(
    std::uint8_t map_mask,
    const std::array<TextureHandle, kMaxTextureMaps>& handles) const {
    std::array<std::uint32_t, kMaxTextureMaps> keys{};
    const auto efb_scale_log2_x32 = static_cast<std::uint8_t>(std::clamp(
        std::lround(32.0 * std::log2(static_cast<double>(std::max(1u, renderer_.efb_scale())))), 0l, 127l));
    for (unsigned map = 0u; map < kMaxTextureMaps; ++map) {
        if ((map_mask & (1u << map)) == 0u) {
            continue;
        }
        const TextureHandle& handle = handles[map];
        keys[map] = pack_sampler_key(
            state_.tex_mode(map), handle.generated_mips, handle.mip_levels,
            handle.lod_shift_x16, handle.lod_arbitrary, efb_scale_log2_x32);
    }
    // Unused slots still get valid zero-key descriptors, but cannot fragment
    // the table cache. map_mask includes both direct and indirect sampling.
    return keys;
}

void GxBackend::clear_frame_texture_binding_table_cache() {
    frame_texture_tables_.clear();
    frame_texture_binding_tables_.clear();
    frame_texture_handle_cache_.clear();
    // Dropping the tables invalidates the remembered binding key with them, so flush_draw_state can
    // trust a valid key without re-deriving it from the BP registers.
    current_texture_binding_key_valid_ = false;
}

void GxBackend::clear_texture_binding_table_cache() {
    clear_frame_texture_binding_table_cache();
    // frame_texture_binding_tables_ own CPU descriptors valid for the current frame only;
    // texture_handle_cache_ is the cache that persists across frames.
    dirty_texture_aliases_.clear();
}

void GxBackend::invalidate_texture_handle_cache_dependency(
    const TextureBindingDependency& dependency) {
    const auto ranges_overlap =
        [](std::uint64_t a_begin,
           std::uint64_t a_size,
           std::uint64_t b_begin,
           std::uint64_t b_size) noexcept {
        return a_size != 0u && b_size != 0u &&
            a_begin < b_begin + b_size &&
            b_begin < a_begin + a_size;
    };
    const std::uint64_t dep_size = dependency.guest_byte_size;
    const auto erase_matching =
        [&dependency, &ranges_overlap, dep_size](auto& cache) {
        for (auto it = cache.begin(); it != cache.end();) {
            const TextureHandleKey& key = it->first;
            const std::uint64_t key_size = it->second.guest_byte_size;
            if (ranges_overlap(
                    dependency.guest_addr,
                    dep_size,
                    key.guest_addr,
                    key_size)) {
                it = cache.erase(it);
            } else {
                ++it;
            }
        }
    };
    erase_matching(texture_handle_cache_);
    erase_matching(frame_texture_handle_cache_);
}

void GxBackend::invalidate_texture_binding_cache_range(
    std::uint32_t guest_addr,
    std::uint32_t size) {
    if (size == 0u) {
        return;
    }

    const auto erase_matching_handles = [&](auto& cache) {
        for (auto it = cache.begin(); it != cache.end();) {
            const TextureHandleKey& key = it->first;
            if (dependency_ranges_overlap(
                    guest_addr,
                    size,
                    key.guest_addr,
                    it->second.guest_byte_size)) {
                it = cache.erase(it);
            } else {
                ++it;
            }
        }
    };
    const auto erase_matching_tables = [&](auto& cache) {
        for (auto it = cache.begin(); it != cache.end();) {
            const TextureBindingTables& tables = it->second;
            bool overlaps = false;
            for (unsigned i = 0; i < tables.dependency_count; ++i) {
                const TextureBindingDependency& dependency =
                    tables.dependencies[i];
                if (dependency_ranges_overlap(
                        guest_addr,
                        size,
                        dependency.guest_addr,
                        dependency.guest_byte_size)) {
                    overlaps = true;
                    break;
                }
            }
            if (overlaps) {
                it = cache.erase(it);
            } else {
                ++it;
            }
        }
    };

    erase_matching_handles(texture_handle_cache_);
    erase_matching_handles(frame_texture_handle_cache_);
    erase_matching_tables(frame_texture_binding_tables_);
}

void GxBackend::mark_texture_binding_alias_dirty(
    const EfbCopyParams& params) {
    const std::uint16_t alias_width = static_cast<std::uint16_t>(
        params.half_scale
            ? std::max<std::uint16_t>(1u, params.src_width / 2u)
            : params.src_width);
    const std::uint16_t alias_height = static_cast<std::uint16_t>(
        params.half_scale
            ? std::max<std::uint16_t>(1u, params.src_height / 2u)
            : params.src_height);
    DirtyTextureAlias dirty{};
    dirty.dependency.guest_addr = params.dest_addr;
    dirty.dependency.width = alias_width;
    dirty.dependency.height = alias_height;
    dirty.dependency.format =
        efb_copy_alias_texture_format(params.target_format);
    dirty.dependency.guest_byte_size = efb_copy_guest_byte_size(params);
    dirty_texture_aliases_.push_back(dirty);
    invalidate_texture_handle_cache_dependency(dirty.dependency);
}

void GxBackend::prune_dirty_texture_binding_tables() {
    if (dirty_texture_aliases_.empty()) {
        return;
    }
    const auto dependencies_overlap =
        [](
            const TextureBindingDependency& a,
            const TextureBindingDependency& b) {
        const std::uint64_t a_size = a.guest_byte_size;
        const std::uint64_t b_size = b.guest_byte_size;
        return a_size != 0u && b_size != 0u &&
            static_cast<std::uint64_t>(a.guest_addr) <
                static_cast<std::uint64_t>(b.guest_addr) + b_size &&
            static_cast<std::uint64_t>(b.guest_addr) <
                static_cast<std::uint64_t>(a.guest_addr) + a_size;
    };
    const auto prune_cache = [this, &dependencies_overlap](auto& cache) {
        for (auto it = cache.begin(); it != cache.end();) {
            bool erase = false;
            const TextureBindingTables& tables = it->second;
            for (std::uint8_t dep_index = 0;
                 dep_index < tables.dependency_count && !erase;
                 ++dep_index) {
                const TextureBindingDependency& dep =
                    tables.dependencies[dep_index];
                for (const DirtyTextureAlias& dirty : dirty_texture_aliases_) {
                    if (dependencies_overlap(dep, dirty.dependency)) {
                        erase = true;
                        break;
                    }
                }
            }
            if (erase) {
                ++frame_texture_binding_cache_prunes_;
                it = cache.erase(it);
            } else {
                ++it;
            }
        }
    };
    prune_cache(frame_texture_binding_tables_);
    dirty_texture_aliases_.clear();
}

bool GxBackend::curved_draw(std::uint8_t vtxfmt) const {
    if (!curved_surfaces_ || !scene_phase_ || side_layer_ != kSceneLayer ||
        state_.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 6u)) != 0u) {
        return false;
    }
    (void)vtxfmt;
    // A native shape record whose model the analysis rounds (curved_surfaces.h; a model kept only for its flat
    // corners draws plainly).
    return record_inputs_ != nullptr && record_inputs_->curved != nullptr && record_inputs_->curved->bends;
}

bool GxBackend::draw_positions_indexed() const noexcept {
    if (record_inputs_ != nullptr) {
        return !record_inputs_->own_positions;
    }
    const VcdType position = state_.vertex_desc(0u).position.vcd;
    return position == VcdType::Index8 || position == VcdType::Index16;
}

namespace {

// GALAXY_GPU_DRAWS=1: each pipeline's keys once, to name the renderer's [gpu-draws] lines.
void describe_pipeline(const gpu::Pipeline* pipeline, const VertexShaderKey& vs, const PixelShaderKey& ps,
                       const RenderStateKey& rs) {
    static const bool enabled = read_env_u64("GALAXY_GPU_DRAWS", 0u) != 0u;
    static std::unordered_set<const gpu::Pipeline*> described;
    if (!enabled || pipeline == nullptr || !described.insert(pipeline).second) {
        return;
    }
    std::fprintf(stderr, "[pso-desc] pso=%p %s %s blend=0x%08x z=0x%x cull=%u topo=0x%02x\n",
                 static_cast<const void*>(pipeline), ShaderGenerator::describe(vs).c_str(),
                 ShaderGenerator::describe(ps).c_str(), rs.blend_bits, static_cast<unsigned>(rs.zmode_bits),
                 static_cast<unsigned>(rs.cull), static_cast<unsigned>(rs.primitive_topology));
}

}  // namespace

bool GxBackend::draw_clear_quad(std::uint8_t vtxfmt, FifoCursor& cursor) {
    // The quad as the game draws it: the compare always, written, no blending or logic op, depth from a Z24X8
    // texture (replace); a 16-bit depth EFB stores another depth.
    const ZMode zm = state_.z_mode();
    const BlendMode bm = state_.blend_mode();
    const std::uint32_t zenv1 = state_.bp(bp::kTevZEnv1);
    const auto pixel_format = static_cast<std::uint8_t>(state_.pe_control().pixel_format);
    if (!zm.test_enable || zm.func != CompareFunc::Always || !zm.update_enable || bm.blend_enable ||
        bm.logic_op_enable || ((zenv1 >> 2u) & 0x3u) != 2u || (zenv1 & 0x3u) != 2u || pixel_format > 1u) {
        return false;
    }
    cursor.require(2);
    if (((static_cast<std::uint32_t>(cursor.data[cursor.offset]) << 8u) |
         static_cast<std::uint32_t>(cursor.data[cursor.offset + 1u])) != 4u) {
        return false;
    }
    // It fills its viewport: the scissor's rectangle, where the viewport covers it.
    const EfbScissor efb_sc = state_.efb_scissor();
    const ScissorRect sc = efb_sc.rect;
    const auto viewport = [&](std::uint16_t i) {
        return std::bit_cast<float>(state_.xf(static_cast<std::uint16_t>(xf::kViewportBase + i)));
    };
    const float left = viewport(3u) - 342.0f - static_cast<float>(efb_sc.offset.x) - viewport(0u);
    const float top = viewport(4u) - 342.0f - static_cast<float>(efb_sc.offset.y) + viewport(1u);
    const float right = left + 2.0f * viewport(0u);
    const float bottom = top - 2.0f * viewport(1u);
    if (sc.x1 < sc.x0 || sc.y1 < sc.y0 || sc.x0 < 0 || sc.y0 < 0 || left > static_cast<float>(sc.x0) ||
        top > static_cast<float>(sc.y0) || right < static_cast<float>(sc.x1 + 1) ||
        bottom < static_cast<float>(sc.y1 + 1)) {
        return false;
    }
    flush_pending_draw_batch();
    // The depth the pixel shader writes (zout.depth, from the texture plus the Z texture's bias), as a clear.
    const std::uint32_t z24 = (clear_quad_z24_ + (state_.bp(bp::kTevZEnv0) & 0xFFFFFFu)) & 0xFFFFFFu;
    renderer_.clear_efb(0u, 0u, z24, false, false, true, static_cast<unsigned>(sc.x0), static_cast<unsigned>(sc.y0),
                        static_cast<unsigned>(sc.x1 - sc.x0 + 1), static_cast<unsigned>(sc.y1 - sc.y0 + 1),
                        pixel_format);
    ++frame_clear_quads_;
    const bool colour = bm.color_update || (bm.alpha_update && pixel_format == 1u);
    if (colour && clear_quad_has_colour_ && !motion_scene_ && !lit_translucent_ && !lit_late_) {
        // A constant colour without fog or dither: the 8-bit value the quad's pixel shader stores, as a clear. Its
        // alpha (an EFB with alpha) is 6-bit: the TEV's (one writes zero) or the destination alpha constant.
        const PixelShaderKey key = build_pixel_shader_key(state_, renderer_.efb_scale());
        if (key.num_tev_stages == 1u && key.fog_type == 0u && (key.output_flags & 0x08u) == 0u) {
            const std::uint32_t c = clear_quad_colour_;
            std::uint32_t alpha = c & 0xFFu;
            if (pixel_format == 1u) {
                alpha = (key.flags & 0x04u) != 0u ? bm.const_alpha : (alpha == 1u ? 0u : alpha);
                alpha = ((alpha >> 2u) * 255u + 31u) / 63u;
            }
            const std::uint32_t ar = (alpha << 8u) | (c >> 24u);
            const std::uint32_t gb = c >> 8u & 0xFFFFu;
            // The whole EFB as a whole-target clear (a fast clear where the GPU has one).
            const bool whole = sc.x0 == 0 && sc.y0 == 0 && static_cast<unsigned>(sc.x1) + 1u >= kEfbWidth &&
                               static_cast<unsigned>(sc.y1) + 1u >= kEfbHeight;
            renderer_.clear_efb(ar, gb, 0u, bm.color_update, bm.alpha_update && pixel_format == 1u, false,
                                whole ? 0u : static_cast<unsigned>(sc.x0), whole ? 0u : static_cast<unsigned>(sc.y0),
                                whole ? 0u : static_cast<unsigned>(sc.x1 - sc.x0 + 1),
                                whole ? 0u : static_cast<unsigned>(sc.y1 - sc.y0 + 1),
                                static_cast<std::uint8_t>(pixel_format | kEfbClearExact));
            const VertexLayoutCacheEntry& layout = cached_vertex_layout(vtxfmt);
            const std::uint16_t count = cursor.read_u16();
            cursor.take(layout.source_size * static_cast<std::size_t>(count));
            return true;
        }
    }
    if (colour) {
        // Its colour: the same quad without the Z texture or the depth compare and write.
        const std::uint32_t zmode = state_.bp(bp::kZMode);
        state_.load_bp((static_cast<std::uint32_t>(bp::kTevZEnv1) << 24u) | (zenv1 & ~0xCu & 0xFFFFFFu));
        state_.load_bp(static_cast<std::uint32_t>(bp::kZMode) << 24u);
        on_draw(PrimitiveClass::Quads, vtxfmt, cursor);
        state_.load_bp((static_cast<std::uint32_t>(bp::kTevZEnv1) << 24u) | (zenv1 & 0xFFFFFFu));
        state_.load_bp((static_cast<std::uint32_t>(bp::kZMode) << 24u) | (zmode & 0xFFFFFFu));
    } else {
        const VertexLayoutCacheEntry& layout = cached_vertex_layout(vtxfmt);
        const std::uint16_t count = cursor.read_u16();
        cursor.take(layout.source_size * static_cast<std::size_t>(count));
    }
    return true;
}

std::uint32_t GxBackend::split_curved_triangles(std::span<const GxVertexOut> vertices,
                                                std::span<const std::uint16_t> triangles,
                                                std::vector<std::uint16_t>& split) {
    // A triangle is flat when no corner has a displacement or crease normal: every edge then keeps one segment and
    // moves nothing, in this triangle and in its neighbours.
    const auto flat = [&](std::size_t t) {
        for (std::size_t k = 0; k < 3u; ++k) {
            const GxVertexOut& v = vertices[triangles[t + k]];
            if ((v.curve[0] | v.curve[1]) != 0u) {
                return false;
            }
        }
        return true;
    };
    split.clear();
    const std::size_t count = triangles.size() / 3u * 3u;
    if (count == 0u || !std::all_of(triangles.begin(), triangles.begin() + static_cast<std::ptrdiff_t>(count),
                                    [&](std::uint16_t i) { return i < vertices.size(); })) {
        return 0u;
    }
    std::size_t flat_indices = 0;
    for (std::size_t t = 0; t < count; t += 3u) {
        flat_indices += flat(t) ? 3u : 0u;
    }
    split.resize(count);
    std::size_t f = 0;
    std::size_t c = flat_indices;
    for (std::size_t t = 0; t < count; t += 3u) {
        std::size_t& out = flat(t) ? f : c;
        split[out] = triangles[t];
        split[out + 1u] = triangles[t + 1u];
        split[out + 2u] = triangles[t + 2u];
        out += 3u;
    }
    return static_cast<std::uint32_t>(flat_indices);
}

gpu::Pipeline* GxBackend::resolve_flat_pipeline(const PsoKey& curved_key) {
    // A triangle a factor-one patch outputs unchanged draws the same pixels in a triangle list, but in another place
    // in the draw's order: only where that order cannot show. Opaque: no blending or logic op, a depth test that
    // passes by nearness, written. Or a sum: both factors one, added or subtracted (the shadow volumes' alpha), into a
    // fixed-point EFB (its clamped sums do not depend on their order; float sums round by it), with depth not written
    // and no other target.
    const RenderStateKey& rs = curved_key.render_state;
    const unsigned depth_func = (rs.zmode_bits >> 1u) & 0x7u;
    const bool ordered_by_depth = (rs.zmode_bits & 0x11u) == 0x11u &&
        (depth_func == 1u || depth_func == 3u || depth_func == 4u || depth_func == 6u);
    const bool opaque = (rs.blend_bits & 0x101u) == 0u && ordered_by_depth;
    constexpr std::uint32_t kOtherTargets =
        kRenderStateMotionTarget | kRenderStateReactiveTarget | kRenderStateLayerTarget;
    const bool sum = (rs.blend_bits & 0x1FDu) == 0x25u && (rs.blend_bits & kOtherTargets) == 0u &&
                     (rs.samples_log2 & kEfbVariantHdr) == 0u && (rs.zmode_bits & 0x10u) == 0u;
    if (!opaque && !sum) {
        return nullptr;
    }
    VertexShaderKey vs_key = vs_key_;
    vs_key.flags = static_cast<std::uint8_t>(vs_key.flags & ~kVsKeyCurved);
    const PsoKey key{vs_key.hash(), curved_key.ps_hash, flat_render_state(rs)};
    const std::uint64_t hash = key.hash();
    PipelineMemoEntry& memo = pipeline_memo_[hash & kPipelineMemoMask];
    if (memo.hash == hash && memo.pipeline != nullptr && memo.key == key) {
        return memo.pipeline;
    }
    gpu::Pipeline* const pipeline = pipeline_cache_.get_if_ready(key, ps_key_, vs_key);
    if (pipeline == nullptr) {
        return nullptr;
    }
    describe_pipeline(pipeline, vs_key, ps_key_, key.render_state);
    memo.hash = hash;
    memo.key = key;
    memo.pipeline = pipeline;
    return pipeline;
}

gpu::Pipeline* GxBackend::flush_draw_state(
    gpu::TopologyType primitive_topology_type, std::uint8_t vtxfmt) {
    const bool time_detail = frame_time_detail_;
    // Always counted: the denominator of every *_us field.
    ++frame_flush_calls_;
    if (screen_run_ && !frame_scene_seen_ && !screen_frame_open_ && interp_recording_ && !history_scene_) [[unlikely]] {
        begin_screen_frame();
    }
    const auto topology_key =
        static_cast<std::uint8_t>(primitive_topology_type);
    const bool curved = primitive_topology_type == gpu::kTopologyTypeTriangle && curved_draw(vtxfmt);
    // Consume once, propagating upload invalidation before considering reuse.
    const std::uint32_t dirty = state_.consume_dirty();
    // Matrix words do not affect the shader/render keys, but they invalidate both upload snapshots.
    // Propagate this before considering reuse: a prior draw may have cleared the flags before
    // LOAD_XF/LOAD_INDX changed the bank.
    const bool mtx_dirty = (dirty & GxState::kDirtyXfMatrices) != 0;
    if (mtx_dirty) {
        matrix_palette_stale_ = true;
        inline_matrices_stale_ = true;
    }
    constexpr std::uint32_t kDirtySatisfiedByCachedUploads =
        GxState::kDirtyXfMatrices;
    const bool dirty_needs_body =
        (dirty & ~kDirtySatisfiedByCachedUploads) != 0u;
    // The draw's vertex inputs: a native shape record's, else the GX vertex description's (VCD bits 0-8). Another
    // source (a record of another model, or GX after a record) rebuilds the keys that depend on it.
    const std::uint32_t matrix_bits =
        record_inputs_ != nullptr ? record_inputs_->matrix_bits : (state_.cp(cp::kVcdLo) & 0x1FFu);
    const std::uint64_t vertex_source =
        record_inputs_ != nullptr
            ? ((record_inputs_->positions * 0x9E3779B97F4A7C15ull) ^ record_inputs_->colors ^
               (static_cast<std::uint64_t>(matrix_bits) << 1u) ^ 1u)
            : static_cast<std::uint64_t>(matrix_bits) << 1u;
    const bool vertex_source_changed = vertex_source != vs_key_vertex_source_;
    vs_key_vertex_source_ = vertex_source;
    if (!dirty_needs_body &&
        !vertex_source_changed &&
        !texture_bindings_dirty_ &&
        !frame_bindings_dirty_ &&
        (!matrix_palette_stale_ || !current_palette_required_) &&
        !inline_matrices_stale_ &&
        rt_light_pending_ == 0u &&
        current_pipeline_ != nullptr &&
        !current_pipeline_is_uber_ &&
        current_vs_constants_ != 0 &&
        current_ps_constants_ != 0 &&
        (current_texture_map_mask_ == 0u ||
         current_texture_table_.ptr != 0) &&
        curved == current_curved_ && (curved || render_state_key_.primitive_topology == topology_key)) {
        ++frame_pso_route_early_;
        return current_pipeline_;
    }

    // Body timing, on detail frames only, excludes the reuse decision above; ScopedFlushTiming's
    // sampled whole-call timing includes it.
    const auto flush_start = time_detail
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    // Rebuild canonical keys only for their dirty inputs; hash only changed
    // keys. Uniform updates still upload their constants below.
    const bool tev_dirty  = (dirty & GxState::kDirtyTev)         != 0;
    const bool rs_dirty   = (dirty & GxState::kDirtyRenderState)  != 0;
    const bool xf_dirty   = (dirty & GxState::kDirtyXfShader)    != 0;
    const bool vcd_dirty  = (dirty & GxState::kDirtyVcd)         != 0;
    const bool tev_c_dirty = (dirty & GxState::kDirtyTevConstants) != 0;
    const bool vs_c_dirty = (dirty & GxState::kDirtyVsConstants) != 0;

    // sampled_texture_map_mask walks every TEV stage; only kDirtyTev can move the mask (per-object
    // matrix loads set kDirtyXfMatrices alone), so it is recomputed only then or after a reset.
    if (!current_texture_map_mask_valid_ || tev_dirty) {
        current_texture_map_mask_ = sampled_texture_map_mask(state_);
        current_texture_map_mask_valid_ = true;
    }
    const std::uint8_t texture_map_mask = current_texture_map_mask_;
    // TextureBindingKey depends only on the map mask and the sampled maps' TexMode0/1, TexImage0/3
    // and TexTlut words, all BP registers that set kDirtyTex, so the last key is reused until one of
    // them changes (dropping the table cache also invalidates it).
    const bool texture_binding_key_fresh =
        current_texture_binding_key_valid_ &&
        dirty != ~0u &&
        (dirty & GxState::kDirtyTex) == 0u &&
        !texture_bindings_dirty_ &&
        texture_map_mask == current_texture_binding_key_mask_;
    TextureBindingKey texture_binding_key = texture_binding_key_fresh
        ? current_texture_binding_key_
        : capture_texture_binding_key(texture_map_mask);
    // Lit translucents: blended (not additive) scene draws of the translucent phases while the ray
    // tracer's lighting texture is ready, with map 7 free for it.
    {
        const BlendMode b = state_.blend_mode();
        using motion::ScenePhase;
        const bool blended =
            b.blend_enable && !b.logic_op_enable && !b.subtract && b.color_update && b.dst_factor != BlendFactor::One &&
            b.src_factor != BlendFactor::SrcColor && b.src_factor != BlendFactor::InvSrcColor &&
            b.dst_factor != BlendFactor::SrcColor && b.dst_factor != BlendFactor::InvSrcColor &&
            (rt_phase_ == static_cast<std::uint8_t>(ScenePhase::Xlu) ||
             rt_phase_ == static_cast<std::uint8_t>(ScenePhase::ListXlu));
        // GALAXY_RT_LIT_LATE=1: the opaque draws after the screen capture take the light texture too. Off by
        // default: those draws turn see-through and dark with it.
        static const bool lit_late = read_env_u64("GALAXY_RT_LIT_LATE", 0u) != 0u;
        const bool late = lit_late && !b.blend_enable && !b.logic_op_enable && b.color_update &&
                          rt_phase_ == static_cast<std::uint8_t>(ScenePhase::AfterIndirect);
        lit_translucent_ = scene_phase_ && side_layer_ == kSceneLayer && (texture_map_mask & 0x80u) == 0u &&
                           (blended || late) && renderer_.rt_light_texture_ready() &&
                           renderer_.rt_light_texture() != nullptr;
        lit_late_ = lit_translucent_ && !blended;
        if ((texture_map_mask & 0x80u) == 0u) {
            texture_binding_key.regs[7u * 5u] = lit_translucent_ ? kLitTranslucentKey : 0u;
        }
    }
    // A dirty BP input requires a fresh capture, but writes to unused maps,
    // image1/2 or restored register values can leave the effective key equal.
    // Explicit frame/resource invalidation still forces work through tex_dirty.
    const bool texture_binding_changed =
        !current_texture_binding_key_valid_ ||
        (!texture_binding_key_fresh &&
         !(texture_binding_key == current_texture_binding_key_)) ||
        texture_binding_key.regs[7u * 5u] != current_texture_binding_key_.regs[7u * 5u];
    const bool tex_dirty = frame_bindings_dirty_ ||
        texture_bindings_dirty_ || texture_binding_changed;

    if (tev_dirty || !shader_key_hashes_valid_) {
        const auto next = build_pixel_shader_key(state_, renderer_.efb_scale());
        if (!shader_key_hashes_valid_ || !(next == ps_key_)) {
            ps_key_ = next;
            current_ps_hash_ = ps_key_.hash();
        }
    }
    if (xf_dirty || vcd_dirty || vertex_source_changed || !shader_key_hashes_valid_) {
        const auto next = build_vertex_shader_key(state_, matrix_bits);
        if (!shader_key_hashes_valid_ || !(next == vs_key_)) {
            vs_key_ = next;
            current_vs_hash_ = vs_key_.hash();
        }
    }
    // Glows: additive scene draws shine at the ray tracer's glow exposure (output_flags bit 5).
    bool glow_changed = false;
    {
        const BlendMode b = state_.blend_mode();
        const bool glow = scene_phase_ && side_layer_ == kSceneLayer && b.blend_enable && !b.logic_op_enable &&
                          !b.subtract && b.color_update && b.dst_factor == BlendFactor::One &&
                          (renderer_.rt_physical() || renderer_.rt_glow_exposure() > 1.0f);
        const auto flags = static_cast<std::uint8_t>(glow ? (ps_key_.output_flags | 0x20u)
                                                          : (ps_key_.output_flags & ~0x20u));
        if (flags != ps_key_.output_flags) {
            ps_key_.output_flags = flags;
            current_ps_hash_ = ps_key_.hash();
            glow_changed = true;
        }
        // Lit translucents (output_flags bit 6): the lighting texture in map 7.
        const auto lit_flags = static_cast<std::uint8_t>(
            (ps_key_.output_flags & ~0xC0u) | (lit_translucent_ ? 0x40u : 0u) | (lit_late_ ? 0x80u : 0u));
        if (lit_flags != ps_key_.output_flags) {
            ps_key_.output_flags = lit_flags;
            current_ps_hash_ = ps_key_.hash();
            glow_changed = true;
        }
        // Indexed positions: a model (a star's fiery surface); direct ones: sprites and sparks.
        const bool position_indexed = draw_positions_indexed();
        const std::uint8_t kind =
            !glow ? 0u : position_indexed ? 2u : 1u;
        if (kind != glow_kind_) {
            glow_kind_ = kind;
            glow_changed = true;
        }
    }
    // Per-pixel and enhanced lighting need the extra position/normal
    // interpolants, which the line and point geometry shaders do not carry.
    const bool triangles = topology_key == static_cast<std::uint8_t>(gpu::kTopologyTypeTriangle);
    // Physical ray tracing: the vertex shaders take a channel's baked vertex light to its program's neutral level (flags
    // bits 2-3, levels in bits 8-23).
    // Bit 6: by the model's facing fit (debake_sh, debake_peak).
    debake_sample_key_ = 0u;
    debake_fit_active_ = false;
    {
        const std::uint32_t state = triangles ? debake_state() : 0u;
        const bool fit = debake_fit_active_ && (state & 3u) != 0u;
        if (fit && cpu_vs_constants_.sky_up_known != 0u) {
            // The level it is de-baked to: its baked light at the player's up, the exposure's reference (a white surface
            // facing the player's up shows white), the up taken into the model's space by its position matrix; at most
            // its most lit facing's, at least a quarter of it.
            float up[3];
            decode_octahedral(cpu_vs_constants_.sky_up, up);
            const unsigned row = std::min(state_.cp(cp::kMatrixIndexA) & 0x3Fu, 61u);
            float model_up[3]{};
            for (unsigned c = 0; c < 3u; ++c) {
                for (unsigned r = 0; r < 3u; ++r) {
                    model_up[c] += std::bit_cast<float>(state_.xf(static_cast<std::uint16_t>((row + r) * 4u + c))) * up[r];
                }
            }
            const float length = std::sqrt(model_up[0] * model_up[0] + model_up[1] * model_up[1] + model_up[2] * model_up[2]);
            if (std::isfinite(length) && length > 1e-6f) {
                for (float& v : model_up) {
                    v /= length;
                }
                float y[9];
                debake_sh9(model_up, y);
                for (unsigned c = 0; c < 3u; ++c) {
                    float light = 0.0f;
                    for (unsigned i = 0; i < 9u; ++i) {
                        light += debake_fit_sh_[(c * 9u + i) / 4u][(c * 9u + i) % 4u] * y[i];
                    }
                    debake_fit_peak_[c] = std::clamp(light, 0.25f * debake_fit_peak_[c], debake_fit_peak_[c]);
                }
            }
        }
        // Bit 7: lit by the game's player light rig (rt_player_rig_draw): its level, not its hue.
        const bool rig = triangles && (enhanced_ps_flags_ & kEnhancedRtPhysical) != 0u && rt_player_rig_draw();
        const std::uint32_t debake = ((state & 3u) << 2u) | (((state >> 2u) & 3u) << 4u) | (state & 0xFFFF00u) |
                                     (fit ? 64u : 0u) | (rig ? 128u : 0u);
        bool changed = cpu_vs_constants_.debake != debake;
        if (fit && (std::memcmp(cpu_vs_constants_.debake_sh, debake_fit_sh_, sizeof(debake_fit_sh_)) != 0 ||
                    std::memcmp(cpu_vs_constants_.debake_peak, debake_fit_peak_, sizeof(debake_fit_peak_)) != 0)) {
            std::memcpy(cpu_vs_constants_.debake_sh, debake_fit_sh_, sizeof(debake_fit_sh_));
            std::memcpy(cpu_vs_constants_.debake_peak, debake_fit_peak_, sizeof(debake_fit_peak_));
            changed = true;
        }
        if (changed) {
            cpu_vs_constants_.debake = debake;
            sky_up_dirty_ = true;   // the scene constants' upload
        }
    }
    // The vertex shaders carry interpolation's vertex blend only in frames recorded for interpolated redraws, and the
    // de-baking only under physical ray tracing (kVsFeature*).
    const std::uint32_t vs_features = (interp_recording_ ? kVsFeatureVertexBlend : 0u) |
                                      ((enhanced_ps_flags_ & kEnhancedRtPhysical) != 0u ? kVsFeatureDebake : 0u);
    const bool vs_features_changed = vs_key_.features != vs_features;
    if (vs_features_changed) {
        vs_key_.features = vs_features;
        current_vs_hash_ = vs_key_.hash();
    }
    bool enhanced = false;
    bool enhanced_changed = false;
    {
        // Perspective scene draws only: the view vector is the eye-space position. The material
        // follows the keys, which change only with their dirty bits.
        // GALAXY_RT_DELIGHT=1: the opaque draws are de-lit under traced lighting. Off by default: de-lighting
        // does not change the traced light, and its enhanced path clamps the TEV where the game wraps.
        static const bool delight = read_env_u64("GALAXY_RT_DELIGHT", 0u) != 0u;
        // The traced lighting de-lights the opaque phases only (it relights what it traces); translucent
        // draws (water, glass, seaweed) keep the game's lit colour.
        using motion::ScenePhase;
        const bool opaque_phase = rt_phase_ == static_cast<std::uint8_t>(ScenePhase::OpaBeforeVolumeShadow) ||
                                  rt_phase_ == static_cast<std::uint8_t>(ScenePhase::OpaBeforeSilhouette) ||
                                  rt_phase_ == static_cast<std::uint8_t>(ScenePhase::Opa) ||
                                  rt_phase_ == static_cast<std::uint8_t>(ScenePhase::ListOpa);
        // Ray tracing in the game's light units (kEnhancedRtPhysical) needs every lit opaque draw at its full light
        // (the albedo the traced light multiplies), whatever the lighting setting.
        const bool physical = opaque_phase && (enhanced_ps_flags_ & kEnhancedRtPhysical) != 0u;
        const bool gate = (enhanced_lighting_ || physical ||
                           (delight && opaque_phase && (enhanced_ps_flags_ & kEnhancedRtLighting) != 0u)) &&
                          triangles &&
                          scene_phase_ && side_layer_ == kSceneLayer &&
                          state_.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 6u)) == 0u;
        if (gate != enhanced_gate_ || tev_dirty || xf_dirty || vcd_dirty || !shader_key_hashes_valid_) {
            enhanced_gate_ = gate;
            enhanced_material_ = gate ? enhanced_lighting_material(vs_key_, ps_key_) : 0u;
        }
        const std::uint32_t material = enhanced_material_;
        enhanced = material != 0u;
        if (((vs_key_.flags & kVsKeyEnhancedLighting) != 0u) != enhanced) {
            vs_key_.flags ^= kVsKeyEnhancedLighting;
            current_vs_hash_ = vs_key_.hash();
        }
        enhanced_changed = ((ps_key_.reserved & kPsKeyEnhancedLighting) != 0u) != enhanced;
        if (enhanced_changed || ps_key_.ind_reserved != material) {
            ps_key_.reserved = static_cast<std::uint8_t>(enhanced ? ps_key_.reserved | kPsKeyEnhancedLighting
                                                                  : ps_key_.reserved & ~kPsKeyEnhancedLighting);
            ps_key_.ind_reserved = material;
            current_ps_hash_ = ps_key_.hash();
        }
        // The scene draws of a ray-traced scene keep the branches only ray tracing takes (kPsKeyRtPaths).
        constexpr std::uint32_t kRtBits = kEnhancedRtReflections | kEnhancedRtLighting | kEnhancedRtPhysical;
        const bool rt_scene = scene_phase_ && (enhanced_ps_flags_ & kRtBits) != 0u;
        if (((ps_key_.reserved & kPsKeyRtPaths) != 0u) != rt_scene) {
            ps_key_.reserved ^= kPsKeyRtPaths;
            current_ps_hash_ = ps_key_.hash();
        }
    }
    {
        const bool ppl = per_pixel_lighting_ && !enhanced && (vs_key_.flags & 0x02u) != 0u && triangles;
        if (((vs_key_.flags & kVsKeyPerPixelLighting) != 0u) != ppl) {
            vs_key_.flags ^= kVsKeyPerPixelLighting;
            current_vs_hash_ = vs_key_.hash();
        }
        if (((ps_key_.reserved & kPsKeyPerPixelLighting) != 0u) != ppl) {
            ps_key_.reserved ^= kPsKeyPerPixelLighting;
            current_ps_hash_ = ps_key_.hash();
        }
        const bool ppl_finite = ppl && (vs_key_.flags & kVsKeyFiniteLightAttn) != 0u;
        if (((ps_key_.reserved & kPsKeyPplFiniteLightAttn) != 0u) != ppl_finite) {
            ps_key_.reserved ^= kPsKeyPplFiniteLightAttn;
            current_ps_hash_ = ps_key_.hash();
        }
    }
    shader_key_hashes_valid_ = true;
    if (rs_dirty) {
        render_state_key_ = build_render_state_key(state_);
    }
    // The EFB's variant (its samples per pixel, MSAA, the output EFB has one; the HDR scene EFB) decides the
    // pipeline's too.
    const std::uint8_t samples_log2 = renderer_.efb_variant();
    // A curved draw after another keeps its patch topology (its variant is settled below).
    const bool keep_patch =
        curved && (render_state_key_.primitive_topology & kTopologyTypeMask) == kTopologyPatch;
    const bool topology_dirty =
        (!keep_patch && render_state_key_.primitive_topology != topology_key) ||
        render_state_key_.samples_log2 != samples_log2;
    if (!keep_patch) {
        render_state_key_.primitive_topology = topology_key;
    }
    render_state_key_.samples_log2 = samples_log2;
    if (renderer_.rt_active()) {
        // Traced geometry: unblended triangles (no logic op) that write colour and depth under a
        // perspective projection. Shadow volumes, z-only passes and screen quads stay out, and so
        // do alpha-tested cut-outs (foliage, fences) and z-textured surfaces, whose depth is not
        // their triangles'. The material table's foliage cards are the exception (rt_card_draw).
        const std::uint32_t bb = render_state_key_.blend_bits;
        const bool traceable =
            topology_key == static_cast<std::uint8_t>(gpu::kTopologyTypeTriangle) &&
            (bb & ((1u << 0u) | (1u << 8u))) == 0u && (bb & (1u << 25u)) != 0u &&
            (render_state_key_.zmode_bits & (1u << 4u)) != 0u &&
            render_state_key_.cull != static_cast<std::uint8_t>(CullMode::All) &&
            state_.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 6u)) == 0u &&
            (ps_key_.flags & 0x02u) == 0u;
        rt_state_eligible_ = traceable && alpha_test_keeps_all(ps_key_);
        rt_card_eligible_ = traceable && !rt_state_eligible_;
        const BlendMode blend = state_.blend_mode();
        rt_glow_eligible_ =
            topology_key == static_cast<std::uint8_t>(gpu::kTopologyTypeTriangle) && blend.blend_enable &&
            !blend.logic_op_enable && !blend.subtract && blend.color_update && blend.dst_factor == BlendFactor::One &&
            render_state_key_.cull != static_cast<std::uint8_t>(CullMode::All) &&
            state_.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 6u)) == 0u;
        // Water and clear materials are models (indexed positions); particles (steam, smoke, haze with
        // indirect warps) are direct-vertex sprites and never water.
        const bool position_indexed = draw_positions_indexed();
        rt_sprite_eligible_ =
            topology_key == static_cast<std::uint8_t>(gpu::kTopologyTypeTriangle) && blend.blend_enable &&
            !blend.logic_op_enable && !blend.subtract && blend.color_update &&
            blend.src_factor == BlendFactor::SrcAlpha && blend.dst_factor == BlendFactor::InvSrcAlpha &&
            render_state_key_.cull != static_cast<std::uint8_t>(CullMode::All) &&
            state_.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 6u)) == 0u;
        rt_water_eligible_ =
            topology_key == static_cast<std::uint8_t>(gpu::kTopologyTypeTriangle) && blend.blend_enable &&
            !blend.logic_op_enable && !blend.subtract && blend.color_update &&
            blend.src_factor == BlendFactor::SrcAlpha && blend.dst_factor == BlendFactor::InvSrcAlpha &&
            render_state_key_.cull != static_cast<std::uint8_t>(CullMode::All) &&
            state_.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 6u)) == 0u &&
            position_indexed;
    }
    // Motion writers: opaque triangles with color writes in a temporal scene
    // (blending, logic ops and dual-source alpha cannot share a second
    // target). Keys only change while a motion scene is (or was) active.
    // Blended triangles (no dual-source alpha) write the reactive mask.
    // The game's scene runs on an RGBA6 EFB (MainLoopFramework::preGX), where every blend that reads the source alpha
    // is dual-source (needs_dual_src_alpha): it blends at the TEV's 8-bit alpha, not the EFB's 6-bit one, and under
    // constant destination alpha (GXSetDstAlpha: the translucent lists, Mario, shadows) writes the constant instead.
    // Dual-source blending binds no second target, so none of those effects wrote the reactive mask (DLSS's, and the
    // water's and clear surfaces' cover: a fall's spray was painted over). In a temporal scene such a draw blends
    // single-source instead, at the same 8-bit alpha (ShaderGenerator's reactive variant), and writes the mask; a
    // constant destination alpha of 0 (every one the game sets) its alpha channel's own blend writes
    // (kRenderStateZeroAlpha). Another constant stays dual-source.
    bool motion_key_changed = false;
    if (motion_scene_ || motion_bits_set_) [[unlikely]] {
        const BlendMode blend_mode = state_.blend_mode();
        const auto src_alpha_factor = [](BlendFactor f) {
            return static_cast<std::uint32_t>(f) == 4u || static_cast<std::uint32_t>(f) == 5u;
        };
        const bool constant_alpha = blend_mode.const_alpha_enable && blend_mode.alpha_update;
        const bool single_source =
            blend_mode.blend_enable && !blend_mode.subtract &&
            (src_alpha_factor(blend_mode.src_factor) || src_alpha_factor(blend_mode.dst_factor)) &&
            state_.pe_control().pixel_format == 1u && (!constant_alpha || blend_mode.const_alpha == 0u);
        const std::uint32_t dual_bit = motion_scene_ && single_source ? 0u : (1u << 27);
        std::uint32_t bb = render_state_key_.blend_bits;
        const bool candidate = motion_scene_ &&
            topology_key == static_cast<std::uint8_t>(gpu::kTopologyTypeTriangle) &&
            (bb & ((1u << 8) | dual_bit)) == 0u && (bb & (1u << 25)) != 0u;
        const bool writer = candidate && (bb & 1u) == 0u;
        const bool reactive = candidate && (bb & 1u) != 0u;
        // Dual-source and constant alpha as the key builders made them, but for a reactive draw drawn single-source.
        if (single_source) {
            const bool dual = !reactive;
            const auto set_bit = [&motion_key_changed](auto& word, auto mask, bool on) {
                if (((word & mask) != 0u) != on) {
                    word ^= mask;
                    motion_key_changed = true;
                }
            };
            set_bit(bb, 1u << 27, dual);
            set_bit(bb, 1u << 28, constant_alpha && dual);
            set_bit(bb, kRenderStateZeroAlpha, constant_alpha && !dual);
            render_state_key_.blend_bits = bb;
            const std::uint8_t flags = ps_key_.flags;
            set_bit(ps_key_.flags, std::uint8_t{0x80u}, dual);
            set_bit(ps_key_.flags, std::uint8_t{0x04u}, constant_alpha && dual);
            if (ps_key_.flags != flags) {
                current_ps_hash_ = ps_key_.hash();
            }
        }
        if (((vs_key_.flags & kVsKeyMotion) != 0u) != writer) {
            vs_key_.flags ^= kVsKeyMotion;
            current_vs_hash_ = vs_key_.hash();
            motion_key_changed = true;
        }
        // The effects layer (RendererD3D12::begin_layer_window): a reactive draw (single-source) whose factors never
        // read the EFB (its colour or, on an EFB with alpha, its alpha) blends the same into the layer's targets.
        const auto reads_efb = [has_alpha = state_.pe_control().pixel_format == 1u](BlendFactor f, bool source) {
            const auto v = static_cast<std::uint32_t>(f);
            return (source && (v == 2u || v == 3u)) || (has_alpha && (v == 6u || v == 7u));
        };
        const bool layer = reactive && renderer_.layer_window() &&
                           (blend_mode.subtract ||
                            (!reads_efb(blend_mode.src_factor, true) && !reads_efb(blend_mode.dst_factor, false)));
        const std::uint8_t ps_bits =
            (writer ? kPsKeyMotion : 0u) | (reactive ? kPsKeyReactive : 0u) | (layer ? kPsKeyLayer : 0u);
        if ((ps_key_.reserved & (kPsKeyMotion | kPsKeyReactive | kPsKeyLayer)) != ps_bits) {
            ps_key_.reserved = static_cast<std::uint8_t>(
                (ps_key_.reserved & ~(kPsKeyMotion | kPsKeyReactive | kPsKeyLayer)) | ps_bits);
            current_ps_hash_ = ps_key_.hash();
            motion_key_changed = true;
        }
        const std::uint32_t rs_bits = (writer ? kRenderStateMotionTarget : 0u) |
                                      (reactive ? kRenderStateReactiveTarget : 0u) |
                                      (layer ? kRenderStateLayerTarget : 0u);
        constexpr std::uint32_t kTargetBits =
            kRenderStateMotionTarget | kRenderStateReactiveTarget | kRenderStateLayerTarget;
        if ((bb & kTargetBits) != rs_bits) {
            render_state_key_.blend_bits = (bb & ~kTargetBits) | rs_bits;
            motion_key_changed = true;
        }
        motion_bits_set_ = writer || reactive;
    }

    // Curved surfaces: the vertex stage's curve outputs and the patches of its output variant's hull and domain shaders.
    bool curved_changed = curved != current_curved_;
    current_curved_ = curved;
    if (((vs_key_.flags & kVsKeyCurved) != 0u) != curved) {
        vs_key_.flags ^= kVsKeyCurved;
        current_vs_hash_ = vs_key_.hash();
        curved_changed = true;
    }
    if (curved) {
        const auto patch = static_cast<std::uint8_t>(
            kTopologyPatch |
            ((vs_key_.flags & (kVsKeyPerPixelLighting | kVsKeyEnhancedLighting)) != 0u ? kCurvedPerPixel : 0u) |
            ((vs_key_.flags & kVsKeyMotion) != 0u ? kCurvedMotion : 0u));
        curved_changed = curved_changed || render_state_key_.primitive_topology != patch;
        render_state_key_.primitive_topology = patch;
    }
    if (current_pipeline_ == nullptr || current_pipeline_is_uber_ ||
        tev_dirty || xf_dirty || vcd_dirty || rs_dirty || topology_dirty ||
        motion_key_changed || curved_changed || vs_features_changed) {
        const PsoKey pso_key{
            current_vs_hash_,
            current_ps_hash_,
            render_state_key_,
        };
        const std::uint64_t pso_key_hash = pso_key.hash();
        const auto pso_start = time_detail
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        // Thread cycles of this PSO resolution (GALAXY_GX_PSO_CYCLES), instrumentation included.
        const ThreadCounterSample pso_cycles_start = pso_cycles_enabled
            ? thread_cycle_count() : ThreadCounterSample{};
        // An unchanged key resolves to current_pipeline_: the shader hashes, render state and topology
        // do not depend on matrices, and the palette uploads by its own stale flag. The hash is
        // compared first (a different hash proves a different key); an equal hash is confirmed
        // against the whole key.
        const bool pso_key_changed =
            current_pipeline_ == nullptr ||
            current_pipeline_is_uber_ ||
            !current_pso_key_valid_ ||
            current_pso_key_hash_ != pso_key_hash ||
            !(current_pso_key_ == pso_key);
        if (pso_key_changed) {
            // Always counted, so the three routes sum to it.
            ++frame_pso_resolutions_;
            // The frame-scoped memo first: the published map cannot change between begin_frame()'s
            // drain_completions() and the frame's last draw, so a hit returns what get() would.
            gpu::Pipeline* resolved = nullptr;
            // One indexed probe. The hash picks the victim; the exact key
            // confirms the hit, so a collision is a miss, never a wrong
            // pipeline.
            PipelineMemoEntry& memo_slot =
                pipeline_memo_[pso_key_hash & kPipelineMemoMask];
            const bool memo_hit =
                memo_slot.hash == pso_key_hash &&
                memo_slot.pipeline != nullptr &&
                memo_slot.key == pso_key;
            bool resolved_uber = false;
            if (memo_hit) {
                resolved = memo_slot.pipeline;
                // Always counted, like the get() calls.
                ++frame_pso_memo_hits_;
            } else {
                ++frame_pso_get_calls_;
                const auto get_start = time_detail
                    ? std::chrono::steady_clock::now()
                    : std::chrono::steady_clock::time_point{};
                resolved = pipeline_cache_.get(pso_key, ps_key_, vs_key_);
                resolved_uber = pipeline_cache_.last_get_used_uber();
                if (time_detail) {
                    const std::uint64_t get_us =
                        elapsed_us(get_start, std::chrono::steady_clock::now());
                    frame_pso_get_us_ += get_us;
                    frame_pso_get_max_us_ =
                        std::max(frame_pso_get_max_us_, get_us);
                }
                // An uber stand-in is never memoized: the next resolution
                // should find the specialization once it is published.
                if (resolved != nullptr && !resolved_uber) {
                    memo_slot.hash = pso_key_hash;
                    memo_slot.key = pso_key;
                    memo_slot.pipeline = resolved;
                }
            }
            current_pipeline_ = resolved;
            current_pipeline_is_uber_ = resolved_uber;
            current_pso_key_ = pso_key;
            current_pso_key_hash_ = pso_key_hash;
            current_pso_key_valid_ = current_pipeline_ != nullptr;
            current_flat_pipeline_ = curved && resolved != nullptr && !resolved_uber
                ? resolve_flat_pipeline(pso_key) : nullptr;
            if (!resolved_uber) {
                describe_pipeline(resolved, vs_key_, ps_key_, render_state_key_);
            }
        }
        if (time_detail) {
            frame_flush_pso_us_ +=
                elapsed_us(pso_start, std::chrono::steady_clock::now());
        }
        if (pso_cycles_enabled) {
            const ThreadCounterSample interval = thread_counter_interval(
                pso_cycles_start, thread_cycle_count());
            if (frame_flush_pso_cycles_valid_ && interval.available &&
                interval.value <= std::numeric_limits<std::uint64_t>::max() -
                    frame_flush_pso_cycles_) {
                frame_flush_pso_cycles_ += interval.value;
            } else {
                // An incomplete numerator must not look like a complete total.
                frame_flush_pso_cycles_ = 0u;
                frame_flush_pso_cycles_valid_ = false;
            }
        }
    }
    if (current_pipeline_ == nullptr) {
        // The driver refused the pipeline in every form and no uber stand-in exists (the pipeline cache logged
        // why): the draw is skipped.
        return nullptr;
    }
    // An effects-layer draw binds the reactive mask and the layer's targets, uber stand-in or not (its variant writes
    // them); other stand-ins stay single-target.
    current_pipeline_aux_ = (render_state_key_.blend_bits & kRenderStateLayerTarget) != 0u ? 3u
        : current_pipeline_is_uber_ ? 0u
        : (render_state_key_.blend_bits & kRenderStateMotionTarget) != 0u ? 1u
        : (render_state_key_.blend_bits & kRenderStateReactiveTarget) != 0u ? 2u : 0u;

    // The XF matrix palette is snapshot only when the matrices changed (kDirtyXfMatrices); uploading it
    // on every TEV or render-state change would exhaust the per-frame matrix ring. Other draws reuse
    // the last snapshot's address, valid until begin_frame() rewinds the ring; the shader's palette
    // base is 0 because the root SRV points at the snapshot. A snapshot is the whole palette
    // (kMatrixPaletteWords * 4 = 6656 bytes), as a fresh ring allocation inherits nothing.
    const unsigned active_texgen_mask =
        vs_key_.num_texgens >= 8u
            ? 0xFFu
            : ((1u << vs_key_.num_texgens) - 1u);
    bool emboss_palette_required = false;
    for (unsigned texgen = 0; texgen < vs_key_.num_texgens; ++texgen) {
        emboss_palette_required |= state_.tex_gen(texgen).type == TexGenType::EmbossMap;
    }
    // Enhanced lighting reads the lights in the pixel shader, unlit draws included.
    const bool palette_required =
        emboss_palette_required ||
        (vs_key_.flags & (0x03u | kVsKeyEnhancedLighting)) != 0u ||
        (vs_key_.texmtx_idx_mask & active_texgen_mask) != 0u;
    current_palette_required_ = palette_required;
    // Actor lights (actor_lights.h): a lit scene draw's follow-camera lights are moved in its
    // snapshot; one moved otherwise (other lights, another scene's aim) is not reused for it. A
    // light load's positions are taken at the first draw after it; a lit draw's world-fixed key
    // light is counted.
    if (rt_light_tracking_ || rt_palette_lights_ != 0u) [[unlikely]] {
        const std::uint64_t start = trace_actor_lights_enabled() ? trace_cycles() : 0u;
        for (unsigned n = 0; n < 2u; ++n) {
            if ((rt_light_pending_ & (1u << n)) != 0u) {
                std::memcpy(rt_light_words_[n].data(),
                            state_.xf_raw() + xf::kLightsBase + kXfLightWords * n + kXfLightPosition,
                            sizeof(rt_light_words_[n]));
            }
        }
        rt_light_pending_ = 0u;
        if (rt_light_tracking_ && (vs_key_.flags & 0x02u) != 0u) {
            note_rt_world_key();
        }
        const std::uint32_t lights = (vs_key_.flags & 0x02u) != 0u ? rt_aimed_lights() : rt_palette_lights_;
        if (lights != rt_palette_lights_) {
            // Counts the snapshots this adds: the palette had not moved since the last one.
            rt_light_forced_ += !matrix_palette_stale_ && current_matrix_palette_ != 0u ? 1u : 0u;
            matrix_palette_stale_ = true;
            xf_palette_valid_ = 0u;
        }
        rt_draw_lights_ = lights;
        static unsigned traced = 0u;
        if (start != 0u) [[unlikely]] {
            rt_light_cycles_ += trace_cycles() - start;
            ++rt_light_regions_;
        }
        if (start != 0u && (lights & 0xFFu) != 0u && traced < 24u) [[unlikely]] {
            ++traced;
            const std::uint32_t* l = state_.xf_raw() + xf::kLightsBase;
            std::fprintf(stderr,
                         "[rt-actor-light] draw scene=%u lights=%u channels=%08x %08x %08x %08x "
                         "light0=%08x attn=(%g %g %g / %g %g %g) pos=(%g %g %g) light1=%08x pos=(%g %g %g)\n",
                         rt_light_serial_, lights & 0xFFu, state_.xf(xf::kChannelCtrlBase),
                         state_.xf(static_cast<std::uint16_t>(xf::kChannelCtrlBase + 1u)),
                         state_.xf(static_cast<std::uint16_t>(xf::kChannelCtrlBase + 2u)),
                         state_.xf(static_cast<std::uint16_t>(xf::kChannelCtrlBase + 3u)), l[3],
                         std::bit_cast<float>(l[4]), std::bit_cast<float>(l[5]), std::bit_cast<float>(l[6]),
                         std::bit_cast<float>(l[7]), std::bit_cast<float>(l[8]), std::bit_cast<float>(l[9]),
                         std::bit_cast<float>(l[10]), std::bit_cast<float>(l[11]), std::bit_cast<float>(l[12]), l[19],
                         std::bit_cast<float>(l[26]), std::bit_cast<float>(l[27]), std::bit_cast<float>(l[28]));
        }
    } else {
        rt_draw_lights_ = 0u;
    }
    if (palette_required &&
        (matrix_palette_stale_ || current_matrix_palette_ == 0)) {
        // Clock reads on detail frames only; the upload counts below are always kept.
        const auto matrix_start = time_detail
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        const std::size_t palette_bytes =
            kMatrixPaletteWords * sizeof(std::uint32_t);
        UploadRing::Allocation mtx_alloc{};
        // Dirty chunks decide whether the existing allocation is reusable. A new allocation takes the
        // whole palette: it inherits nothing, and patching an earlier one would change recorded draws.
        const std::uint64_t palette_todo =
            (state_.xf_palette_dirty() | ~xf_palette_valid_) &
            xf_palette_chunk_mask;
        if (palette_todo != 0u) {
            const std::uint64_t snapshot_start = trace_actor_lights_enabled() ? trace_cycles() : 0u;
            mtx_alloc =
                renderer_.matrix_ring().allocate(palette_bytes, /*alignment=*/256u);
            // XF memory has nothing at 0x100-0x3FF, which no reader addresses: the position and texture matrices,
            // then the normal, post-transform and light words.
            constexpr std::size_t kMatrixWords = 0x100u;
            constexpr std::size_t kNormalBase = 0x400u;
            std::memcpy(mtx_alloc.cpu, state_.xf_raw(), kMatrixWords * sizeof(std::uint32_t));
            std::memcpy(mtx_alloc.cpu + kNormalBase * sizeof(std::uint32_t),
                        state_.xf_raw() + kNormalBase, (kMatrixPaletteWords - kNormalBase) * sizeof(std::uint32_t));
            if (snapshot_start != 0u) [[unlikely]] {
                rt_snapshot_cycles_ += trace_cycles() - snapshot_start;
                ++rt_snapshot_count_;
            }
            current_matrix_palette_ = mtx_alloc.gpu;
            if (rt_draw_lights_ != 0u) [[unlikely]] {
                write_rt_lights(reinterpret_cast<std::uint32_t*>(mtx_alloc.cpu));
                ++rt_light_uploads_;
            }
            rt_palette_lights_ = rt_draw_lights_;
            if (interp_recording_ && history_scene_) [[unlikely]] {
                InterpScene& scene = interp_scenes_[interp_scene_];
                const std::size_t words = kMatrixPaletteWords;
                if (scene.palettes.size() < (scene.palette_count + 1u) * words) {
                    scene.palettes.resize((scene.palette_count + 1u) * words);
                }
                std::memcpy(scene.palettes.data() + scene.palette_count * words,
                            state_.xf_raw(), palette_bytes);
                if (rt_draw_lights_ != 0u) {
                    write_rt_lights(scene.palettes.data() + scene.palette_count * words);
                }
                current_palette_shadow_ = static_cast<std::int32_t>(scene.palette_count++);
            }
            // Every palette chunk was copied to this allocation, so mark the
            // complete image valid without computing a redundant dirty span.
            xf_palette_valid_ = xf_palette_chunk_mask;
            state_.clear_xf_palette_dirty(0u, GxState::kMatrixPaletteChunkCount - 1u);
            frame_matrix_upload_bytes_ += (kMatrixWords + kMatrixPaletteWords - kNormalBase) * sizeof(std::uint32_t);
        }
        // Every chunk is valid in this frame slot, so current_matrix_palette_ already points into this
        // frame's segment.
        matrix_palette_stale_ = false;
        if (time_detail) {
            frame_flush_matrix_us_ +=
                elapsed_us(matrix_start, std::chrono::steady_clock::now());
        }
    }
    draw_matrix_palette_ = history_scene_
        ? renderer_.matrix_ring().gpu_base()
        : current_matrix_palette_;

    // Upload VS constants when scalar state or fixed inline matrices change.
    if (std::exchange(sky_up_dirty_, false) || vs_c_dirty || xf_dirty || vcd_dirty || inline_matrices_stale_ ||
        frame_bindings_dirty_ || dirty == ~0u ||
        (history_scene_ && current_matrix_palette_ != temporal_vs_palette_va_)) {
        const auto constants_start = time_detail
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        const GxVsConstants& vs_c = cpu_vs_constants_.update(
            state_, dirty, renderer_.efb_scale(), frame_bindings_dirty_);
        UploadRing::Allocation vs_alloc =
            renderer_.constant_ring().allocate(sizeof(GxVsConstants), 256u);
        std::memcpy(vs_alloc.cpu, &vs_c, sizeof(vs_c));
        if (history_scene_) [[unlikely]] {
            write_scene_vs_constants(vs_c, *reinterpret_cast<GxVsConstants*>(vs_alloc.cpu));
        }
        apply_uber_vs_key(vs_key_, *reinterpret_cast<GxVsConstants*>(vs_alloc.cpu));
        current_vs_constants_ = vs_alloc.gpu;
        inline_matrices_stale_ = false;
        if (time_detail) {
            frame_flush_constants_us_ +=
                elapsed_us(constants_start, std::chrono::steady_clock::now());
        }
    }

    // Texture tables are refreshed only when a texture input changed. tex_dirty covers every input:
    // the binding key is captured only under kDirtyTex or the two binding flags, so otherwise the
    // result would be the current tables. dirty_texture_aliases_ outlives a flush (cleared by
    // prune_dirty_texture_binding_tables()) and is honoured even without tex_dirty, or an EFB-alias
    // copy followed by matrix-only flushes would keep its entries for the rest of the frame. Most
    // flushes carry kDirtyXfMatrices alone (the game loads matrices per object with LOAD_INDX).
    if (tex_dirty || !dirty_texture_aliases_.empty()) {
        const auto texture_start = time_detail
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        std::uint64_t texture_get_us = 0;
        std::uint64_t texture_table_us = 0;
        std::uint64_t sampler_table_us = 0;
        if (texture_bindings_dirty_) {
            prune_dirty_texture_binding_tables();
        }

        // A draw that samples no map still gets real tables (null SRVs and the default samplers):
        // the renderer skips null tables, leaving the root tables undefined after a command-list
        // reset, and an uber pixel shader samples every enabled map even when the combiner ignores
        // it, which faults on some GPUs.
        {
            bool binding_cache_hit = false;
            // Texture tables point into the frame-local shader-visible SRV
            // segment, so only the per-frame binding cache may own them.
            if (const auto frame_binding_it =
                    frame_texture_binding_tables_.find(texture_binding_key);
                frame_binding_it != frame_texture_binding_tables_.end()) {
                current_texture_table_ =
                    frame_binding_it->second.texture_table;
                current_texture_handles_ = frame_binding_it->second.texture_handles;
                current_sampler_table_ =
                    frame_binding_it->second.sampler_table;
                for (const auto& handle : frame_binding_it->second.texture_handles) {
                    texture_cache_.touch_cached_handle(handle);
                }
                texture_rt_material_ =
                    rt_material_of(texture_map_mask, frame_binding_it->second.texture_handles, current_rt_map_);
                refresh_rt_material();
                binding_cache_hit = true;
                ++frame_texture_binding_cache_hits_;
            }

            if (!binding_cache_hit) {
                ++frame_texture_binding_cache_misses_;
                std::array<TextureHandle, kMaxTextureMaps> texture_handles{};
                TextureBindingTables binding_tables{};
                TextureTableKey table_key{};
                table_key.map_mask = texture_map_mask;
                for (unsigned m = 0; m < kMaxTextureMaps; ++m) {
                    if ((texture_map_mask & (1u << m)) != 0u) {
                        const TexImage img = state_.tex_image(m);
                        const TexMode mode = state_.tex_mode(m);
                        const TlutRef tlut = state_.tex_tlut(m);
                        const TextureHandleKey handle_key =
                            texture_handle_key(img, mode, tlut);
                        if (const auto handle_it =
                                frame_texture_handle_cache_.find(handle_key);
                            handle_it != frame_texture_handle_cache_.end()) {
                            texture_handles[m] = handle_it->second;
                        } else {
                            if (const auto persistent_it =
                                    texture_handle_cache_.find(handle_key);
                                persistent_it != texture_handle_cache_.end()) {
                                texture_handles[m] = persistent_it->second;
                            } else {
                                const auto get_start = time_detail
                                    ? std::chrono::steady_clock::now()
                                    : std::chrono::steady_clock::time_point{};
                                texture_handles[m] = texture_cache_.get(
                                    img,
                                    mode,
                                    tlut,
                                    frame_memory_);
                                if (time_detail) {
                                    texture_get_us += elapsed_us(
                                        get_start,
                                        std::chrono::steady_clock::now());
                                }
                                texture_handle_cache_.emplace(
                                    handle_key,
                                    texture_handles[m]);
                            }
                            frame_texture_handle_cache_.emplace(
                                handle_key,
                                texture_handles[m]);
                        }
                        texture_cache_.touch_cached_handle(texture_handles[m]);
                        binding_tables.dependencies[
                            binding_tables.dependency_count++] =
                            TextureBindingDependency{
                                img.guest_addr,
                                img.width,
                                img.height,
                                static_cast<std::uint8_t>(img.format),
                                texture_handles[m].guest_byte_size,
                            };
                    }
                    const TextureHandle& th = texture_handles[m];
                    table_key.resources[m] =
                        reinterpret_cast<std::uintptr_t>(th.resource);
                    table_key.srv_indices[m] = th.srv_index;
                    table_key.widths[m] = th.width;
                    table_key.heights[m] = th.height;
                }
                const bool lit_table = lit_translucent_ && texture_handles[7].resource == nullptr;
                if (lit_table) {
                    table_key.resources[7] = reinterpret_cast<std::uintptr_t>(renderer_.rt_light_texture());
                    table_key.srv_indices[7] = 0xFFFFFFFFu;
                }

                const auto table_start = time_detail
                    ? std::chrono::steady_clock::now()
                    : std::chrono::steady_clock::time_point{};
                if (const auto table_it =
                        frame_texture_tables_.find(table_key);
                    table_it != frame_texture_tables_.end()) {
                    current_texture_table_ = table_it->second;
                } else {
                    // Reserve one SRV slot per texture map regardless of
                    // stage count, so the root table has a stable size
                    // matching the root signature.
                    current_texture_table_ = renderer_.texture_table_for(
                        texture_handles.data(), kMaxTextureMaps, texture_cache_, lit_table);
                    frame_texture_tables_.emplace(table_key, current_texture_table_);
                }
                if (time_detail) {
                    texture_table_us = elapsed_us(
                        table_start, std::chrono::steady_clock::now());
                }

                // Per-map sampler state from TexMode0/1 (wrap, filters, LOD
                // bias and range).  Packed key layout documented at
                // RendererD3D12::sampler_table_for; tables are
                // cached for this frame's fence-owned sampler heap.
                const auto samp_keys = capture_sampler_keys(
                    texture_map_mask, texture_handles);
                const auto sampler_start = time_detail
                    ? std::chrono::steady_clock::now()
                    : std::chrono::steady_clock::time_point{};
                current_sampler_table_ =
                    renderer_.sampler_table_for(samp_keys.data());
                if (time_detail) {
                    sampler_table_us = elapsed_us(
                        sampler_start, std::chrono::steady_clock::now());
                }
                TextureBindingTables cached_tables{
                    current_texture_table_,
                    current_sampler_table_,
                    binding_tables.dependencies,
                    binding_tables.dependency_count,
                    texture_handles,
                };
                frame_texture_binding_tables_.emplace(
                    texture_binding_key,
                    cached_tables);
                current_texture_handles_ = texture_handles;
                texture_rt_material_ = rt_material_of(texture_map_mask, texture_handles, current_rt_map_);
                refresh_rt_material();
            }
        }
        current_texture_map_mask_ = texture_map_mask;
        current_texture_binding_key_ = texture_binding_key;
        current_texture_binding_key_valid_ = true;
        current_texture_binding_key_mask_ = texture_map_mask;
        if (time_detail) {
            const std::uint64_t total_texture_us =
                elapsed_us(texture_start, std::chrono::steady_clock::now());
            frame_flush_texture_us_ += total_texture_us;
            if (trace_gx_stalls_enabled() &&
                total_texture_us >= trace_gx_stall_threshold_us()) {
                std::cerr
                    << "[gx-texture-flush] frame=" << frame_index_
                    << " map-mask=0x" << std::hex
                    << static_cast<unsigned>(texture_map_mask)
                    << std::dec
                    << " get-us=" << texture_get_us
                    << " table-us=" << texture_table_us
                    << " sampler-us=" << sampler_table_us
                    << " total-us=" << total_texture_us
                    << "\n";
            }
        }
    }

    // Upload PS constants when TEV stage config / fog / alpha / tex dims change, or an enhanced
    // draw's material table entry does (after the texture binding, which finds it).
    const std::uint32_t enhanced_material_bits =
        (ps_key_.reserved & kPsKeyEnhancedLighting) != 0u ? enhanced_table_material(rt_material(current_rt_material_)) : 0u;
    // A glow's gain follows its material (a light source's radiance): another material is another gain. A lit
    // translucent's material decides whether it is falling water (its constants' tex_dims[7].w).
    const std::uint16_t glow_material = (ps_key_.output_flags & 0x60u) != 0u ? current_rt_material_ : 0u;
    // A clear ice or crystal draw under physical light (rt_materials.h kRtIceTintKept): the share of its art's tint it
    // loses, in its TEV and konst registers here and its rasterized colours in the pixel shader (ztex_params.w).
    float tint_cleared = 0.0f;
    if (const RtMaterial* const tinted = rt_material(current_rt_material_);
        tinted != nullptr && tinted->tint_kept < 1.0f && scene_phase_ && renderer_.rt_physical()) {
        tint_cleared = 1.0f - std::clamp(tinted->tint_kept, 0.0f, 1.0f);
    }
    if (tev_dirty || tev_c_dirty || tex_dirty || frame_bindings_dirty_ || glow_changed || enhanced_changed ||
        dirty == ~0u || (ps_record_restart_ && history_scene_) || enhanced_material_bits != enhanced_material_bits_ ||
        glow_material != glow_material_ || tint_cleared != ps_tint_cleared_) {
        enhanced_material_bits_ = enhanced_material_bits;
        glow_material_ = glow_material;
        ps_tint_cleared_ = tint_cleared;
        const auto constants_start = time_detail
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        GxPsConstants ps_c{};
        fill_ps_constants(state_, ps_c, scales_from_textures_);
        if (tint_cleared > 0.0f) {
            for (auto& reg : ps_c.tev_regs) {
                clear_register_tint(reg, tint_cleared);
            }
            for (auto& reg : ps_c.konst) {
                clear_register_tint(reg, tint_cleared);
            }
            // Its maps that are textures clear their texels too (a seesaw's blue IceFace01); the screen captures it
            // shows through it (the ice's and the crystals' refraction, the floors' mirror) keep their colours.
            // alpha_refs.z bits 8-15, above the alpha test's packed modes.
            std::uint32_t textures = 0u;
            for (unsigned m = 0; m < kMaxTextureMaps; ++m) {
                if ((current_texture_map_mask_ & (1u << m)) != 0u && !current_texture_handles_[m].from_efb_copy) {
                    textures |= 1u << m;
                }
            }
            ps_c.alpha_refs[2] = std::bit_cast<float>(std::bit_cast<std::uint32_t>(ps_c.alpha_refs[2]) | (textures << 8u));
        }
        ps_c.ztex_params[1] = 1.0f / static_cast<float>(renderer_.efb_scale());
        // A glow's gain (output_flags bit 5): 1 under physical light (the game's additive effects as it drew them;
        // their light reaches the scene as the engine's emitting geometry), the art model's glow exposure otherwise.
        ps_c.ztex_params[2] = (ps_key_.output_flags & 0x20u) == 0u ? 0.0f
                              : renderer_.rt_physical() ? 1.0f
                                                        : renderer_.rt_glow_exposure(glow_kind_ == 2u);
        // The lighting texture's scale (bit 6); 0 leaves a lit translucent unlit: a table light source (its own light),
        // an effect that is light (`luminous`: sparkles, glows, fire; the game draws them unlit, at the brightness they
        // shine, and lit by the surface behind them a sparkle grew into a flat orange star) or a clear surface (glass,
        // crystal, ice, water: the lit scene shows through it). Falling water is white water, a foam of bubbles and
        // drops that scatters what reaches it: it takes the light at its own sheet (rt_engine.hlsl water_light).
        bool takes_light = (ps_key_.output_flags & 0x40u) != 0u;
        float recolour = 0.0f;
        if (const RtMaterial* const m = rt_material(current_rt_material_);
            m != nullptr && (ps_key_.output_flags & 0x60u) != 0u) {
            const bool emits = m->emission != RtEmission::None && !m->no_light && !m->hide;
            const bool clear = (m->surface_class == static_cast<std::uint8_t>(RtSurfaceClass::Crystal) ||
                                m->surface_class == static_cast<std::uint8_t>(RtSurfaceClass::Water)) &&
                               !m->waterfall;
            takes_light = takes_light && !emits && !clear && !m->luminous;
            // `recolour`: a light source's sprite in the table's colour rather than the one the game draws its glow
            // in, as RGB8 + 1, negated.
            if (emits && m->recolour) {
                const auto byte = [](float v) {
                    return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
                };
                recolour = -static_cast<float>(1u + byte(m->color[0]) + (byte(m->color[1]) << 8u) +
                                               (byte(m->color[2]) << 16u));
            }
        }
        // Else the tint the draw loses, between -1 and 0 (below any recolour code, which is at most -1). A tinted draw
        // that takes the light (a translucent of an unclear class) keeps its rasterized colours' tint.
        ps_c.ztex_params[3] =
            takes_light ? renderer_.rt_light_texture_scale() : recolour != 0.0f ? recolour : -tint_cleared;
        // A lit translucent's kind (shader_gen; map 7 is the light texture there, so its texture dimensions are free
        // and always set here: fill_ps_constants leaves map 7's texcoord scale, at least 1, in them): 0 an effect or
        // any other translucent, 1 falling water (clear where the art paints its water hue), 2 water, glass or
        // crystal (the water pass's surfaces). Only 0 covers what lies behind for the water's composites (the
        // reactive mask).
        if ((ps_key_.output_flags & 0x40u) != 0u) {
            float kind = 0.0f;
            if (const RtMaterial* const m = rt_material(current_rt_material_); m != nullptr) {
                const bool clear_class = m->surface_class == static_cast<std::uint8_t>(RtSurfaceClass::Crystal) ||
                                         m->surface_class == static_cast<std::uint8_t>(RtSurfaceClass::Water);
                // Falling water: kind 1 plus 0.4 x its aeration (the share of its clear water's light its bubbles
                // scatter: the table's, else rt_engine.hlsl kFallAeration's 0.36), below 1.5.
                kind = takes_light && m->waterfall ? 1.0f + 0.4f * (m->aeration >= 0.0f ? m->aeration : 0.36f)
                       : clear_class               ? 2.0f
                                                   : 0.0f;
                // Falling water's clear water colour as the art paints it (the table row's; white: none).
                if (kind < 1.5f && kind >= 1.0f) {
                    ps_c.tex_dims[7][0] = m->color[0];
                    ps_c.tex_dims[7][1] = m->color[1];
                    ps_c.tex_dims[7][2] = m->color[2];
                }
            }
            ps_c.tex_dims[7][3] = kind;
        }
        // GALAXY_RT_LOG_GLOWS=1: each scene draw's material once per phase, glow kind, lighting texture and J3D
        // material identity (mat_id.py names the material, material_names.py --find the identity).
        if (scene_phase_ && rt_log_glows()) [[unlikely]] {
            static std::set<std::uint64_t> logged;
            const std::uint64_t key = (static_cast<std::uint64_t>(rt_identity_seen_) << 40u) |
                                      (static_cast<std::uint64_t>(current_rt_material_) << 24u) |
                                      (static_cast<std::uint64_t>(rt_phase_) << 8u) | glow_kind_ |
                                      (ps_c.ztex_params[3] > 0.0f ? 0x80u : 0u);
            if (logged.insert(key).second) {
                std::fprintf(stderr,
                             "[rt-draw-material] material=%u phase=%u kind=%u gain=%.2f lit-scale=%.3f identity=%08X\n",
                             static_cast<unsigned>(current_rt_material_), static_cast<unsigned>(rt_phase_),
                             static_cast<unsigned>(glow_kind_), ps_c.ztex_params[2], ps_c.ztex_params[3],
                             rt_identity_seen_);
            }
        }
        apply_uber_ps_key(ps_key_, ps_c);
        if ((ps_key_.reserved & kPsKeyEnhancedLighting) != 0u) {
            ps_c.uber_ps_header[3] = enhanced_ps_flags_ | enhanced_material_bits;
        }

        UploadRing::Allocation ps_alloc =
            renderer_.constant_ring().allocate(sizeof(GxPsConstants), 256u);
        std::memcpy(ps_alloc.cpu, &ps_c, sizeof(ps_c));
        current_ps_constants_ = ps_alloc.gpu;
        if (interp_recording_ && history_scene_) [[unlikely]] {
            ps_record_restart_ = false;
            InterpScene& scene = interp_scenes_[interp_scene_];
            current_ps_record_ = static_cast<std::int32_t>(scene.ps_constants.size());
            scene.ps_constants.push_back(ps_c);
        }
        if (time_detail) {
            frame_flush_constants_us_ +=
                elapsed_us(constants_start, std::chrono::steady_clock::now());
        }
    }

    frame_bindings_dirty_ = false;
    texture_bindings_dirty_ = false;
    // The frame's detail flag guards both endpoints. A zero on an ordinary
    // frame denotes an unavailable timing, not zero renderer work.
    if (time_detail) {
        frame_flush_us_ +=
            elapsed_us(flush_start, std::chrono::steady_clock::now());
    }
    return current_pipeline_;
}

bool GxBackend::draw_calls_batch_compatible(
    const DrawCall& lhs,
    const DrawCall& rhs,
    bool lhs_indexed,
    bool rhs_indexed) noexcept {
    if (lhs_indexed != rhs_indexed ||
        lhs.pipeline != rhs.pipeline ||
        lhs.vs_constants != rhs.vs_constants ||
        lhs.ps_constants != rhs.ps_constants ||
        lhs.texture_table.ptr != rhs.texture_table.ptr ||
        lhs.sampler_table.ptr != rhs.sampler_table.ptr ||
        lhs.matrix_palette != rhs.matrix_palette ||
        lhs.topology != rhs.topology ||
        lhs.has_viewport != rhs.has_viewport ||
        lhs.flat_pipeline != nullptr || rhs.flat_pipeline != nullptr) {
        return false;
    }
    if (lhs.scissor.left != rhs.scissor.left ||
        lhs.scissor.top != rhs.scissor.top ||
        lhs.scissor.right != rhs.scissor.right ||
        lhs.scissor.bottom != rhs.scissor.bottom) {
        return false;
    }
    if (!lhs.has_viewport) {
        return true;
    }
    return std::memcmp(
               lhs.xf_viewport,
               rhs.xf_viewport,
               sizeof(lhs.xf_viewport)) == 0;
}

void GxBackend::flush_pending_draw_batch() {
    if (!pending_draw_batch_.active) {
        return;
    }

    const bool time_detail = frame_time_detail_;
    const auto draw_record_start = time_detail
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    if (!pending_stripe_vertices_.empty()) [[unlikely]] {
        renderer_.note_stripe_vertices(pending_stripe_vertices_);
        pending_stripe_vertices_.clear();
    }
    // A hidden draw stays out of the picture and of the traced scene, unless the engine traces it (clear glass whose
    // game draw paints a fake reflection: hidden, its geometry the engine's glass) or it is trace-only.
    if (pending_draw_batch_.rt && (!pending_draw_batch_.hidden || pending_draw_batch_.traced)) {
        renderer_.rt_note_draw(pending_draw_batch_.call, pending_draw_batch_.rt_info);
    }
    if (!pending_draw_batch_.hidden) {
        const DrawCall& call = pending_draw_batch_.call;
        if (call.topology == gpu::kTopologyTrianglePatches) {
            frame_curved_flat_triangles_ += call.flat_index_count / 3u;
            frame_curved_patch_triangles_ += (call.index_count - call.flat_index_count) / 3u;
            frame_curved_split_draws_ += call.flat_pipeline != nullptr ? 1u : 0u;
        }
        renderer_.draw(call);
    }
    if (pending_draw_batch_.debake_model != 0u) [[unlikely]] {
        sample_debake_model(pending_draw_batch_.debake_model, pending_draw_batch_.call);
    }
    if (time_detail) {
        frame_draw_record_us_ +=
            elapsed_us(draw_record_start, std::chrono::steady_clock::now());
    }
    ++frame_draw_batches_;
    pending_draw_batch_ = {};
}

bool GxBackend::begin_cached_draw_run(
    PrimitiveClass primitive,
    std::uint8_t vtxfmt,
    std::size_t draw_count) {
    (void)draw_count;
    if (cached_draw_run_active_) {
        throw std::runtime_error(
            "GxBackend: nested cached display-list draw run");
    }
    gpu::Topology topology{};
    gpu::TopologyType topology_type{};
    gx_topology_for_primitive(primitive, topology, topology_type);

    const EfbScissor efb_sc = state_.efb_scissor();
    const ScissorRect sc = efb_sc.rect;
    // The reject flag also covers fully culled polygons; lines and points stay visible under
    // GX_CULL_ALL.
    cached_draw_run_scissor_reject_ =
        cull_all_suppresses_primitive(state_.gen_mode().cull, primitive) ||
        sc.x1 < sc.x0 || sc.y1 < sc.y0 ||
        sc.x1 < 0 || sc.y1 < 0 ||
        sc.x0 >= static_cast<std::int32_t>(kEfbWidth) ||
        sc.y0 >= static_cast<std::int32_t>(kEfbHeight);
    cached_draw_run_primitive_ = primitive;
    cached_draw_run_vtxfmt_ = vtxfmt;
    cached_draw_run_primitive_indexed_ =
        primitive != PrimitiveClass::Triangles;
    cached_draw_run_no_pso_ = false;
    cached_draw_run_batch_compatible_known_ = false;
    cached_draw_run_state_call_ = {};
    cached_draw_run_active_ = true;
    if (cached_draw_run_scissor_reject_) {
        return true;
    }

    gpu::Pipeline* pso = nullptr;
    {
        ScopedFlushTiming flush_timing{
            frame_flush_timed_calls_,
            frame_flush_total_us_,
            frame_flush_timed_samples_};
        pso = flush_draw_state(topology_type, vtxfmt);
    }
    if (pso == nullptr) {
        flush_pending_draw_batch();
        cached_draw_run_no_pso_ = true;
        return true;
    }

    const gpu::Rect scissor_rect = gpu::rect(
        sc.x0 < 0 ? 0 : sc.x0, sc.y0 < 0 ? 0 : sc.y0, sc.x1 + 1, sc.y1 + 1);

    cached_draw_run_state_call_.pipeline       = pso;
    cached_draw_run_state_call_.vs_constants   = current_vs_constants_;
    cached_draw_run_state_call_.ps_constants   = current_ps_constants_;
    cached_draw_run_state_call_.texture_table  = current_texture_table_;
    cached_draw_run_state_call_.sampler_table =
        (current_sampler_table_.ptr != 0)
            ? current_sampler_table_
            : renderer_.default_sampler_table();
    cached_draw_run_state_call_.matrix_palette = draw_matrix_palette_;
    cached_draw_run_state_call_.aux_target     = current_pipeline_aux_;
    cached_draw_run_state_call_.replay_record  = current_replay_record_;
    cached_draw_run_state_call_.ps_record      = current_ps_record_;
    cached_draw_run_state_call_.topology       = draw_topology(topology);
    cached_draw_run_state_call_.scissor        = scissor_rect;
    for (unsigned i = 0; i < 6; ++i) {
        cached_draw_run_state_call_.xf_viewport[i] = std::bit_cast<float>(
            state_.xf(static_cast<std::uint16_t>(xf::kViewportBase + i)));
    }
    // Viewport origin in EFB pixels: screen position minus the scissor offset.
    cached_draw_run_state_call_.xf_viewport[3] -= static_cast<float>(efb_sc.offset.x);
    cached_draw_run_state_call_.xf_viewport[4] -= static_cast<float>(efb_sc.offset.y);
    cached_draw_run_state_call_.has_viewport =
        cached_draw_run_state_call_.xf_viewport[0] > 0.0f &&
        cached_draw_run_state_call_.xf_viewport[1] < 0.0f;
    return true;
}

void GxBackend::end_cached_draw_run() {
    cached_draw_run_active_ = false;
    cached_draw_run_scissor_reject_ = false;
    cached_draw_run_primitive_indexed_ = false;
    cached_draw_run_no_pso_ = false;
    cached_draw_run_batch_compatible_known_ = false;
    cached_draw_run_primitive_ = PrimitiveClass::Points;
    cached_draw_run_vtxfmt_ = 0;
    cached_draw_run_state_call_ = {};
}

void GxBackend::on_cached_draw_run_draw(
    PrimitiveClass primitive,
    std::uint8_t vtxfmt,
    FifoCursor& cursor) {
    ++frame_replay_generic_draws_;
    if (primitive != cached_draw_run_primitive_ ||
        vtxfmt != cached_draw_run_vtxfmt_) {
        throw GxFatalError(
            "GxBackend: cached display-list draw run changed format",
            cursor.command_offset,
            0xFFu);
    }

    if (cached_draw_run_scissor_reject_) {
        const VertexLayoutCacheEntry& layout = cached_vertex_layout(vtxfmt);
        const std::uint16_t vertex_count = cursor.read_u16();
        cursor.take(
            layout.source_size * static_cast<std::size_t>(vertex_count));
        ++frame_scissor_skips_;
        return;
    }

    if (cached_draw_run_no_pso_) {
        flush_pending_draw_batch();
        ++stat_draws_no_pso_;
        ++frame_draws_no_pso_;
        const bool time_detail = frame_time_detail_;
        const auto vertex_start = time_detail
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        const VertexLayoutCacheEntry& layout = cached_vertex_layout(vtxfmt);
        vertex_loader_.load_with_layout(
            cursor, primitive, state_, layout.desc,
            layout.source_size, frame_memory_, renderer_.vertex_ring(),
            renderer_.index_ring());
        if (time_detail) {
            frame_vertex_load_us_ +=
                elapsed_us(vertex_start, std::chrono::steady_clock::now());
        }
        return;
    }

    cursor.require(2);
    const std::uint16_t source_vertex_count =
        static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(cursor.data[cursor.offset]) << 8) |
            static_cast<std::uint16_t>(cursor.data[cursor.offset + 1]));

    bool append_to_pending = false;
    if (pending_draw_batch_.active && !pending_draw_batch_.resident) {
        append_to_pending = cached_draw_run_batch_compatible_known_ ||
            draw_calls_batch_compatible(
                pending_draw_batch_.call,
                cached_draw_run_state_call_,
                pending_draw_batch_.indexed,
                cached_draw_run_primitive_indexed_);
        cached_draw_run_batch_compatible_known_ = append_to_pending;
    }
    if (append_to_pending && !cached_draw_run_primitive_indexed_ &&
        primitive == PrimitiveClass::Triangles &&
        (pending_draw_batch_.vertex_count % 3u != 0u || source_vertex_count % 3u != 0u)) {
        append_to_pending = false;
        cached_draw_run_batch_compatible_known_ = false;
    }
    if (append_to_pending && cached_draw_run_primitive_indexed_ &&
        pending_draw_batch_.vertex_count +
            static_cast<std::uint32_t>(source_vertex_count) >
            std::numeric_limits<std::uint16_t>::max()) {
        flush_pending_draw_batch();
        append_to_pending = false;
        cached_draw_run_batch_compatible_known_ = false;
    }
    if (pending_draw_batch_.active && !append_to_pending) {
        flush_pending_draw_batch();
        cached_draw_run_batch_compatible_known_ = false;
    }

    const bool time_detail = frame_time_detail_;
    const auto vertex_start = time_detail
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    const VertexLayoutCacheEntry& layout = cached_vertex_layout(vtxfmt);
    const LoadedPrimitive prim = vertex_loader_.load_with_layout(
        cursor, primitive, state_, layout.desc, layout.source_size,
        frame_memory_, renderer_.vertex_ring(), renderer_.index_ring(),
        append_to_pending && cached_draw_run_primitive_indexed_
            ? pending_draw_batch_.base_vertex
            : 0u,
        append_to_pending && cached_draw_run_primitive_indexed_);
    if (time_detail) {
        frame_vertex_load_us_ +=
            elapsed_us(vertex_start, std::chrono::steady_clock::now());
    }

    if (prim.index_count == 0) {
        if (pending_draw_batch_.active) {
            flush_pending_draw_batch();
            cached_draw_run_batch_compatible_known_ = false;
        }
        return;
    }
    ++stat_draws_issued_;
    ++frame_draws_issued_;

    if (append_to_pending) {
        if (prim.indexed != pending_draw_batch_.indexed) {
            throw GxFatalError(
                "GxBackend: cached draw batch indexing changed inside a run",
                cursor.command_offset,
                0);
        }
        const std::size_t vertex_byte_end =
            prim.vertex_byte_offset +
            static_cast<std::size_t>(prim.vertex_count) * sizeof(GxVertexOut);
        const bool vertex_contiguous =
            prim.vertex_byte_offset == pending_draw_batch_.vertex_byte_end;
        const bool index_contiguous =
            !prim.indexed ||
            prim.first_index ==
                pending_draw_batch_.first_index +
                pending_draw_batch_.index_count;
        if (!vertex_contiguous || !index_contiguous) {
            throw GxFatalError(
                "GxBackend: cached draw batch upload rings were not contiguous",
                cursor.command_offset,
                0);
        }
        pending_draw_batch_.call.index_count += prim.index_count;
        pending_draw_batch_.call.vertex_view.SizeInBytes +=
            prim.vertex_count * static_cast<std::uint32_t>(sizeof(GxVertexOut));
        if (prim.indexed) {
            pending_draw_batch_.call.index_view.SizeInBytes +=
                prim.index_count * static_cast<std::uint32_t>(sizeof(std::uint16_t));
        }
        pending_draw_batch_.vertex_count += prim.vertex_count;
        pending_draw_batch_.vertex_byte_end = vertex_byte_end;
        pending_draw_batch_.index_count += prim.index_count;
        ++pending_draw_batch_.draw_count;
        return;
    }

    const gpu::VertexView vbv = gpu::vertex_view(
        renderer_.vertex_ring().gpu_base() + prim.vertex_byte_offset,
        prim.vertex_count * static_cast<std::uint32_t>(sizeof(GxVertexOut)), static_cast<std::uint32_t>(sizeof(GxVertexOut)));

    gpu::IndexView ibv{};
    if (prim.indexed) {
        ibv = gpu::index_view(
            renderer_.index_ring().gpu_base() + static_cast<std::size_t>(prim.first_index) * sizeof(std::uint16_t),
            prim.index_count * static_cast<std::uint32_t>(sizeof(std::uint16_t)));
    }

    DrawCall call = cached_draw_run_state_call_;
    call.vertex_view = vbv;
    call.index_view = ibv;
    call.index_count = prim.index_count;

    const std::size_t vertex_byte_end =
        prim.vertex_byte_offset +
        static_cast<std::size_t>(prim.vertex_count) * sizeof(GxVertexOut);
    flush_pending_draw_batch();
    pending_draw_batch_.active = true;
    pending_draw_batch_.call = call;
    pending_draw_batch_.rt = begin_rt_batch(call, pending_draw_batch_.rt_info);
    exclude_layer_water(pending_draw_batch_.call, pending_draw_batch_.rt_info, pending_draw_batch_.rt);
    pending_draw_batch_.hidden = rt_hidden_draw();
    pending_draw_batch_.traced = pending_draw_batch_.hidden && (rt_traced_clear_draw() || rt_trace_only_);
    pending_draw_batch_.debake_model = debake_sample_key_;
    pending_draw_batch_.indexed = prim.indexed;
    pending_draw_batch_.base_vertex = prim.base_vertex;
    pending_draw_batch_.vertex_count = prim.vertex_count;
    pending_draw_batch_.vertex_byte_end = vertex_byte_end;
    pending_draw_batch_.first_index = prim.first_index;
    pending_draw_batch_.index_count = prim.index_count;
    pending_draw_batch_.draw_count = 1;
    cached_draw_run_batch_compatible_known_ = true;
}

const GxBackend::VertexLayoutCacheEntry& GxBackend::cached_vertex_layout(
    std::uint8_t vtxfmt) const {
    const auto index = static_cast<std::uint8_t>(vtxfmt & 0x07u);
    VertexLayoutCacheEntry& entry = vertex_layout_cache_[index];
    const std::uint32_t vcd_lo = state_.cp(cp::kVcdLo);
    const std::uint32_t vcd_hi = state_.cp(cp::kVcdHi);
    const std::uint32_t vat_a =
        state_.cp(static_cast<std::uint8_t>(cp::kVatABase + index));
    const std::uint32_t vat_b =
        state_.cp(static_cast<std::uint8_t>(cp::kVatBBase + index));
    const std::uint32_t vat_c =
        state_.cp(static_cast<std::uint8_t>(cp::kVatCBase + index));

    if (!entry.valid ||
        entry.vcd_lo != vcd_lo ||
        entry.vcd_hi != vcd_hi ||
        entry.vat_a != vat_a ||
        entry.vat_b != vat_b ||
        entry.vat_c != vat_c) {
        entry.valid = true;
        entry.vcd_lo = vcd_lo;
        entry.vcd_hi = vcd_hi;
        entry.vat_a = vat_a;
        entry.vat_b = vat_b;
        entry.vat_c = vat_c;
        entry.desc = state_.vertex_desc(index);
        entry.source_size = VertexLoader::source_vertex_size(entry.desc);
    }

    return entry;
}

std::size_t GxBackend::draw_payload_size(
    std::uint8_t vtxfmt,
    std::uint16_t vertex_count) const {
    return cached_vertex_layout(vtxfmt).source_size *
           static_cast<std::size_t>(vertex_count);
}

void GxBackend::on_shape_record(const shapes::Record& record) {
    switch (record.kind) {
    case shapes::Record::Kind::Array:
        shape_arrays_[record.key] = ShapeArrayEntry{record.array, ++shape_array_versions_};
        return;
    case shapes::Record::Kind::Geometry: {
        RecordShape& shape = record_shapes_[record.key];
        release_record_shape(shape);
        shape = RecordShape{};
        shape.geometry = record.geometry;
        return;
    }
    case shapes::Record::Kind::Draw:
        draw_record_shape(record);
        return;
    case shapes::Record::Kind::Material:
        apply_record_material(*record.material);
        return;
    case shapes::Record::Kind::State: {
        // As the stream parser applies the same writes: a copy, a texture invalidation or a TLUT load acts at once.
        struct Events final : state::Writes {
            GxBackend& backend;
            explicit Events(GxBackend& b) : backend(b) {}
            void bp(std::uint32_t word) override {
                backend.state_.load_bp(word);
                const auto reg = static_cast<std::uint8_t>(word >> 24u);
                const std::uint32_t value = backend.state_.bp(reg);
                if (reg == bp::kCopyExecute) {
                    backend.on_efb_copy(value);
                } else if (reg == 0x66u && value == 0u) {
                    backend.on_invalidate_textures();
                } else if (reg == bp::kTlutDest) {
                    backend.on_tlut_load();
                }
            }
            void xf(std::uint16_t address, std::uint32_t value) override { backend.state_.load_xf(address, &value, 1u); }
            void cp(std::uint8_t reg, std::uint32_t value) override { backend.state_.load_cp(reg, value); }
            std::uint32_t bp_read(std::uint8_t reg) override { return backend.state_.bp(reg); }
            std::uint32_t xf_read(std::uint16_t address) override { return backend.state_.xf(address); }
            std::uint32_t cp_read(std::uint8_t reg) override { return backend.state_.cp(reg); }
        } events(*this);
        // The game's TEV order, indirect order or texture object changes: GX re-derived the coordinate scales then.
        for (const state::Op& op : record.state) {
            if (op.kind == state::kTevStageOrder || op.kind == state::kIndTexOrder ||
                (op.kind == state::kTexImage && op.a[1] != 0u)) {
                scales_from_textures_ = true;
                break;
            }
        }
        state::encode(record.state, events);
        return;
    }
    case shapes::Record::Kind::TextureImage:
        flush_pending_draw_batch();
        texture_cache_.register_image(static_cast<std::uint32_t>(record.key), record.image);
        // New bytes at that address: bindings made from what was there before are out of date too (a buffer the game
        // reuses for its next image, as the Mii library's part textures).
        if (record.image != nullptr) {
            invalidate_texture_binding_cache_range(static_cast<std::uint32_t>(record.key),
                                                   static_cast<std::uint32_t>(record.image->size()));
        }
        texture_bindings_dirty_ = true;
        current_texture_binding_key_valid_ = false;
        return;
    case shapes::Record::Kind::Immediate:
        draw_record_immediate(*record.immediate);
        return;
    case shapes::Record::Kind::Readback:
        // The picture here, as the channel's next field (its reads take the latest finished one).
        flush_pending_draw_batch();
        record_async_peek_fields_at_token(static_cast<std::uint16_t>(record.key));
        return;
    }
}

void GxBackend::apply_record_material(const material::Material& m) {
    material::apply(m, state_, this);
    // The material wrote its coordinate scales.
    scales_from_textures_ = false;
}

void GxBackend::release_record_shape(RecordShape& shape) noexcept {
    if (shape.resident_epoch != 0u && shape.resident_epoch == resident_vertices_.epoch()) {
        resident_vertices_.release(shape.resident_offset, shape.resident_units);
    }
    shape.resident_epoch = 0u;
}

void GxBackend::apply_record_matrices(const shapes::Record& record) {
    for (const shapes::Matrix& m : record.matrices) {
        std::uint16_t base = static_cast<std::uint16_t>(4u * m.id);
        std::uint16_t count = 12u;
        switch (m.kind) {
        case shapes::MatrixKind::Normal:
            base = static_cast<std::uint16_t>(0x400u + 3u * m.id);
            count = 9u;
            break;
        case shapes::MatrixKind::Texture2x4:
            count = 8u;
            break;
        case shapes::MatrixKind::PostTexture:
            base = static_cast<std::uint16_t>(0x500u + 4u * (m.id - 64u));
            break;
        default:
            break;
        }
        std::array<std::uint32_t, 12> words{};
        for (unsigned i = 0; i < count; ++i) {
            words[i] = std::bit_cast<std::uint32_t>(m.values[i]);
        }
        if (m.model_matrix != 0u) {
            // A model's draw matrix by its index, named as its indexed load named it (array 0 positions, 1 normals).
            const std::uint32_t array = m.kind == shapes::MatrixKind::Normal ? 1u : 0u;
            state_.load_xf_indexed(base, words.data(), count, array << 16u | (m.model_matrix - 1u));
        } else {
            state_.load_xf(base, words.data(), count);
        }
    }
    if (record.has_current) {
        const auto& c = record.current;
        const std::uint32_t a = c[0] | static_cast<std::uint32_t>(c[1]) << 6u | static_cast<std::uint32_t>(c[2]) << 12u |
                                static_cast<std::uint32_t>(c[3]) << 18u | static_cast<std::uint32_t>(c[4]) << 24u;
        const std::uint32_t b = c[5] | static_cast<std::uint32_t>(c[6]) << 6u | static_cast<std::uint32_t>(c[7]) << 12u |
                                static_cast<std::uint32_t>(c[8]) << 18u;
        state_.load_cp(cp::kMatrixIndexA, a);
        state_.load_cp(cp::kMatrixIndexB, b);
        const std::uint32_t words[2] = {a, b};
        state_.load_xf(0x1018u, words, 2u);
    }
    for (const auto& g : record.texgens) {
        const std::uint32_t gen = material::tex_gen_word(g[1], g[2]);
        const std::uint32_t post = static_cast<std::uint32_t>(g[4] - 64u) | static_cast<std::uint32_t>(g[3]) << 8u;
        state_.load_xf(static_cast<std::uint16_t>(0x1040u + g[0]), &gen, 1u);
        state_.load_xf(static_cast<std::uint16_t>(0x1050u + g[0]), &post, 1u);
    }
}

void GxBackend::draw_record_shape(const shapes::Record& record) {
    apply_record_matrices(record);
    if (record.key == 0u) {
        return;  // state only (the matrices, current matrices and texgens set before a material record)
    }
    const auto found = record_shapes_.find(record.key);
    if (found == record_shapes_.end() || found->second.geometry == nullptr) {
        ++frame_native_shape_misses_;
        static bool warned = false;
        if (!warned) {
            warned = true;
            std::fprintf(stderr, "[native-shapes] a draw of group %llx, which was never registered\n",
                         static_cast<unsigned long long>(record.key));
        }
        return;
    }
    RecordShape& shape = found->second;
    const shapes::Geometry& geometry = *shape.geometry;
    // The arrays it reads: the draw's position, normal and colour 0 (a bump-mapped group's NBT array is its own), the
    // group's for the rest.
    std::array<std::uint64_t, 12> keys = geometry.arrays;
    keys[0] = record.position;
    if ((geometry.attributes & shapes::kNbt) == 0u) {
        keys[1] = record.normal;
    }
    keys[2] = record.color;
    shapes::DrawArrays arrays{};
    std::array<std::uint64_t, 12> versions{};
    for (unsigned i = 0; i < 12u; ++i) {
        if (keys[i] == 0u) {
            continue;
        }
        if (const auto a = shape_arrays_.find(keys[i]); a != shape_arrays_.end()) {
            arrays[i] = a->second.array.get();
            versions[i] = a->second.version;
        }
    }
    // Corners without a position matrix of their own take the one J3DCurrentMtx::load set.
    const auto position_matrix = static_cast<std::uint8_t>(
        (geometry.attributes & shapes::kPositionMatrix) != 0u ? 0u : state_.cp(cp::kMatrixIndexA) & 0x3Fu);
    if (!shape.expanded || shape.versions != versions || shape.position_matrix != position_matrix) {
        // The model's analysis for its curved surfaces (its first draw waits for it).
        shape.curved = curved::find_model(keys[0]);
        if (!shapes::expand(geometry, arrays, position_matrix, shape.curved.get(), shape.vertices) ||
            shape.vertices.empty() || shape.vertices.size() > 0x10000u) {
            ++frame_native_shape_misses_;
            static bool warned = false;
            if (!warned) {
                warned = true;
                std::fprintf(stderr, "[native-shapes] group %llx reads an array the game never registered\n",
                             static_cast<unsigned long long>(record.key));
            }
            shape.expanded = false;
            return;
        }
        release_record_shape(shape);
        shape.expanded = true;
        shape.versions = versions;
        shape.position_matrix = position_matrix;
        shape.split_built = false;
        shape.split_indices.clear();
    }
    if (!ensure_resident_vertex_buffer()) {
        return;
    }
    const auto units = static_cast<std::uint32_t>(shape.vertices.size());
    if (shape.resident_epoch != resident_vertices_.epoch()) {
        shape.resident_epoch = 0u;
        const std::optional<std::uint32_t> offset = resident_vertices_.allocate(units);
        if (!offset.has_value()) {
            return;
        }
        // Written once while the block belongs to this group, so no in-flight frame sees a partial write.
        std::memcpy(resident_vertex_mapped_ + static_cast<std::size_t>(*offset) * sizeof(GxVertexOut),
                    shape.vertices.data(), static_cast<std::size_t>(units) * sizeof(GxVertexOut));
        renderer_.copy_resident_vertices(static_cast<std::uint64_t>(*offset) * sizeof(GxVertexOut),
                                         static_cast<std::uint64_t>(units) * sizeof(GxVertexOut));
        shape.resident_epoch = resident_vertices_.epoch();
        shape.resident_offset = *offset;
        shape.resident_units = units;
    }

    RecordVertexInputs inputs;
    inputs.matrix_bits = ((geometry.attributes & shapes::kPositionMatrix) != 0u ? 1u : 0u) |
                         (((geometry.attributes / shapes::kTextureMatrix0) & 0xFFu) << 1u);
    inputs.positions = keys[0];
    inputs.colored = (geometry.attributes & shapes::kColor0) != 0u;
    inputs.own_positions = (geometry.attributes & shapes::kOwnPositions) != 0u;
    inputs.colors = keys[2] ^ (keys[3] * 0x9E3779B97F4A7C15ull);
    inputs.curved = shape.curved.get();
    struct RecordScope {
        GxBackend* backend = nullptr;
        bool run = false;
        ~RecordScope() {
            if (run) {
                backend->end_cached_draw_run();
            }
            backend->record_inputs_ = nullptr;
        }
    };
    record_inputs_ = &inputs;
    RecordScope scope{this};
    if (!begin_cached_draw_run(PrimitiveClass::Triangles, 0u, 1u)) {
        return;
    }
    scope.run = true;
    ++frame_native_shape_draws_;
    if (cached_draw_run_scissor_reject_) {
        ++frame_scissor_skips_;
        return;
    }
    if (cached_draw_run_no_pso_) {
        ++stat_draws_no_pso_;
        ++frame_draws_no_pso_;
        return;
    }
    flush_pending_draw_batch();
    cached_draw_run_batch_compatible_known_ = false;

    const bool curved = current_curved_ && curved_split_ && current_flat_pipeline_ != nullptr;
    if (curved && !shape.split_built) {
        shape.split_built = true;
        shape.split_flat_indices = split_curved_triangles(shape.vertices, geometry.indices, shape.split_indices);
    }
    const bool split = curved && !shape.split_indices.empty();
    const std::vector<std::uint16_t>& indices = split ? shape.split_indices : geometry.indices;
    const UploadRing::Allocation ib = renderer_.index_ring().upload_immutable(
        indices.data(), indices.size() * sizeof(std::uint16_t), sizeof(std::uint16_t),
        split ? shape.split_upload_token : shape.index_upload_token);
    const auto first_index = static_cast<std::uint32_t>(ib.offset / sizeof(std::uint16_t));
    const auto index_count = static_cast<std::uint32_t>(indices.size());
    const std::size_t vertex_byte_offset = static_cast<std::size_t>(shape.resident_offset) * sizeof(GxVertexOut);
    ++stat_draws_issued_;
    ++frame_draws_issued_;
    ++frame_resident_vertex_hits_;
    frame_resident_vertex_bytes_ += static_cast<std::uint64_t>(units) * sizeof(GxVertexOut);

    const gpu::VertexView vbv = gpu::vertex_view(resident_vertex_gpu_ + vertex_byte_offset,
                                                 units * static_cast<std::uint32_t>(sizeof(GxVertexOut)),
                                                 static_cast<std::uint32_t>(sizeof(GxVertexOut)));
    const gpu::IndexView ibv = gpu::index_view(
        renderer_.index_ring().gpu_base() + static_cast<std::size_t>(first_index) * sizeof(std::uint16_t),
        index_count * static_cast<std::uint32_t>(sizeof(std::uint16_t)));
    DrawCall call = cached_draw_run_state_call_;
    call.vertex_view = vbv;
    call.index_view = ibv;
    call.index_count = index_count;
    if (split) {
        call.flat_pipeline = current_flat_pipeline_;
        call.flat_index_count = shape.split_flat_indices;
        // The sort's view of this frame: of the triangles after the flat ones, those the hull shader would leave
        // whole draw after the flat ones.
        call.curve_sort = renderer_.sort_curved(
            vbv.BufferLocation, ibv.BufferLocation + shape.split_flat_indices * sizeof(std::uint16_t),
            (index_count - shape.split_flat_indices) / 3u, call.vs_constants, call.matrix_palette,
            (vs_key_.flags & 0x01u) != 0u);
        call.curve_sort_list = renderer_.curve_sort_list();
    }
    pending_draw_batch_.active = true;
    pending_draw_batch_.call = call;
    pending_draw_batch_.rt = begin_rt_batch(call, pending_draw_batch_.rt_info);
    exclude_layer_water(pending_draw_batch_.call, pending_draw_batch_.rt_info, pending_draw_batch_.rt);
    pending_draw_batch_.hidden = rt_hidden_draw();
    pending_draw_batch_.traced = pending_draw_batch_.hidden && (rt_traced_clear_draw() || rt_trace_only_);
    pending_draw_batch_.debake_model = debake_sample_key_;
    pending_draw_batch_.indexed = true;
    pending_draw_batch_.base_vertex = shape.resident_offset;
    pending_draw_batch_.vertex_count = units;
    pending_draw_batch_.vertex_byte_end = vertex_byte_offset + static_cast<std::size_t>(units) * sizeof(GxVertexOut);
    pending_draw_batch_.first_index = first_index;
    pending_draw_batch_.index_count = index_count;
    pending_draw_batch_.draw_count = 1u;
    pending_draw_batch_.resident = true;
    pending_draw_batch_.sealed = true;
}

void GxBackend::draw_record_immediate(const shapes::Immediate& immediate) {
    display_copy_on_output_ = false;
    const auto primitive = static_cast<PrimitiveClass>(immediate.primitive >> 3u);
    const shapes::Geometry& geometry = immediate.geometry;
    const auto count = static_cast<std::uint32_t>(geometry.corners.size());
    shapes::primitive_indices(immediate.primitive, count, immediate_indices_);
    if (immediate_indices_.empty() || count > 0x10000u) {
        return;
    }
    shapes::DrawArrays arrays{};
    for (unsigned i = 0; i < 12u; ++i) {
        arrays[i] = geometry.arrays[i] != 0u ? &immediate.arrays[i] : nullptr;
    }
    RecordVertexInputs inputs;
    inputs.matrix_bits = (geometry.attributes & shapes::kPositionMatrix) != 0u ? 1u : 0u;
    inputs.colored = (geometry.attributes & shapes::kColor0) != 0u;
    inputs.own_positions = true;
    struct RecordScope {
        GxBackend* backend = nullptr;
        bool run = false;
        ~RecordScope() {
            if (run) {
                backend->end_cached_draw_run();
            }
            backend->record_inputs_ = nullptr;
        }
    };
    record_inputs_ = &inputs;
    RecordScope scope{this};
    if (!begin_cached_draw_run(primitive, 0u, 1u)) {
        return;
    }
    scope.run = true;
    if (cached_draw_run_scissor_reject_) {
        ++frame_scissor_skips_;
        return;
    }
    if (cached_draw_run_no_pso_) {
        ++stat_draws_no_pso_;
        ++frame_draws_no_pso_;
        return;
    }
    // Vertices without a position matrix of their own take the current one (as GX's direct vertices did).
    const auto position_matrix = static_cast<std::uint8_t>(state_.cp(cp::kMatrixIndexA) & 0x3Fu);
    if (!shapes::expand(geometry, arrays, position_matrix, nullptr, immediate_vertices_)) {
        return;
    }
    const DrawCall& state_call = cached_draw_run_state_call_;
    // Consecutive draws with the same state share one GPU draw (as GX's direct draws batch): the vertices and indices
    // follow the batch's in the rings, the indices counted from its first vertex.
    bool append = pending_draw_batch_.active && !pending_draw_batch_.resident && pending_draw_batch_.indexed &&
                  pending_draw_batch_.vertex_count + count <= 0x10000u &&
                  draw_calls_batch_compatible(pending_draw_batch_.call, state_call, true, true);
    if (!append) {
        flush_pending_draw_batch();
    }
    cached_draw_run_batch_compatible_known_ = false;
    const UploadRing::Allocation vb =
        renderer_.vertex_ring().allocate(static_cast<std::size_t>(count) * sizeof(GxVertexOut), 16u);
    std::memcpy(vb.cpu, immediate_vertices_.data(), static_cast<std::size_t>(count) * sizeof(GxVertexOut));
    const auto index_count = static_cast<std::uint32_t>(immediate_indices_.size());
    const UploadRing::Allocation ib = renderer_.index_ring().allocate(
        static_cast<std::size_t>(index_count) * sizeof(std::uint16_t), sizeof(std::uint16_t));
    const auto first_index = static_cast<std::uint32_t>(ib.offset / sizeof(std::uint16_t));
    const auto base_vertex = static_cast<std::uint32_t>(vb.offset / sizeof(GxVertexOut));
    if (append && (vb.offset != pending_draw_batch_.vertex_byte_end ||
                   first_index != pending_draw_batch_.first_index + pending_draw_batch_.index_count)) {
        flush_pending_draw_batch();
        append = false;
    }
    const std::uint32_t bias = append ? base_vertex - pending_draw_batch_.base_vertex : 0u;
    auto* indices = reinterpret_cast<std::uint16_t*>(ib.cpu);
    for (std::uint32_t i = 0; i < index_count; ++i) {
        indices[i] = static_cast<std::uint16_t>(immediate_indices_[i] + bias);
    }
    ++stat_draws_issued_;
    ++frame_draws_issued_;
    if (append) {
        pending_draw_batch_.call.index_count += index_count;
        pending_draw_batch_.call.vertex_view.SizeInBytes += count * static_cast<std::uint32_t>(sizeof(GxVertexOut));
        pending_draw_batch_.call.index_view.SizeInBytes += index_count * static_cast<std::uint32_t>(sizeof(std::uint16_t));
        pending_draw_batch_.vertex_count += count;
        pending_draw_batch_.vertex_byte_end = vb.offset + static_cast<std::size_t>(count) * sizeof(GxVertexOut);
        pending_draw_batch_.index_count += index_count;
        ++pending_draw_batch_.draw_count;
        return;
    }
    const gpu::VertexView vbv =
        gpu::vertex_view(renderer_.vertex_ring().gpu_base() + vb.offset,
                         count * static_cast<std::uint32_t>(sizeof(GxVertexOut)),
                         static_cast<std::uint32_t>(sizeof(GxVertexOut)));
    const gpu::IndexView ibv = gpu::index_view(
        renderer_.index_ring().gpu_base() + static_cast<std::size_t>(first_index) * sizeof(std::uint16_t),
        index_count * static_cast<std::uint32_t>(sizeof(std::uint16_t)));
    DrawCall call = state_call;
    call.vertex_view = vbv;
    call.index_view = ibv;
    call.index_count = index_count;
    pending_draw_batch_.active = true;
    pending_draw_batch_.call = call;
    pending_draw_batch_.rt = begin_rt_batch(call, pending_draw_batch_.rt_info);
    exclude_layer_water(pending_draw_batch_.call, pending_draw_batch_.rt_info, pending_draw_batch_.rt);
    pending_draw_batch_.hidden = rt_hidden_draw();
    pending_draw_batch_.traced = pending_draw_batch_.hidden && (rt_traced_clear_draw() || rt_trace_only_);
    pending_draw_batch_.debake_model = debake_sample_key_;
    pending_draw_batch_.indexed = true;
    pending_draw_batch_.base_vertex = base_vertex;
    pending_draw_batch_.vertex_count = count;
    pending_draw_batch_.vertex_byte_end = vb.offset + static_cast<std::size_t>(count) * sizeof(GxVertexOut);
    pending_draw_batch_.first_index = first_index;
    pending_draw_batch_.index_count = index_count;
    pending_draw_batch_.draw_count = 1u;
    pending_draw_batch_.resident = false;
    pending_draw_batch_.sealed = false;
}

void GxBackend::on_draw(
    PrimitiveClass primitive,
    std::uint8_t vtxfmt,
    FifoCursor& cursor)
{
    display_copy_on_output_ = false;
    if (cached_draw_run_active_) {
        on_cached_draw_run_draw(primitive, vtxfmt, cursor);
        return;
    }
    if (std::exchange(clear_quad_pending_, false) && clear_quads_ && primitive == PrimitiveClass::Quads &&
        draw_clear_quad(vtxfmt, cursor)) {
        return;
    }

    const bool time_detail = frame_time_detail_;
    gpu::Topology topology{};
    gpu::TopologyType topology_type{};
    gx_topology_for_primitive(primitive, topology, topology_type);

    const EfbScissor efb_sc = state_.efb_scissor();
    const ScissorRect sc = efb_sc.rect;
    const bool scissor_reject =
        sc.x1 < sc.x0 || sc.y1 < sc.y0 ||
        sc.x1 < 0 || sc.y1 < 0 ||
        sc.x0 >= static_cast<std::int32_t>(kEfbWidth) ||
        sc.y0 >= static_cast<std::int32_t>(kEfbHeight);
    const bool cull_reject =
        cull_all_suppresses_primitive(state_.gen_mode().cull, primitive);
    if (cull_reject || scissor_reject) {
        const VertexLayoutCacheEntry& layout = cached_vertex_layout(vtxfmt);
        const std::uint16_t vertex_count = cursor.read_u16();
        cursor.take(
            layout.source_size * static_cast<std::size_t>(vertex_count));
        ++frame_scissor_skips_;
        return;
    }

    cursor.require(2);
    const std::uint16_t source_vertex_count =
        static_cast<std::uint16_t>(
            (static_cast<std::uint16_t>(cursor.data[cursor.offset]) << 8) |
            static_cast<std::uint16_t>(cursor.data[cursor.offset + 1]));
    // The FIFO position of the vertex data (top-level stream draws only:
    // display-list cursors address list memory).
    const bool stream_draw = cursor.recover_truncation && cursor.base_offset == 0u;
    const std::size_t vertex_stream_offset = parse_span_base_ + cursor.offset + 2u;

    gpu::Pipeline* pso = nullptr;
    {
        ScopedFlushTiming flush_timing{
            frame_flush_timed_calls_,
            frame_flush_total_us_,
            frame_flush_timed_samples_};
        pso = flush_draw_state(topology_type, vtxfmt);
    }
    if (pso == nullptr) {
        flush_pending_draw_batch();
        // No pipeline ready: the draw is skipped, but its vertex data is still
        // consumed so the cursor stays at a command boundary.
        ++stat_draws_no_pso_;
        ++frame_draws_no_pso_;
        const auto vertex_start = time_detail
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        const VertexLayoutCacheEntry& layout = cached_vertex_layout(vtxfmt);
        vertex_loader_.load_with_layout(
            cursor, primitive, state_, layout.desc,
            layout.source_size, frame_memory_, renderer_.vertex_ring(),
            renderer_.index_ring());
        if (time_detail) {
            frame_vertex_load_us_ +=
                elapsed_us(vertex_start, std::chrono::steady_clock::now());
        }
        return;
    }

    const gpu::Rect scissor_rect = gpu::rect(
        sc.x0 < 0 ? 0 : sc.x0, sc.y0 < 0 ? 0 : sc.y0, sc.x1 + 1, sc.y1 + 1);

    DrawCall state_call{};
    state_call.pipeline       = pso;
    state_call.vs_constants   = current_vs_constants_;
    state_call.ps_constants   = current_ps_constants_;
    state_call.texture_table  = current_texture_table_;
    state_call.sampler_table  = (current_sampler_table_.ptr != 0)
        ? current_sampler_table_
        : renderer_.default_sampler_table();
    state_call.matrix_palette = draw_matrix_palette_;
    state_call.aux_target     = current_pipeline_aux_;
    state_call.replay_record  = current_replay_record_;
    state_call.ps_record      = current_ps_record_;
    state_call.topology       = draw_topology(topology);
    state_call.scissor        = scissor_rect;
    for (unsigned i = 0; i < 6; ++i) {
        state_call.xf_viewport[i] = std::bit_cast<float>(
            state_.xf(static_cast<std::uint16_t>(xf::kViewportBase + i)));
    }
    // Viewport origin in EFB pixels: screen position minus the scissor offset.
    state_call.xf_viewport[3] -= static_cast<float>(efb_sc.offset.x);
    state_call.xf_viewport[4] -= static_cast<float>(efb_sc.offset.y);
    state_call.has_viewport =
        state_call.xf_viewport[0] > 0.0f &&
        state_call.xf_viewport[1] < 0.0f;

    const bool primitive_indexed =
        primitive != PrimitiveClass::Triangles;
    bool append_to_pending =
        pending_draw_batch_.active && !pending_draw_batch_.resident &&
        draw_calls_batch_compatible(
            pending_draw_batch_.call,
            state_call,
            pending_draw_batch_.indexed,
            primitive_indexed);
    if (append_to_pending && !primitive_indexed &&
        primitive == PrimitiveClass::Triangles &&
        (pending_draw_batch_.vertex_count % 3u != 0u || source_vertex_count % 3u != 0u)) {
        append_to_pending = false;
    }
    if (append_to_pending && primitive_indexed &&
        pending_draw_batch_.vertex_count +
            static_cast<std::uint32_t>(source_vertex_count) >
            std::numeric_limits<std::uint16_t>::max()) {
        flush_pending_draw_batch();
        append_to_pending = false;
    }
    if (pending_draw_batch_.active && !append_to_pending) {
        flush_pending_draw_batch();
    }

    const auto vertex_start = time_detail
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    const VertexLayoutCacheEntry& layout = cached_vertex_layout(vtxfmt);
    const LoadedPrimitive prim = vertex_loader_.load_with_layout(
        cursor, primitive, state_, layout.desc, layout.source_size,
        frame_memory_, renderer_.vertex_ring(), renderer_.index_ring(),
        append_to_pending && primitive_indexed
            ? pending_draw_batch_.base_vertex
            : 0u,
        append_to_pending && primitive_indexed);
    if (time_detail) {
        frame_vertex_load_us_ +=
            elapsed_us(vertex_start, std::chrono::steady_clock::now());
    }

    if (prim.index_count == 0) {
        if (pending_draw_batch_.active) {
            flush_pending_draw_batch();
        }
        return;
    }
    ++stat_draws_issued_;
    ++frame_draws_issued_;
    // Stripe particles drawn by this primitive: each mark is the FIFO position
    // of a particle's two vertices.
    draw_stripe_vertices_.clear();
    if (stream_draw && parse_stripe_next_ < parse_stripes_.size() && vertex_stream_offset >= parse_span_prefix_)
        [[unlikely]] {
        const std::size_t start = vertex_stream_offset - parse_span_prefix_;
        const std::size_t end = start + layout.source_size * static_cast<std::size_t>(source_vertex_count);
        const std::uint32_t base = append_to_pending ? pending_draw_batch_.vertex_count : 0u;
        while (parse_stripe_next_ < parse_stripes_.size() && parse_stripes_[parse_stripe_next_].offset < start) {
            ++parse_stripe_next_;
        }
        while (parse_stripe_next_ < parse_stripes_.size() && parse_stripes_[parse_stripe_next_].offset < end &&
               layout.source_size != 0u) {
            const StripeParticleMark& mark = parse_stripes_[parse_stripe_next_++];
            const auto local = static_cast<std::uint32_t>((mark.offset - start) / layout.source_size);
            for (std::uint8_t side = 0; side < 2u && local + side < prim.vertex_count; ++side) {
                draw_stripe_vertices_.push_back({base + local + side, mark.particle, mark.age_lifetime, mark.strip,
                                                 side});
            }
        }
    }


    // Vertex and index views at the primitive's offsets in the (persistently mapped) rings.
    const gpu::VertexView vbv = gpu::vertex_view(
        renderer_.vertex_ring().gpu_base() + prim.vertex_byte_offset,
        prim.vertex_count * static_cast<std::uint32_t>(sizeof(GxVertexOut)), static_cast<std::uint32_t>(sizeof(GxVertexOut)));

    gpu::IndexView ibv{};
    if (prim.indexed) {
        ibv = gpu::index_view(
            renderer_.index_ring().gpu_base() + static_cast<std::size_t>(prim.first_index) * sizeof(std::uint16_t),
            prim.index_count * static_cast<std::uint32_t>(sizeof(std::uint16_t)));
    }

    DrawCall call = state_call;
    call.vertex_view    = vbv;
    call.index_view     = ibv;
    call.index_count    = prim.index_count;


    const std::size_t vertex_byte_end =
        prim.vertex_byte_offset +
        static_cast<std::size_t>(prim.vertex_count) * sizeof(GxVertexOut);
    const bool compatible_after_load =
        pending_draw_batch_.active &&
        (prim.indexed || primitive != PrimitiveClass::Triangles ||
            (pending_draw_batch_.vertex_count % 3u == 0u && prim.vertex_count % 3u == 0u)) &&
        draw_calls_batch_compatible(
            pending_draw_batch_.call,
            call,
            pending_draw_batch_.indexed,
            prim.indexed);
    if (append_to_pending && !compatible_after_load) {
        throw GxFatalError(
            "GxBackend: draw batch compatibility changed after index rebase",
            cursor.command_offset,
            0);
    }
    if (compatible_after_load) {
        const bool vertex_contiguous =
            prim.vertex_byte_offset == pending_draw_batch_.vertex_byte_end;
        const bool index_contiguous =
            !prim.indexed ||
            prim.first_index ==
                pending_draw_batch_.first_index +
                pending_draw_batch_.index_count;
        if (!vertex_contiguous || !index_contiguous) {
            throw GxFatalError(
                "GxBackend: draw batch upload rings were not contiguous",
                cursor.command_offset,
                0);
        }
        pending_draw_batch_.call.index_count += prim.index_count;
        pending_draw_batch_.call.vertex_view.SizeInBytes +=
            prim.vertex_count * static_cast<std::uint32_t>(sizeof(GxVertexOut));
        if (prim.indexed) {
            pending_draw_batch_.call.index_view.SizeInBytes +=
                prim.index_count * static_cast<std::uint32_t>(sizeof(std::uint16_t));
        }
        pending_draw_batch_.vertex_count += prim.vertex_count;
        pending_draw_batch_.vertex_byte_end = vertex_byte_end;
        pending_draw_batch_.index_count += prim.index_count;
        ++pending_draw_batch_.draw_count;
    } else {
        flush_pending_draw_batch();
        pending_draw_batch_.active = true;
        pending_draw_batch_.call = call;
        pending_draw_batch_.rt = begin_rt_batch(call, pending_draw_batch_.rt_info);
        exclude_layer_water(pending_draw_batch_.call, pending_draw_batch_.rt_info, pending_draw_batch_.rt);
        pending_draw_batch_.hidden = rt_hidden_draw();
        pending_draw_batch_.traced = pending_draw_batch_.hidden && (rt_traced_clear_draw() || rt_trace_only_);
        pending_draw_batch_.debake_model = debake_sample_key_;
        pending_draw_batch_.indexed = prim.indexed;
        pending_draw_batch_.base_vertex = prim.base_vertex;
        pending_draw_batch_.vertex_count = prim.vertex_count;
        pending_draw_batch_.vertex_byte_end = vertex_byte_end;
        pending_draw_batch_.first_index = prim.first_index;
        pending_draw_batch_.index_count = prim.index_count;
        pending_draw_batch_.draw_count = 1;
    }
    if (!draw_stripe_vertices_.empty()) [[unlikely]] {
        pending_stripe_vertices_.insert(pending_stripe_vertices_.end(), draw_stripe_vertices_.begin(),
                                        draw_stripe_vertices_.end());
    }
}

void GxBackend::on_efb_copy(std::uint32_t exec_command) {
    flush_pending_draw_batch();
    const EfbCopyParams params = state_.efb_copy(exec_command);
    if (params.copy_to_xfb) {
        galaxy::timeline::record(galaxy::timeline::Event::XfbCopy, params.dest_addr);
        xfb_copied_in_chunk_ = xfb_copied_in_chunk_ || is_vi_named_xfb(params.dest_addr);
    }
    const bool time_copy = frame_time_detail_;
    const auto copy_start = time_copy
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};


    if (params.copy_to_xfb) {
        XfbTexture* xfb = efb_copies_.acquire_xfb(params, frame_index_);
        if (xfb != nullptr) {
            end_side_layer();
            mirror_opened_scene_ = false;
            // The frame ends: the next is recorded as screen-only from its first draw when this one drew no scene.
            screen_run_ = !frame_scene_seen_;
            frame_scene_seen_ = false;
            screen_frame_open_ = false;
            if (renderer_.can_hand_over_display_copy(params, *xfb)) {
                // Native presentation: the XFB takes the EFB's colour target, and the EFB continues on a texture of
                // its size the XFB pool released.
                const auto scale = renderer_.efb_scale();
                efb_copies_.adopt_picture(
                    *xfb, renderer_.hand_over_efb_color(efb_copies_.take_idle_texture(
                              static_cast<std::uint16_t>(kEfbWidth * scale),
                              static_cast<std::uint16_t>(kEfbHeight * scale))));
            } else {
                renderer_.copy_efb_to_xfb(params, *xfb);
            }
            // The display copy ends the 2D layer: later work and the next
            // frame use the render-resolution EFB again.
            display_copy_on_output_ = renderer_.output_efb_active() && !params.clear;
            if (renderer_.output_efb_active()) {
                renderer_.end_output_efb();
                efb_copies_.set_efb_scale(renderer_.efb_scale());
                frame_bindings_dirty_ = true;
            }
            static const bool trace_layout=read_env_u64("GALAXY_MONITOR_DISPLAY_LAYOUT",0u)!=0u;
            if (trace_layout) {
                const std::array<unsigned,7> current{params.src_x,params.src_y,params.src_width,params.src_height,
                    xfb->width,xfb->height,renderer_.efb_scale()};
                static std::array<std::array<unsigned,7>,8> seen{};
                static unsigned count{};
                if (count<seen.size() && std::find(seen.begin(),seen.begin()+count,current)==seen.begin()+count) {
                    seen[count++]=current;
                    const auto scale=renderer_.efb_scale();
                    std::cerr<<"[display-render-layout] unique="<<count<<" backing-efb="<<640u*scale<<'x'<<528u*scale
                        <<" guest-copy-rect="<<params.src_x<<','<<params.src_y<<','<<params.src_width<<','<<params.src_height
                        <<" scene-copy-sampled="<<params.src_width*scale<<'x'<<params.src_height*scale
                        <<" scaled-xfb="<<xfb->width<<'x'<<xfb->height<<" guest-stride="<<params.dest_stride
                        <<" internal-scale="<<scale<<" xfb-serial-before-publication="<<xfb->copy_serial<<'\n';
                }
            }
            xfb->pointer_response=render_pointer_response_;
            xfb->prerecorded_movie=render_movie_content_;
            xfb->safety_surround=render_safety_surround_;
            efb_copies_.mark_xfb_copied(*xfb, frame_index_);
            ++stat_xfb_copies_;
            ++frame_xfb_copies_;
            record_monotonic_cadence_sample(xfb_copy_production_cadence_);
        }
    } else {
        ++frame_tex_copies_;
        // The copy overwrites its destination: an image handed over there (memory the game freed and reused, as
        // the Mii library's face masks reuse its part textures' buffers) no longer describes those bytes. The
        // extent is the destination's tile rows (4 lines each, the shortest tile) over the source height.
        const TextureCache::GuestRange copied{
            params.dest_addr, params.dest_stride * ((static_cast<std::uint32_t>(params.src_height) + 3u) / 4u)};
        texture_cache_.drop_registered_images(std::span<const TextureCache::GuestRange>(&copied, 1u));
        // Depth-source copy when the PE pixel format is Z24 at copy time
        // (Dolphin BPStructs: is_depth_copy = zcontrol.pixel_format == Z24).
        const bool is_depth_copy = state_.pe_control().pixel_format == 3u;
        // A colour copy right after a display copy takes the picture it showed (with the 2D layer), as the
        // console's single EFB holds it, not the scene the output-resolution 2D layer was drawn over.
        const bool from_display = display_copy_on_output_ && !is_depth_copy && !params.clear;
        display_copy_on_output_ = display_copy_on_output_ && !params.clear;
        const bool alias_updated = from_display
            ? renderer_.copy_output_efb_to_texture(params, texture_cache_)
            : renderer_.copy_efb_to_texture(params, texture_cache_, is_depth_copy);
        if (alias_updated) {
            ++frame_efb_alias_updates_;
            // One guest address can alternate between format-keyed GPU resources, and the game need not
            // rewrite the BP texture registers after a copy, so the next draw refreshes every frame table
            // that referenced this alias. A reused texture is updated in place; its SRV stays valid.
            mark_texture_binding_alias_dirty(params);
            texture_bindings_dirty_ = true;
        } else {
            ++frame_efb_alias_noops_;
        }
    }

    // GX clear-on-copy: the copy command's clear bit wipes the copy's source rect with the latched
    // copy-clear registers, honoring the PE write masks (Dolphin BPStructs BPMEM_TRIGGER_EFB_COPY:
    // ClearScreen(srcRect) with colorEnable = blendmode.colorupdate, alphaEnable =
    // blendmode.alphaupdate, zEnable = zmode.updateenable); later copies of the frame still read the rest.
    if (params.clear) {
        const BlendMode bm = state_.blend_mode();
        const ZMode zm = state_.z_mode();
        renderer_.clear_efb(
            params.clear_color_ar,
            params.clear_color_gb,
            params.clear_z24,
            bm.color_update, bm.alpha_update, zm.update_enable,
            params.src_x, params.src_y,
            params.src_width, params.src_height,
            state_.pe_control().pixel_format);
    }
    const std::uint64_t copy_us = time_copy
        ? elapsed_us(copy_start, std::chrono::steady_clock::now())
        : 0u;
    if (time_copy) {
        frame_efb_copy_us_ += copy_us;
    }
    if (trace_gx_stalls_enabled() &&
        copy_us >= trace_gx_stall_threshold_us()) {
        std::cerr << "[gx-efb-copy-timing] frame=" << frame_index_
                  << " kind=" << (params.copy_to_xfb ? "xfb" : "tex")
                  << " us=" << copy_us
                  << " src=" << params.src_width << 'x' << params.src_height
                  << " dst=0x" << std::hex << params.dest_addr << std::dec
                  << " clear=" << (params.clear ? 1 : 0) << '\n';
    }
}

void GxBackend::on_pe_finish() {
    flush_pending_draw_batch();
    if (!frame_pe_callbacks_enabled_) {
        return;
    }
    if (frame_pe_events_ != nullptr) {
        if (frame_services_ == nullptr ||
            frame_services_->gx_pe_finish == nullptr) {
            throw std::runtime_error(
                "GX PE_FINISH requires a native gx_pe_finish service");
        }
        frame_pe_events_->push_back(PeEvent{PeEvent::Kind::Finish, 0, false});
        return;
    }
    if (frame_services_ == nullptr ||
        frame_services_->gx_pe_finish == nullptr) {
        throw std::runtime_error(
            "GX PE_FINISH requires a native gx_pe_finish service");
    }
    frame_services_->gx_pe_finish(frame_services_->user);
    // The optional log is capped: these callbacks fire on every GXDrawDone.
    static int s_pe_done_logs = 0;
    if (frame_services_ != nullptr && frame_services_->log != nullptr &&
        s_pe_done_logs < 4) {
        ++s_pe_done_logs;
        frame_services_->log(
            frame_services_->user,
            galaxy::GxLogLevel::Trace,
            "[GxBackend] PE_DONE (GXDrawDone)");
    }
}

void GxBackend::record_async_peek_fields_at_token(std::uint16_t token) {
    for (std::size_t index = 0; index < async_peek_.size(); ++index) {
        AsyncPeekFieldState& state = async_peek_[index];
        const std::uint32_t key = state.key.load(std::memory_order_acquire);
        if (key == kAsyncPeekUnbound || (key & 0xFFFFu) != token ||
            !state.requested.load(std::memory_order_acquire) || renderer_.async_peek_field_pending(index)) {
            continue;
        }
        // Never the published slot: a reader may be converting its pixel.
        const std::uint64_t published = state.published.load(std::memory_order_relaxed);
        const int slot = (published & 0xFFFFFFFFu) == 0u
            ? 0 : static_cast<int>(published & 0xFFFFFFFFu) % 3;
        const std::uint32_t generation = state.generation.load(std::memory_order_acquire);
        if (renderer_.record_async_peek_field_in_frame(
                index, async_peek_key_kind(key), static_cast<std::size_t>(slot))) {
            state.requested.store(false, std::memory_order_relaxed);
            if (state.rgba[0] == nullptr) {
                for (std::size_t i = 0; i < state.rgba.size(); ++i) {
                    state.rgba[i] = renderer_.async_peek_field_rgba(index, i);
                }
            }
            state.pending_slot = slot;
            state.pending_generation = generation;
        }
    }
}

void GxBackend::on_pe_token(std::uint16_t token, bool interrupt) {
    flush_pending_draw_batch();
    if (token != 0u) {
        record_async_peek_fields_at_token(token);
    }
    if (!frame_pe_callbacks_enabled_) {
        return;
    }
    if (frame_pe_events_ != nullptr) {
        if (frame_services_ == nullptr ||
            frame_services_->gx_pe_token == nullptr) {
            throw std::runtime_error(
                "GX PE_TOKEN requires a native gx_pe_token service");
        }
        frame_pe_events_->push_back(
            PeEvent{PeEvent::Kind::Token, token, interrupt});
        return;
    }
    if (frame_services_ == nullptr ||
        frame_services_->gx_pe_token == nullptr) {
        throw std::runtime_error(
            "GX PE_TOKEN requires a native gx_pe_token service");
    }
    frame_services_->gx_pe_token(
        frame_services_->user,
        token,
        interrupt);
    static int s_pe_token_logs = 0;
    if (frame_services_ != nullptr && frame_services_->log != nullptr &&
        s_pe_token_logs < 4) {
        ++s_pe_token_logs;
        char buf[64]{};
        std::snprintf(
            buf, sizeof(buf),
            "[GxBackend] PE_TOKEN token=0x%04X int=%d",
            static_cast<unsigned>(token),
            interrupt ? 1 : 0);
        frame_services_->log(
            frame_services_->user,
            galaxy::GxLogLevel::Trace,
            buf);
    }
}

void GxBackend::on_invalidate_textures() {
    flush_pending_draw_batch();
    texture_cache_.invalidate_all();
    texture_handle_cache_.clear();
    clear_texture_binding_table_cache();
    texture_bindings_dirty_ = true;
    current_texture_binding_key_valid_ = false;
}

void GxBackend::on_tlut_load() {
    const TlutTransfer transfer = decode_wii_tlut_transfer(
        state_.bp(bp::kTlutSrcAddr), state_.bp(bp::kTlutDest));
    if (transfer.byte_count == 0u) {
        return;
    }
    flush_pending_draw_batch();
    // BP 0x64 (source) is latched by GxState; BP 0x65 (destination) triggers the load into TMEM, which
    // palettized (C4/C8/C14X2) textures decode against.
    if (frame_memory_ == nullptr) {
        return;
    }
    if (!texture_cache_.load_tlut(
        state_.bp(bp::kTlutSrcAddr),
        state_.bp(bp::kTlutDest),
        frame_memory_)) {
        return;
    }
    texture_handle_cache_.clear();
    clear_texture_binding_table_cache();
    texture_bindings_dirty_ = true;
    current_texture_binding_key_valid_ = false;
}

void GxBackend::on_invalidate_vertex_cache() {
    flush_pending_draw_batch();
    // GXInvalidateVtxCache invalidates the hardware vertex cache, not guest array memory. Decoded
    // vertices are already invalidated by guest writes and checked byte for byte before each hit.
}

void boot_step(const char* step) noexcept {
    static std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    std::fprintf(stderr, "[boot] renderer %s %.0f ms\n", step,
                 std::chrono::duration<double, std::milli>(now - last).count());
    last = now;
}

unsigned layer_2d_scale() noexcept {
    return s_layer_2d_scale.load(std::memory_order_relaxed);
}

unsigned scene_display_scale() noexcept {
    return s_scene_display_scale.load(std::memory_order_relaxed);
}

}  // namespace galaxy::gx
