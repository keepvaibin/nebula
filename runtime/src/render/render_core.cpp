// RenderCore: takes the game's records and drives the renderer.
// render_api.cpp exposes it through the galaxy::render API in render_api.h.

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#pragma warning(push, 0)
#include "galaxy/portable.h"
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
#include "galaxy/loss_report.h"
#include "galaxy/run_options.h"
#include "galaxy/platform_time.h"
#include "galaxy/platform_paths.h"
#include "galaxy/perf_hint.h"

#include "galaxy/render/present_clock.h"
#include "galaxy/portable.h"
#include "galaxy/render/render_core.h"
#include "galaxy/render/native_shaders.h"
#include "galaxy/host_env.h"

#include "galaxy/frame_cadence_diagnostics.h"
#include "galaxy/render/line_point_params.h"
#include "galaxy/render/rt_materials.h"
#include "galaxy/render/curved_surfaces.h"
#include "galaxy/render/draw_constants.h"
#include "galaxy/experimental_ultrawide_aspect.h"
#include "galaxy/content_viewport.h"
#include "galaxy/render/render_config.h"
#include "galaxy/windows_power.h"
#include "galaxy/frame_timeline.h"
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
#include <fstream>
#include <set>
#include <tuple>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace galaxy::render {

std::uint32_t pack_sampler_key(
    const TextureSampler& mode,
    bool generated_mips,
    std::uint8_t mip_levels,
    std::uint8_t lod_shift_x16,
    bool arbitrary,
    std::uint8_t mip_lines_log2_x32,
    int scene_bias_x32) noexcept {
    std::uint32_t min_filter =
        min_filter_code(mode) & 0x7u;
    std::uint32_t min_lod =
        static_cast<std::uint32_t>(lod_x16(mode.min_lod)) & 0xFFu;
    std::uint32_t max_lod =
        static_cast<std::uint32_t>(lod_x16(mode.max_lod)) & 0xFFu;
    const bool source_mips = (min_filter & 0x3u) != 0u;
    bool full_chain = false;
    if (generated_mips && mip_levels > 1u &&
        min_filter == 4u &&
        mode.mag_linear) {
        min_filter =
            6u;
        max_lod = std::min<std::uint32_t>(
            0xFFu,
            static_cast<std::uint32_t>(mip_levels - 1u) * 16u);
        full_chain = true;
    }
    // The levels only a texture with levels has: a scene rendered under the output reads them for the output's pixels.
    const int authored_bias_x32 = static_cast<int>(static_cast<std::int8_t>(lod_bias_x32(mode.lod_bias))) +
                                  (source_mips || full_chain ? scene_bias_x32 : 0);
    std::uint32_t lod_bias = static_cast<std::uint32_t>(static_cast<std::uint8_t>(
        static_cast<std::int8_t>(std::clamp(authored_bias_x32, -128, 127))));
    if (arbitrary && source_mips) {
        const int bias = authored_bias_x32 +
                         static_cast<int>(mip_lines_log2_x32) - 2 * static_cast<int>(lod_shift_x16);
        lod_bias = static_cast<std::uint32_t>(static_cast<std::uint8_t>(
            static_cast<std::int8_t>(std::clamp(bias, -128, 127))));
        max_lod = 0xFFu;
        min_filter = (min_filter & 0x4u) | 0x3u;
    } else if (lod_shift_x16 != 0u) {
        if (source_mips) {
            min_lod = min_lod == 0u ? 0u : std::min<std::uint32_t>(0xFFu, min_lod + lod_shift_x16);
            max_lod = std::min<std::uint32_t>(0xFFu, max_lod + lod_shift_x16);
        } else if (!full_chain) {
            min_filter = min_filter >= 4u ? 6u : 1u;  // between levels if linear, else the nearest level
            min_lod = 0u;
            max_lod = lod_shift_x16;
        }
    }
    return
          (static_cast<std::uint32_t>(mode.wrap_s) & 0x3u)
        | ((static_cast<std::uint32_t>(mode.wrap_t) & 0x3u) << 2)
        | (((mode.mag_linear ? 1u : 0u) & 0x1u) << 4)
        | (min_filter << 5)
        | (lod_bias << 8)
        | (min_lod << 16)
        | (max_lod << 24);
}

void boot_step(const char* step) noexcept;

namespace {


std::atomic<unsigned> s_layer_2d_lines{0u};

// Test aid for runs without a display: every Nth swap-chain present is read
// back and written to GALAXY_PRESENT_SNAPSHOT_DIR\present_<n>.ppm, limited to
// presents GALAXY_PRESENT_SNAPSHOT_FIRST..GALAXY_PRESENT_SNAPSHOT_LAST.
std::uint64_t g_snapshot_presents = 0u;

std::string present_snapshot_path() {
    static const std::uint64_t every = ::galaxy::run_option_u64(::galaxy::run_options().present_snapshot_every, 0u);
    static const std::uint64_t first = ::galaxy::run_option_u64(::galaxy::run_options().present_snapshot_first, 0u);
    static const std::uint64_t last = ::galaxy::run_option_u64(::galaxy::run_options().present_snapshot_last, std::numeric_limits<std::uint64_t>::max());
    static const std::string directory = [] {
        char value[1024]{};
        std::size_t length = 0;
        return ::galaxy::run_option_read(&length, value, sizeof(value), ::galaxy::run_options().present_snapshot_dir) == 0 && length > 1u
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

std::uint64_t read_env_u64(const char* name, std::uint64_t fallback) {
    char value[32]{};
    std::size_t length = 0;
    if (::galaxy::env_read(&length, value, sizeof(value), name) != 0 || length == 0) {
        return fallback;
    }
    char* end = nullptr;
    const auto parsed = std::strtoull(value, &end, 10);
    return end == value ? fallback : parsed;
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


// A draw's facts the frame interpolation needs (InterpScene::spatial_texgens, light_masks), from its drawing state:
// a regular texgen from position, normal or a binormal (rows 0, 1, 3, 4) maps space; lit channels and emboss texgens
// name their lights.
[[nodiscard]] std::pair<std::uint8_t, std::uint8_t> interp_draw_facts(const NativeDrawState& s) noexcept {
    std::uint32_t spatial = 0u;
    std::uint32_t lights = 0u;
    for (unsigned i = 0; i < s.gen.num_texgens && i < 8u; ++i) {
        const TexGen& g = s.texgen[i];
        if (g.type == TexGenType::Regular &&
            (g.source_row == 0u || g.source_row == 1u || g.source_row == 3u || g.source_row == 4u)) {
            spatial |= 1u << i;
        }
        if (g.type == TexGenType::EmbossMap) {
            lights |= 1u << (g.emboss_light & 7u);
        }
    }
    for (unsigned ch = 0; ch < s.gen.num_color_chans && ch < 2u; ++ch) {
        for (const ChannelControl& control : {s.channel[ch], s.channel[2u + ch]}) {
            if (control.lighting_enable) {
                lights |= control.light_mask;
            }
        }
    }
    return {static_cast<std::uint8_t>(spatial), static_cast<std::uint8_t>(lights)};
}

[[nodiscard]] bool snapshot_should_generate_native_mips(
    const TextureInfo& image,
    const TextureSampler& mode) {
    if ((min_filter_code(mode) & 0x3u) != 0u ||
        min_filter_code(mode) != 4u ||
        !mode.mag_linear ||
        (image.width < 64u && image.height < 64u) ||
        !image.colour) {
        return false;
    }
    return get_render_config().enhanced_mipmaps;
}

// The vblanks per present slot for a rate on a display of `refresh` hertz: a whole number, the rate kept above 60.
std::int64_t vblanks_per_slot(double refresh, double rate) {
    std::int64_t slot = std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil(refresh / rate - 0.02)));
    if (refresh / static_cast<double>(slot) < 61.0) {
        slot = std::max<std::int64_t>(1, static_cast<std::int64_t>(refresh / 61.0));
    }
    return slot;
}

// Adds its lifetime to the game thread's time blocked on the render thread (RenderCore::game_wait_qpc_).
struct GameWaitTimer {
    std::atomic<std::uint64_t>& total;
    const std::int64_t start = galaxy::qpc_now();
    ~GameWaitTimer() {
        total.fetch_add(static_cast<std::uint64_t>(galaxy::qpc_now() - start), std::memory_order_relaxed);
    }
};

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

void topology_for_primitive(gpu::Topology& topology, gpu::TopologyType& topology_type) {
    topology = gpu::kTopologyTriangleList;
    topology_type = gpu::kTopologyTypeTriangle;
}

// The kinds of a draw's matrices (draw_matrices.h), in the order their blocks are appended (RenderCore::flush).
constexpr std::array<DrawMatrices::Kind, 5> kMatrixKinds{DrawMatrices::kPositions, DrawMatrices::kTextures,
                                                         DrawMatrices::kNormals, DrawMatrices::kPosts,
                                                         DrawMatrices::kLights};
constexpr std::size_t kLightKind = 4u;
constexpr std::uint32_t kAllMatrixKinds = DrawMatrices::kPositions | DrawMatrices::kTextures | DrawMatrices::kNormals |
                                          DrawMatrices::kPosts | DrawMatrices::kLights;
// A kind's first row in the draw_rows numbering.
constexpr std::uint32_t matrix_kind_first_row(DrawMatrices::Kind kind) noexcept {
    return kind == DrawMatrices::kPositions ? draw_rows::kPosition
           : kind == DrawMatrices::kTextures ? draw_rows::kTexture
           : kind == DrawMatrices::kNormals  ? draw_rows::kNormal
           : kind == DrawMatrices::kPosts    ? draw_rows::kPost
                                             : draw_rows::kLight;
}

}  // anonymous namespace

void RenderCore::reset_session_state() {
    // While no producer or render thread runs.
    matrices_ = DrawMatrices{};
    draw_state_ = NativeDrawState{};

    // Runs before the renderer initializes, so nothing still reads the buffer.
    release_resident_vertex_buffer();

    frame_services_ = nullptr;
    pending_draw_batch_ = {};
    cached_draw_run_active_ = false;
    cached_draw_run_scissor_reject_ = false;
    cached_draw_run_primitive_indexed_ = false;
    cached_draw_run_no_pso_ = false;
    cached_draw_run_batch_compatible_known_ = false;
    cached_draw_run_primitive_ = Primitive::Points;
    cached_draw_run_vtxfmt_ = 0u;
    cached_draw_run_state_call_ = {};

    // Derived state below holds descriptors and resources of the previous renderer session.
    render_state_key_ = {};
    current_vs_hash_ = 0u;
    current_ps_hash_ = 0u;
    shader_key_hashes_valid_ = false;
    current_pso_key_ = {};
    current_pso_key_valid_ = false;
    current_pipeline_ = nullptr;
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
    // The ring segment is reused, so no block is valid until the next draw appends them all.
    matrix_blocks_valid_ = false;
    frame_bindings_dirty_ = true;
    texture_bindings_dirty_ = true;
    current_texture_map_mask_ = 0u;
    current_texture_map_mask_valid_ = false;
    native_material_ = nullptr;
    current_texture_binding_key_ = {};
    current_texture_binding_key_mask_ = 0u;
    // Also clears current_texture_binding_key_valid_, so no key outlives its tables.
    clear_texture_binding_table_cache();
    texture_handle_cache_.clear();

    // Frame stamps, seeding and display picture addresses restart with the new scene target/display picture resources.
    frame_index_ = 0u;
    scene_seeded_ = false;
    present_epoch_ = 0u;
    display_serial_accounting_ = {};
    reset_monotonic_cadence(display_copy_production_cadence_);
    reset_monotonic_cadence(display_first_serial_present_cadence_);
    last_presented_display_stamp_.store(
        ~std::uint64_t{0}, std::memory_order_relaxed);
    last_presented_display_serial_.store(
        ~std::uint64_t{0}, std::memory_order_relaxed);
    last_presented_slot_.store(0u, std::memory_order_relaxed);
}

bool RenderCore::initialize(int w, int h) {
    if (initialized_) {
        return true;
    }

    cadence_diagnostics_ = cadence::global_session_if_enabled();

    reset_session_state();

    const RenderConfig cfg = get_render_config();

    boot_step("begins");
    // The scene at the window's picture: its exact size, square pixels.
    const auto start_aspect = galaxy::experimental_effective_host_aspect();
    const galaxy::ContentViewport start_content =
        galaxy::fit_content_viewport(w, h, start_aspect ? start_aspect->content_aspect : galaxy::kDesignContentAspect);
    const FrameMap start_frame = start_content.width > 0 && start_content.height > 0
                                     ? FrameMap(static_cast<unsigned>(start_content.width), static_cast<unsigned>(start_content.height))
                                     : FrameMap{};
    if (!renderer_.initialize(w, h, start_frame)) {
        std::cerr << "[RenderCore] RendererD3D12::initialize failed\n";
        return false;
    }
    boot_step("device rest");

    if (!display_pictures_.initialize(renderer_.device(), start_frame)) {
        std::cerr << "[RenderCore] EfbCopyManager::initialize failed\n";
        renderer_.shutdown();
        return false;
    }

    if (!texture_cache_.initialize(renderer_.device())) {
        std::cerr << "[RenderCore] TextureCache::initialize failed\n";
        display_pictures_.shutdown();
        renderer_.shutdown();
        return false;
    }
    if (!texture_cache_.preallocate_upload_arenas(kFramesInFlight)) {
        std::cerr << "[RenderCore] TextureCache upload preallocation failed\n";
        texture_cache_.shutdown();
        display_pictures_.shutdown();
        renderer_.shutdown();
        return false;
    }
    boot_step("frame reads and texture cache");
    // The resident decoded-vertex buffer (a 64 MB upload heap) is created here: creating it at first
    // use stalls gameplay.
    (void)ensure_resident_vertex_buffer();
    renderer_.create_first_use_resources();
    boot_step("first-use resources");

#if defined(NEBULA_GPU_VULKAN)
    if (!pipeline_cache_.initialize(renderer_.device(), renderer_.root_signature(), shader_cache_directory())) {
#else
    if (!pipeline_cache_.initialize(renderer_.device(), renderer_.root_signature())) {
#endif
        std::cerr << "[RenderCore] PipelineCache::initialize failed\n";
        texture_cache_.shutdown();
        display_pictures_.shutdown();
        renderer_.shutdown();
        return false;
    }
    boot_step("pipeline cache");

    initialized_ = true;
    stop_render_thread_ = false;
    render_chunks_in_flight_ = 0;
    present_chunks_in_flight_ = 0;
    utility_chunks_in_flight_ = 0;
    render_thread_ = std::thread(&RenderCore::render_thread_main, this);
    {
        try {
            frame_telemetry_ = std::make_unique<galaxy::telemetry::Publisher>([this] {
                const auto stats = display_present_stats();
                return galaxy::telemetry::Counts{
                    display_copy_count(), stats.first_serial_presentations, stats.presented,
                    stats.repeated_or_stale_serial_requests, stats.last_presented_copy_serial,
                    stats.last_presented_frame_stamp, stats.last_presented_slot,
                    display_copy_production_cadence_.published_last_sample_ns.load(std::memory_order_relaxed),
                    display_first_serial_present_cadence_.published_last_sample_ns.load(std::memory_order_relaxed)};
            }, static_cast<std::uint64_t>(w), static_cast<std::uint64_t>(h),
               renderer_.frame_map().height);
        } catch (...) { /* Optional startup allocation cannot fail gameplay. */ }
    }
    return true;
}

void RenderCore::pump_messages() {
    if (initialized_) {
        renderer_.pump_messages();
    }
}

bool RenderCore::quit_requested() const noexcept {
    return initialized_ && renderer_.quit_requested();
}

std::uint64_t RenderCore::display_copy_count() const noexcept {
    return stat_display_copies_.load(std::memory_order_relaxed);
}

DisplayPresentStats RenderCore::display_present_stats() const noexcept {
    const std::uint64_t last_stamp =
        last_presented_display_stamp_.load(std::memory_order_relaxed);
    const std::uint64_t last_serial =
        last_presented_display_serial_.load(std::memory_order_relaxed);
    return DisplayPresentStats{
        stat_display_present_requests_.load(std::memory_order_relaxed),
        stat_display_present_presented_.load(std::memory_order_relaxed),
        stat_display_first_serial_presentations_.load(std::memory_order_relaxed),
        stat_display_repeated_or_stale_serial_requests_.load(
            std::memory_order_relaxed),
        stat_display_present_skipped_duplicate_presents_.load(
            std::memory_order_relaxed),
        stat_display_present_duplicate_run_max_.load(std::memory_order_relaxed),
        stat_display_out_of_order_serial_requests_.load(
            std::memory_order_relaxed),
        stat_display_present_serial_delta_zero_.load(std::memory_order_relaxed),
        stat_display_present_serial_delta_one_.load(std::memory_order_relaxed),
        stat_display_present_serial_delta_multi_.load(std::memory_order_relaxed),
        stat_display_highest_first_presented_copy_serial_.load(
            std::memory_order_relaxed),
        last_stamp == ~std::uint64_t{0} ? 0u : last_stamp,
        last_serial == ~std::uint64_t{0} ? 0u : last_serial,
        last_presented_slot_.load(std::memory_order_relaxed),
    };
}

void RenderCore::record_monotonic_cadence_sample(
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
    static const bool monitor=::galaxy::diagnostics_recording()||::galaxy::run_option_u64(::galaxy::run_options().monitor_frame_tails, 0u)!=0u;
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

void RenderCore::reset_monotonic_cadence(
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

// GALAXY_TRACE_MEMORY=1: every 600 presented frames, what the renderer holds, in MB.
void RenderCore::trace_memory() {
    static const bool enabled = ::galaxy::run_option_u64(::galaxy::run_options().trace_memory, 0u) != 0u;
    static std::uint64_t frames = 0u;
    if (!enabled || ++frames % 600u != 0u) {
        return;
    }
    std::uint64_t arrays = 0u;
    for (const auto& [key, entry] : shape_arrays_) {
        arrays += entry.array->floats.capacity() * sizeof(float) + entry.array->colors.capacity() * sizeof(std::uint32_t);
    }
    std::uint64_t geometry = 0u;
    std::uint64_t vertices = 0u;
    for (const auto& [key, shape] : record_shapes_) {
        if (shape.geometry != nullptr) {
            geometry += shape.geometry->corners.capacity() * sizeof(shapes::Corner) + shape.geometry->indices.capacity() * sizeof(std::uint16_t);
        }
        vertices += shape.resident_vertices * sizeof(RenderVertex);
    }
    const auto mb = [](std::uint64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); };
    std::fprintf(stderr,
                 "[memory] shape arrays %.1f (%zu), groups %.1f, expanded vertices %.1f (%zu shapes), images %.1f, decoded %.1f, "
                 "upload arenas %.1f, texture pool %.1f, curved %.1f, record pool %.1f\n",
                 mb(arrays), shape_arrays_.size(), mb(geometry), mb(vertices), record_shapes_.size(), mb(texture_cache_.image_bytes()),
                 mb(texture_cache_.decoded_map_stats().resident_bytes), mb(texture_cache_.upload_arena_bytes()), mb(texture_cache_.pool_bytes()),
                 mb(curved::memory_bytes()),
                 mb(producer_record_pool_.vertices.capacity() * sizeof(RenderVertex) + producer_record_pool_.matrices.capacity() * sizeof(shapes::Matrix)));
#if defined(_WIN32)
    // GALAXY_TRACE_MEMORY=2: the process's heaps as well, each one's busy bytes and the block sizes holding the most.
    static const bool heaps = ::galaxy::run_option_u64(::galaxy::run_options().trace_memory, 0u) == 2u;
    if (heaps) {
        HANDLE list[64];
        const DWORD count = std::min<DWORD>(GetProcessHeaps(64, list), 64);
        for (DWORD i = 0; i < count; ++i) {
            std::unordered_map<std::uint64_t, std::uint64_t> by_size;
            std::uint64_t busy = 0u;
            std::uint64_t free_bytes = 0u;
            HeapLock(list[i]);
            PROCESS_HEAP_ENTRY entry{};
            while (HeapWalk(list[i], &entry)) {
                if ((entry.wFlags & PROCESS_HEAP_ENTRY_BUSY) != 0) {
                    busy += entry.cbData;
                    by_size[entry.cbData] += entry.cbData;
                } else if ((entry.wFlags & (PROCESS_HEAP_REGION | PROCESS_HEAP_UNCOMMITTED_RANGE)) == 0) {
                    free_bytes += entry.cbData;
                }
            }
            HeapUnlock(list[i]);
            if (busy < 1024u * 1024u) {
                continue;
            }
            std::vector<std::pair<std::uint64_t, std::uint64_t>> sizes(by_size.begin(), by_size.end());
            std::sort(sizes.begin(), sizes.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
            std::fprintf(stderr, "[memory] heap %lu: busy %.1f, free %.1f, largest by size:", i, mb(busy), mb(free_bytes));
            for (std::size_t s = 0; s < sizes.size() && s < 14u; ++s) {
                std::fprintf(stderr, " %llux%llu", static_cast<unsigned long long>(sizes[s].first), static_cast<unsigned long long>(sizes[s].second / sizes[s].first));
            }
            std::fprintf(stderr, "\n");
        }
    }
#endif
    [](auto& renderer) {
        if constexpr (requires { renderer.trace_rings(); }) {
            renderer.trace_rings();
        }
    }(renderer_);
}

void RenderCore::report_frame_tail_window() noexcept {
    static const bool enabled=::galaxy::diagnostics_recording()||::galaxy::run_option_u64(::galaxy::run_options().monitor_frame_tails, 0u)!=0u;
    if(!enabled) return;
    const auto now=display_first_serial_present_cadence_.last_sample_ns;
    if(!frame_tail_report_ns_) { frame_tail_report_ns_=now; return; }
    if(now-frame_tail_report_ns_<2000000000u) return;
    const auto elapsed=now-frame_tail_report_ns_;frame_tail_report_ns_=now;
    try {
        std::ostringstream row;
        row<<"[new-frame-tails] boundary=owned-display picture-copy-and-first-Present-return physical-display-time=unmeasured window-ns="<<elapsed;
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
        append("production",display_copy_production_cadence_);append("first-present",display_first_serial_present_cadence_);
        row<<'\n';std::cerr<<row.str();
    } catch (...) { /* Optional monitoring must not unwind rendering. */ }
}

void RenderCore::record_pointer_response(const DisplayTexture& texture) noexcept {
    static const bool enabled=::galaxy::diagnostics_recording()||::galaxy::run_option_u64(::galaxy::run_options().monitor_pointer_latency, 0u)!=0u;
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
        row<<"[mouse-response-latency] boundary=selected-KPAD-to-first-marked-display picture-Present-return physical-display-time=unmeasured"
           <<" samples="<<pointer_response_count_<<" mean-us="<<pointer_latency_total_/pointer_response_count_/1000u
           <<" p50-upper-us="<<percentile(50u)<<" p95-upper-us="<<percentile(95u)<<" p99-upper-us="<<percentile(99u)
           <<" max-us="<<pointer_latency_max_/1000u<<" source-serial="<<stamp.serial<<" generation="<<stamp.generation
           <<" display-serial="<<texture.copy_serial<<" selected-ns="<<stamp.selected_ns<<" processed-ns="<<stamp.processed_ns
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

void RenderCore::record_presented_display_copy(
    const DisplayTexture& texture) noexcept {
    galaxy::timeline::record(galaxy::timeline::Event::DisplayPresented, texture.slot,
                         texture.copy_serial);
    g_last_present_ns.store(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count(), std::memory_order_relaxed);
    record_pointer_response(texture);
    ++stat_display_present_presented_;
    const DisplaySerialAccountingEvent event =
        account_display_present_serial(
            display_serial_accounting_,
            texture.last_accounted_present_serial,
            texture.copy_serial);
    last_presented_display_stamp_.store(
        texture.frame_stamp, std::memory_order_relaxed);
    last_presented_display_serial_.store(
        texture.copy_serial, std::memory_order_relaxed);

    if (event.first_presentation) {
        ++stat_display_first_serial_presentations_;
        stat_display_highest_first_presented_copy_serial_.store(
            display_serial_accounting_.highest_presented_serial,
            std::memory_order_relaxed);
        record_monotonic_cadence_sample(display_first_serial_present_cadence_);
        report_frame_tail_window();
        trace_memory();
        if (event.below_high_water) {
            ++stat_display_out_of_order_serial_requests_;
        }
        if (event.delta_one) {
            ++stat_display_present_serial_delta_one_;
        } else if (event.delta_multi) {
            ++stat_display_present_serial_delta_multi_;
        }
        return;
    }

    ++stat_display_repeated_or_stale_serial_requests_;
    if (event.adjacent_repeat) {
        ++stat_display_present_serial_delta_zero_;
    } else {
        ++stat_display_out_of_order_serial_requests_;
    }
    std::uint64_t observed =
        stat_display_present_duplicate_run_max_.load(std::memory_order_relaxed);
    while (event.nonfirst_run > observed &&
           !stat_display_present_duplicate_run_max_.compare_exchange_weak(
               observed,
               event.nonfirst_run,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

void RenderCore::record_skipped_duplicate_display_present(
    const DisplayTexture& texture) noexcept {
    if (texture.copy_serial !=
        last_presented_display_serial_.load(std::memory_order_relaxed)) {
        return;
    }

    const DisplaySerialAccountingEvent event =
        account_display_present_serial(
            display_serial_accounting_,
            texture.last_accounted_present_serial,
            texture.copy_serial);
    if (!event.not_first_presentation) {
        return;
    }
    ++stat_display_repeated_or_stale_serial_requests_;
    ++stat_display_present_skipped_duplicate_presents_;
    if (event.adjacent_repeat) {
        ++stat_display_present_serial_delta_zero_;
    } else {
        ++stat_display_out_of_order_serial_requests_;
    }
    std::uint64_t observed =
        stat_display_present_duplicate_run_max_.load(std::memory_order_relaxed);
    while (event.nonfirst_run > observed &&
           !stat_display_present_duplicate_run_max_.compare_exchange_weak(
               observed,
               event.nonfirst_run,
               std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }
}

bool RenderCore::ensure_resident_vertex_buffer() {
    gpu::Device* const device = renderer_.device();
    if (device == nullptr) {
        return false;
    }
    if (resident_vertex_gpu_ != 0u && resident_vertex_device_ == device) {
        return true;
    }
    if (resident_vertex_buffer_failed_ && resident_vertex_device_ == device) {
        return false;
    }
    // A different device means the old one was shut down (drained) first.
    release_resident_vertex_buffer();
    resident_vertex_device_ = device;
    UploadBuffer buffer = renderer_.create_resident_vertices(
        static_cast<std::size_t>(kResidentDecodedVertexCapacity) * sizeof(RenderVertex),
        "Resident decoded vertices");
    if (buffer.gpu == 0u) {
        // Not fatal: every hit keeps using the per-frame ring upload.
        resident_vertex_buffer_failed_ = true;
        std::cerr << "[render-resident-vertices] buffer creation failed; "
                     "decoded vertices stay on the frame ring\n";
        return false;
    }
    resident_vertex_gpu_ = buffer.gpu;
    resident_vertex_buffer_ = std::move(buffer);
    resident_vertices_.reset(kResidentDecodedVertexCapacity, kFramesInFlight);
    renderer_.set_resident_vertices(resident_vertex_gpu_,
                                    static_cast<std::uint64_t>(kResidentDecodedVertexCapacity) *
                                        sizeof(RenderVertex));
    return true;
}

void RenderCore::release_resident_vertex_buffer() noexcept {
    // The GPU no longer reads the buffer (session reset before renderer initialization, or shutdown
    // after drain_gpu()). The arena reset bumps its epoch, invalidating every entry's block.
    resident_vertex_buffer_ = {};
    resident_vertex_gpu_ = 0;
    renderer_.set_resident_vertices(0u, 0u);
    resident_vertex_device_ = nullptr;
    resident_vertex_buffer_failed_ = false;
    resident_vertices_.reset(0u, kFramesInFlight);
}

// Producer (simulation thread): copies the frame and hands it to the render thread. Blocks only
// while the queue is full.
void RenderCore::render_frame(
    const RenderServices* services,
    bool present_swap_chain,
    bool black)
{
    if (!initialized_) {
        return;
    }

    // Nothing to render: nothing attached (records, scene markers, captures).
    const bool empty = producer_shape_records_.empty() && producer_stripe_particles_.empty();
    if (empty) {
        if (present_swap_chain) {
            present_cached_display(black, true);
        } else {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (render_thread_error_ != nullptr) {
                std::exception_ptr error = render_thread_error_;
                render_thread_error_ = nullptr;
                std::rethrow_exception(error);
            }
        }
        return;
    }
    galaxy::timeline::record(galaxy::timeline::Event::SubmitBegin, 0u);
    // Taken under the same condition that measures producer_wait_ns (the simulation thread's wait
    // for queue space), so a measured frame never subtracts a default time_point.
    FrameChunk chunk;
    chunk.pointer_response=producer_pointer_response_;
    take_attached_stripe_particles(chunk);
    take_attached_shape_records(chunk);
    chunk.prerecorded_movie=producer_movie_content_;
    chunk.movie_frame=producer_movie_frame_;
    chunk.services = services;
    chunk.present_swap_chain = present_swap_chain;
    chunk.black = black;
    const RenderTimingConfig& timing = get_frame_timing_config();

    std::uint64_t producer_wait_ns = 0;
    std::uint64_t diagnostic_queue_wait_ns = 0;
    std::size_t queue_depth_after_push = 0;
    const std::size_t queue_depth_limit = timing.render_queue_depth;
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
        if (render_thread_error_ != nullptr) {
            // Rethrow the render thread's failure here rather than leave a frozen display.
            std::exception_ptr error = render_thread_error_;
            render_thread_error_ = nullptr;
            std::rethrow_exception(error);
        }
        if (stop_render_thread_) {
            return;
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
            return;
        }
        frame_queue_.push_back(std::move(chunk));
        queue_depth_after_push = frame_queue_.size();
    }
    if (cadence_diagnostics_ != nullptr) {
        cadence_diagnostics_->record_phase(
            cadence::TimingPhase::ProducerQueueWait,
            diagnostic_queue_wait_ns);
    }
    queue_cv_.notify_one();
    galaxy::timeline::record(galaxy::timeline::Event::SubmitEnd, 0u, 0u);
}

void RenderCore::wait_for_render_idle() {
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
                utility_chunks_in_flight_ == 0u);
    });
    if (render_thread_error_ != nullptr) {
        std::exception_ptr error = render_thread_error_;
        render_thread_error_ = nullptr;
        std::rethrow_exception(error);
    }
}


// A perspective draw in the temporal scene: the clip-space offset that moves
// the image by the scene jitter in render pixels at this draw's viewport.
// The first such draw also supplies the camera planes and field of view.
void RenderCore::apply_scene_jitter(const VertexConstants& cpu, VertexConstants& upload,
                                   float jitter_ndc[2]) {
    jitter_ndc[0] = 0.0f;
    jitter_ndc[1] = 0.0f;
    if (draw_state_.projection_type != 0u) {
        return;
    }
    const FrameMap& map = renderer_.frame_map();
    const float width = std::fabs(draw_state_.viewport[2]) * map.sx;
    const float height = std::fabs(draw_state_.viewport[3]) * map.sy;
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
        // game perspective: clip z = E z + F, w = -z; NDC z -1 at near, 0 at far.
        const float c = draw_state_.projection[2];
        const float e = draw_state_.projection[4];
        const float f = draw_state_.projection[5];
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
void RenderCore::write_scene_vs_constants(const VertexConstants& cpu, VertexConstants& upload) {
    float jitter_ndc[2]{};
    if (temporal_scene_) {
        apply_scene_jitter(cpu, upload, jitter_ndc);
    }
    // The draw's position rows (motion vectors of the frame after read them as its previous ones).
    const std::uint32_t palette_offset = current_matrix_palette_ != 0u ? matrix_block_base_[0] : kNoPaletteOffset;
    temporal_vs_palette_va_ = current_matrix_palette_;
    if (std::exchange(scene_chunk_restart_, false) && interp_recording_ && !motion_scene_ &&
        current_replay_record_ >= 0) [[unlikely]] {
        // The chunk start re-uploaded the bindings of the open record: when
        // nothing changed (no marker, no matrix write, the same constants and
        // palette), its draws stay in that record instead of an unmatched one.
        InterpScene& scene = interp_scenes_[interp_scene_];
        const auto r = static_cast<std::size_t>(current_replay_record_);
        const std::uint64_t base = combine64(
            identity_marker_, static_cast<std::uint32_t>(matrices_.write_count() - identity_matrix_base_));
        if (identity_scene_ && base == identity_last_base_ && r < scene.constants.size()) {
            VertexConstants probe = cpu;
            probe.pos_base = scene.constants[r].pos_base;
            std::memcpy(probe.row_bases, scene.constants[r].row_bases, sizeof(probe.row_bases));
            // Derived from the fields compared (write_scene_vs_constants).
            std::memcpy(probe.motion_prev_proj, scene.constants[r].motion_prev_proj,
                        offsetof(VertexConstants, motion_ctrl) + sizeof(probe.motion_ctrl) -
                            offsetof(VertexConstants, motion_prev_proj));
            const std::int32_t shadow = scene.palette[r];
            const bool palette_continues =
                shadow < 0
                    ? !current_palette_required_
                    : current_palette_shadow_ >= 0 &&
                          (current_palette_shadow_ == shadow ||
                           same_rows(scene.palette_view(static_cast<std::size_t>(shadow)),
                                     scene.palette_view(static_cast<std::size_t>(current_palette_shadow_)), 0u,
                                     draw_rows::kRows));
            if (palette_continues && std::memcmp(&probe, &scene.constants[r], sizeof(probe)) == 0) {
                if (shadow >= 0 && current_palette_shadow_ != shadow &&
                    static_cast<std::size_t>(current_palette_shadow_) + 1u == scene.palette_count) {
                    --scene.palette_count;  // the duplicate snapshot (its rows stay: the game's indices may name them)
                    scene.palette_tables.resize(scene.palette_count * draw_rows::kRows);
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
            : 0u);
    record.identity = record_identity();
    record.layout = current_vs_hash_;
    record.load_signature = matrices_.load_signature();
    record.category = identity_category_;
    record.continuity = identity_continuity_;
    record.particle = identity_continuity_particle_;
    record.palette = cpu_vs_constants_.per_vertex != 0u;
    if (record.palette) {
        // Position matrix 0 (rows 0-2) stands for the per-vertex palette's object.
        for (unsigned i = 0; i < 12u; ++i) record.position[i] = matrices_.matrix_value(i / 4u, i % 4u);
        record.palette_offset = palette_offset;
    } else {
        std::memcpy(record.position.data(), cpu.inline_pos_matrix, sizeof(record.position));
    }
    record.translation[0] = record.position[3];
    record.translation[1] = record.position[7];
    record.translation[2] = record.position[11];
    for (unsigned i = 0; i < 6u; ++i) {
        record.projection[i] = draw_state_.projection[i];
    }
    record.projection[6] =
        draw_state_.projection_type != 0u ? 1.0f : 0.0f;
    TransformHistory& history = side_layer_ == kScreenLayer ? screen_history_
        : side_layer_ == kMirrorLayer                         ? mirror_history_
                                                              : transform_history_;
    const TransformRecord* previous = history.add(record);
    if (interp_recording_) [[unlikely]] {
        // The replay rebuilds this record's constants per sub-frame.
        InterpScene& scene = interp_scenes_[interp_scene_];
        current_replay_record_ = static_cast<std::int32_t>(scene.constants.size());
        // Not read back from `upload`: the ring is write-combined memory. Its matrix bases are the sequence's
        // palette source.
        scene.constants.push_back(cpu);
        const std::int32_t m = history.current_matches().back();
        scene.prev_index.push_back(m);
        // Inline draws read normals, lights and indexed texture matrices from
        // the palette too: their snapshot is interpolated like a skinned one.
        scene.palette.push_back(record.palette || current_palette_required_ ? current_palette_shadow_ : -1);
        scene.per_vertex.push_back(record.palette ? 1u : 0u);
        const auto [spatial, lights] = interp_draw_facts(draw_state_);
        scene.spatial_texgens.push_back(spatial);
        scene.light_masks.push_back(lights);
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
        VertexConstants& image = interp_scenes_[interp_scene_].constants.back();
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

std::uint32_t RenderCore::note_identity_occurrence(std::uint64_t key) {
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

void RenderCore::begin_identity_scene() {
    identity_scene_ = identity_scene_markers_ != 0u;
    identity_scene_markers_ = 0u;
    ++identity_scene_index_;
    identity_occurrences_used_ = 0u;
    identity_marker_ = mix64(0x5343454E45ull);
    identity_matrix_base_ = matrices_.write_count();
    identity_last_base_ = 0u;
    identity_sub_index_ = 0u;
    identity_continuity_ = 0u;
    identity_continuity_particle_ = false;
    identity_category_ = motion::kNoCategory;
    matrices_.reset_load_signature();
}

std::uint64_t RenderCore::record_identity() noexcept {
    if (!identity_scene_) {
        return 0u;
    }
    const std::uint64_t base = combine64(
        identity_marker_, static_cast<std::uint32_t>(matrices_.write_count() - identity_matrix_base_));
    identity_sub_index_ = base == identity_last_base_ ? identity_sub_index_ + 1u : 0u;
    identity_last_base_ = base;
    const std::uint64_t key = combine64(base, identity_sub_index_);
    return key != 0u ? key : 1u;
}

void RenderCore::on_host_marker(std::uint8_t kind, std::uint16_t aux, std::uint64_t a, std::uint64_t b) {
    // The game's bloom, drawn by the renderer at this point of the stream (game_bloom.h).
    if (kind == motion::kMarkerBloom) {
        flush_pending_draw_batch();
        renderer_.game_bloom(aux, static_cast<std::uint32_t>(a), static_cast<std::uint32_t>(b));
        return;
    }
    if (kind < identity_markers_by_kind_.size()) {
        if (identity_markers_by_kind_[kind]++ == 0u && rt_log_glows()) [[unlikely]] {
            std::fprintf(stderr, "[rt-identity] first marker of kind %u\n", static_cast<unsigned>(kind));
        }
    }
    // Materials and effects name nothing the renderer keeps (the ray tracer's material rows went with it).
    if (kind == motion::kMarkerMaterial || kind == motion::kMarkerEffect) {
        return;
    }
    if (kind >= motion::kMarkerCategory) {
        on_motion_marker(kind, aux, a, b);
        return;
    }
    if (kind == motion::kMarkerNameObj) {
    } else if (kind == motion::kMarkerShapePacket) {
    } else if (kind == motion::kMarkerEmitter || kind == motion::kMarkerParticle) {
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
        identity_continuity_ = static_cast<std::uint32_t>(b >> 16u);
        tag = b & 0xFFFFu;
    }
    set_identity_marker(kind, aux, a, tag);
}

void RenderCore::set_identity_marker(std::uint8_t kind, std::uint16_t aux, std::uint64_t a, std::uint64_t tag) {
    std::uint64_t key = combine64(combine64(kind | (static_cast<std::uint64_t>(aux) << 8u), a), tag);
    key = combine64(key, note_identity_occurrence(key));
    identity_marker_ = key;
    identity_matrix_base_ = matrices_.write_count();
    matrices_.reset_load_signature();
}

void RenderCore::on_motion_marker(std::uint8_t kind, std::uint16_t aux, std::uint64_t a, std::uint64_t b) {
    switch (kind) {
    case motion::kMarkerCategory: {
        ++motion_category_markers_;
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
        identity_category_ = motion::encode_category(phase, aux, static_cast<std::uint32_t>(a));
        return;
    }
    case motion::kMarkerScene:
        ++motion_scene_markers_;
        if (!history_scene_) {
            return;
        }
        // The game cut the view: the temporal upscaler's history is invalid.
        if (temporal_scene_ && (aux & motion::kSceneCameraCut) != 0u) {
            renderer_.reset_temporal_history();
        }
        if (interp_recording_) {
            InterpScene& scene = interp_scenes_[interp_scene_];
            scene.camera_epoch = static_cast<std::uint32_t>(a);
            scene.camera_epoch_known = true;
        }
        return;
    case motion::kMarkerPass:
        ++motion_mirror_markers_;
        if (aux == static_cast<std::uint16_t>(motion::PassMarker::MirrorBegin)) {
            end_side_layer();
            begin_side_layer(kMirrorLayer);
        } else if (side_layer_ == kMirrorLayer) {
            end_side_layer();
        }
        return;
    case motion::kMarkerLight: {
        ++motion_light_markers_;
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

// GameScene::draw2D's entry (SceneMarker::Scene2DBegin).
void RenderCore::on_scene_2d_begin() {
    if (scene_phase_) {
        // GameScene::draw3D returned before its clearZBuffer (movie, nerve):
        // the scene ends here.
        on_scene_marker(SceneMarker::SceneEnd, nullptr);
    }
    ++window_2d_scenes_;
}

void RenderCore::take_attached_stripe_particles(FrameChunk& chunk) {
    if (producer_stripe_particles_.empty()) {
        return;
    }
    chunk.stripe_particles.insert(chunk.stripe_particles.end(), producer_stripe_particles_.begin(),
                                  producer_stripe_particles_.end());
    producer_stripe_particles_.clear();
}

void RenderCore::take_attached_shape_records(FrameChunk& chunk) {
    if (producer_shape_records_.empty()) {
        return;
    }
    // The chunk takes the vector whole rather than moving each record, and the next chunk's records get room for as
    // many (growing from empty moved every record again at each doubling).
    const std::size_t count = producer_shape_records_.size();
    if (chunk.shape_records.empty()) {
        chunk.shape_records.swap(producer_shape_records_);
        producer_shape_records_.reserve(count);
        const std::size_t matrices = producer_record_pool_.matrices.size();
        const std::size_t ops = producer_record_pool_.ops.size();
        const std::size_t natives = producer_record_pool_.natives.size();
        const std::size_t vertices = producer_record_pool_.vertices.size();
        chunk.record_pool.scenes.swap(producer_record_pool_.scenes);
        chunk.record_pool.vertices.swap(producer_record_pool_.vertices);
        producer_record_pool_.vertices.reserve(vertices);
        chunk.record_pool.probes.swap(producer_record_pool_.probes);
        producer_record_pool_.probes.clear();
        chunk.record_pool.matrices.swap(producer_record_pool_.matrices);
        chunk.record_pool.ops.swap(producer_record_pool_.ops);
        chunk.record_pool.natives.swap(producer_record_pool_.natives);
        producer_record_pool_.matrices.reserve(matrices);
        producer_record_pool_.ops.reserve(ops);
        producer_record_pool_.natives.reserve(natives);
        return;
    }
    const auto first_matrix = static_cast<std::uint32_t>(chunk.record_pool.matrices.size());
    const auto first_op = static_cast<std::uint32_t>(chunk.record_pool.ops.size());
    chunk.record_pool.matrices.insert(chunk.record_pool.matrices.end(), producer_record_pool_.matrices.begin(),
                                      producer_record_pool_.matrices.end());
    chunk.record_pool.ops.insert(chunk.record_pool.ops.end(), producer_record_pool_.ops.begin(), producer_record_pool_.ops.end());
    const auto first_native = static_cast<std::uint32_t>(chunk.record_pool.natives.size());
    const auto first_vertex = static_cast<std::uint32_t>(chunk.record_pool.vertices.size());
    const auto first_scene = static_cast<std::uint32_t>(chunk.record_pool.scenes.size());
    chunk.record_pool.scenes.insert(chunk.record_pool.scenes.end(), producer_record_pool_.scenes.begin(), producer_record_pool_.scenes.end());
    producer_record_pool_.scenes.clear();
    chunk.record_pool.vertices.insert(chunk.record_pool.vertices.end(), producer_record_pool_.vertices.begin(), producer_record_pool_.vertices.end());
    producer_record_pool_.vertices.clear();
    const auto first_probe = static_cast<std::uint32_t>(chunk.record_pool.probes.size());
    chunk.record_pool.probes.insert(chunk.record_pool.probes.end(), producer_record_pool_.probes.begin(), producer_record_pool_.probes.end());
    producer_record_pool_.probes.clear();
    chunk.record_pool.natives.insert(chunk.record_pool.natives.end(), producer_record_pool_.natives.begin(), producer_record_pool_.natives.end());
    producer_record_pool_.matrices.clear();
    producer_record_pool_.ops.clear();
    producer_record_pool_.natives.clear();
    chunk.shape_records.reserve(chunk.shape_records.size() + count);
    for (shapes::Record& record : producer_shape_records_) {
        record.matrix_first += first_matrix;
        record.op_first += first_op;
        record.vertex_first += record.kind == shapes::Record::Kind::Readback ? first_probe : first_vertex;
        record.native_index += record.native_index == shapes::kNoNative ? 0u : record.kind == shapes::Record::Kind::Scene ? first_scene : first_native;
        chunk.shape_records.push_back(std::move(record));
    }
    producer_shape_records_.clear();
}

void RenderCore::note_stripe_particles(const FrameChunk& chunk) {
    motion_stripe_marks_ += chunk.stripe_particles.size();
}

// A fresh InterpScene for the frame being recorded (its mirror pass or its 3D
// scene, whichever comes first).
void RenderCore::open_interp_scene() {
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
void RenderCore::end_record_layer(unsigned layer) {
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
void RenderCore::begin_side_layer(unsigned layer) {
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
    identity_matrix_base_ = matrices_.write_count();
    identity_last_base_ = 0u;
    identity_sub_index_ = 0u;
    identity_continuity_ = 0u;
    identity_continuity_particle_ = false;
    identity_category_ = motion::kNoCategory;
    matrices_.reset_load_signature();
    matrices_.set_load_tracking(true);
    matrix_palette_stale_ = true;
    current_palette_shadow_ = -1;
    current_replay_record_ = -1;
    current_ps_record_ = -1;
    ps_record_restart_ = true;
}

void RenderCore::end_side_layer() {
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
    matrices_.set_load_tracking(false);
}

// A frame of a run that draws no 3D scene, from its first draw: an InterpScene whose whole picture is its screen
// layer (no camera), matched with the frame before's by the layouts' identities, and a renderer log that counts as
// the frame's scene.
void RenderCore::begin_screen_frame() {
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
void RenderCore::cancel_screen_frame() {
    end_side_layer();
    interp_scenes_[interp_scene_].valid = false;
    renderer_.cancel_replay_screen_frame();
    screen_frame_open_ = false;
}

RtView RenderCore::scene_depth_view(const float terms[6], const ScaledViewport& viewport) const {
    const auto projection = [terms](unsigned i) {
        return terms[i];
    };
    // fill_vs_projection: D3D clip z = -(z E + F) (1 - 1e-7).
    constexpr float kFarEps = 1.0f - 1e-7f;
    RtView view{};
    view.projection = {projection(0u), projection(1u), projection(2u), projection(3u)};
    // The scene's depth is drawn with the temporal upscaler's jitter (apply_scene_jitter: clip x + jx w, so the
    // projection's x offset B becomes B - jx; y likewise). Passes rebuild every sample's position from that depth, so
    // the view carries the jitter too, or each sample would land up to half a pixel off its surface.
    if (temporal_scene_) {
        const float width = viewport.width;
        const float height = viewport.height;
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

// The game's opaque surfaces are drawn: the occlusion from their depth, multiplied into their colour under what blends.
void RenderCore::apply_scene_ao(const std::array<float, 12>& view) {
    if (!ambient_occlusion_ || !scene_phase_ || side_layer_ != kSceneLayer) {
        return;
    }
    const ScaledViewport viewport = viewport_pixels(view.data() + 6, renderer_.frame_map());
    if (!viewport.valid()) {
        return;
    }
    flush_pending_draw_batch();
    renderer_.set_scene_ao_view(scene_depth_view(view.data(), viewport));
    ao_objects_.finish();
    collect_ao_objects_ = false;
    renderer_.set_scene_ao_objects(ao_objects_.list());
    renderer_.record_scene_ao();
}

void RenderCore::on_scene_marker(SceneMarker marker, const std::array<float, 12>* view, std::uint32_t sky_up,
                               bool sky_up_known, const SceneStage* stage) {
    flush_pending_draw_batch();
    if (marker == SceneMarker::SceneBegin) {
        end_side_layer();
        // A frame recorded as screen-only (its frame before drew no scene) draws one after all: the scene's record
        // replaces it.
        frame_scene_seen_ = true;
        if (screen_frame_open_) {
            cancel_screen_frame();
        }
    }
    scene_phase_ = marker == SceneMarker::SceneBegin;
    if (marker == SceneMarker::SceneBegin) {
        // The game names its stage: a new one starts the tracer's stage state over.
        if (stage != nullptr && (*stage)[0] != 0 && (!scene_stage_known_ || *stage != scene_stage_)) {
            scene_stage_ = *stage;
            scene_stage_known_ = true;
        }
        // The enhanced sky's up for the scene's draws (the view's up without a player).
        const bool wide = enhanced_lighting_ && get_render_config().wide_scene;
        const std::uint32_t known = (sky_up_known ? 1u : 0u) | (enhanced_lighting_ ? 2u : 0u) | (wide ? 4u : 0u);
        if (cpu_vs_constants_.sky_up != sky_up || cpu_vs_constants_.sky_up_known != known) {
            cpu_vs_constants_.sky_up = sky_up;
            cpu_vs_constants_.sky_up_known = known;
            sky_up_dirty_ = true;
        }
        renderer_.set_scene_view(view);
    } else {
        if (side_layer_ == kMirrorLayer) {
            end_side_layer();
        }
    }
    const bool was_temporal = temporal_scene_;
    const bool was_motion = motion_scene_;
    if (marker == SceneMarker::SceneBegin) {
        collect_ao_objects_ = ambient_occlusion_ && side_layer_ == kSceneLayer;
        if (collect_ao_objects_) {
            ao_objects_.begin(view != nullptr ? view->data() : nullptr);
        }
        temporal_scene_ = renderer_.scene_temporal_begin(
            scene_jitter_px_[0], scene_jitter_px_[1]);
        {
            // Rendered under the output, the scene's textures keep the output's detail: the upscaler takes it from the
            // jittered samples of many frames.
            const unsigned output_width = renderer_.temporal_output_width();
            const unsigned render_width = renderer_.frame_map().width;
            int bias = 0;
            if (temporal_scene_ && output_width > render_width && render_width != 0u) {
                bias = static_cast<int>(std::lround(32.0 * std::log2(static_cast<double>(render_width) / output_width)));
            }
            if (bias != scene_mip_bias_x32_) {
                scene_mip_bias_x32_ = bias;
                texture_bindings_dirty_ = true;
            }
        }
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
            matrices_.set_load_tracking(true);
            if (interp_recording_) {
                InterpScene& scene = interp_scenes_[interp_scene_];
                scene.camera = transform_history_.camera_delta();
                scene.view = camera;
                scene.view_known = view != nullptr;
            }
        } else {
            transform_history_.reset();
            matrices_.set_load_tracking(false);
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
        if (scene_mip_bias_x32_ != 0) {
            scene_mip_bias_x32_ = 0;
            texture_bindings_dirty_ = true;
        }
        current_replay_record_ = -1;
        current_ps_record_ = -1;
        matrices_.set_load_tracking(false);
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
    // Scene end: the 2D layer renders into the output-resolution scene target. A new
    // scene that begins before a display copy restores the render scene target.
    bool switched = false;
    display_copy_on_output_ = false;
    if (marker == SceneMarker::SceneEnd) {
        renderer_.end_wide_scene();
        renderer_.record_scene_aa();
        switched = renderer_.begin_output_scene();
    } else if (renderer_.output_scene_active()) {
        renderer_.end_output_scene();
        switched = true;
    }
    if (marker == SceneMarker::SceneBegin) {
        renderer_.begin_wide_scene(enhanced_lighting_ && get_render_config().wide_scene);
    }
    if (switched) {
        display_pictures_.set_frame_map(renderer_.frame_map());
        frame_bindings_dirty_ = true;
    }
    if (marker == SceneMarker::SceneEnd) {
        s_layer_2d_lines.store(renderer_.frame_map().height, std::memory_order_relaxed);
        begin_side_layer(kScreenLayer);
    }
    if (marker == SceneMarker::SceneBegin) {
    }
}

// A display copy completed a game frame's log: its scene (the one recording
// when the copy landed) is the one to interpolate when its present arrives.
void RenderCore::note_interp_log_completion() {
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
    interp_log_slot_ = renderer_.replay_picture_slot();
}

void RenderCore::run_pending_interp_start() {
    if (!interp_start_pending_) {
        return;
    }
    interp_start_pending_ = false;
    if (interp_sequence_.active) {
        supersede_interp_sequence();
    }
    if (interp_log_waiting_ && renderer_.replay_available()) {
        interp_log_waiting_ = false;
        start_interp_sequence(interp_log_slot_);
        advance_interp_sequence(false);
    }
}

void RenderCore::start_interp_sequence(std::uint32_t picture_slot) {
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
        vblanks_per_slot = galaxy::render::vblanks_per_slot(refresh, rate);
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
    interp_sequence_.picture_slot = picture_slot;
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
void RenderCore::select_interp_before() {
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
unsigned RenderCore::interp_gpu_governor(double& rate) {
    constexpr double budget_ms = 15.0;
    const unsigned configured = interp_stopping_ ? 0u : interp_frames_;
    if (configured == 0u) {
        return configured;
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

// Sub-frames must never cost the game its own frames. Each game frame shown is timed against the frame period: an
// interval above 1.5 periods missed VIs. A miss is the sub-frames' when the interval would not have missed without
// their render-thread time: the game waited on the render thread, and would have waited that much less (at most what
// it waited). The rest are the game's own, as a scene's first frames, whose decoding, uploads and pipelines keep the
// render thread far longer than sub-frames do. Two or more of the sub-frames' misses a second, and no fewer than the
// game's own, take sub-frames away to the next lower present rate (to half of them at eight). The game's own misses
// three seconds in a row above their lowest rate are probed, for threads that share a CPU's cores: the next lower
// rate for a second, kept only if the misses halve. A try of the next higher rate is undone at its first miss of the
// sub-frames'. Tries and probes follow a clean interval that doubles after each failed one (2 s to 64 s: a scene's
// first frames back the render thread up for a second or two) and halves after a try that held.
void RenderCore::restart_interp_governors() {
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

void RenderCore::note_game_frame_cadence(std::int64_t now_qpc) {
    static const std::int64_t frequency = galaxy::qpc_frequency();
    static const bool trace = ::galaxy::run_option_u64(::galaxy::run_options().trace_present_stats, 0u) != 0u;
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
double RenderCore::interp_rate_with(unsigned subframes) const {
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
unsigned RenderCore::interp_step_down(unsigned subframes) const {
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
unsigned RenderCore::interp_step_up(unsigned subframes) const {
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
void RenderCore::supersede_interp_sequence() {
    if (!interp_sequence_.active) {
        return;
    }
    interp_sequence_.active = false;
    ++interp_superseded_;
}

// t of the next slot in the playing sequence (see advance_interp_sequence).
double RenderCore::interp_slot_t() const {
    static const std::int64_t frequency = galaxy::qpc_frequency();
    return static_cast<double>(interp_slot_due_qpc_ - interp_sequence_.frame_qpc) * 60.0 /
               static_cast<double>(frequency) + interp_sequence_.lead;
}

bool RenderCore::overlay_idle_presents() const noexcept {
    static const std::int64_t frequency = galaxy::qpc_frequency();
    return (native_settings_overlay_visible() || home_menu_visible()) && !interp_sequence_.active &&
           interp_slot_period_qpc_ > 0 && interp_slot_period_qpc_ * 61 < frequency;
}

std::int64_t RenderCore::interp_remaining_us() const {
    const std::int64_t now = galaxy::qpc_now();
    static const std::int64_t frequency = galaxy::qpc_frequency();
    return (interp_slot_due_qpc_ - now) * 1'000'000 / frequency;
}

#if defined(_WIN32) && !defined(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

// Render thread: a short wait to a present slot, finer than the scheduler tick.
void RenderCore::interp_sleep_us(std::int64_t us) {
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

namespace {
// GALAXY_TRACE_INTERP_SLOTS=<file>: each present slot, kept in memory and written once after 30 s, so the timing measured is the
// timing played. tools/verify/interp_slots.py reads it.
struct InterpSlotRecord {
    std::int64_t slot;
    std::int64_t began;
    std::int64_t ended;
    double t;
    std::uint32_t sequence;
    std::uint32_t kind;  // 0 a sub-frame, 1 the game frame itself
};
std::vector<InterpSlotRecord> g_interp_slots;

void write_interp_slots() {
    const char* const path = ::galaxy::run_options().trace_interp_slots;
    if (path == nullptr) {
        return;
    }
    if (std::FILE* const file = ::galaxy::open_file(path, "w")) {
        std::fprintf(file, "frequency %lld\n", static_cast<long long>(galaxy::qpc_frequency()));
        for (const InterpSlotRecord& r : g_interp_slots) {
            std::fprintf(file, "%lld %lld %lld %.6f %u %u\n", static_cast<long long>(r.slot), static_cast<long long>(r.began),
                         static_cast<long long>(r.ended), r.t, r.sequence, r.kind);
        }
        std::fclose(file);
    }
}

void note_interp_slot(std::int64_t slot, std::int64_t began, double t, std::uint32_t sequence, std::uint32_t kind) {
    static bool trace = [] {
        if (::galaxy::run_options().trace_interp_slots == nullptr) {
            return false;
        }
        g_interp_slots.reserve(4'000'000u);
        return true;
    }();
    if (!trace) {
        return;
    }
    g_interp_slots.push_back({slot, began, galaxy::qpc_now(), t, sequence, kind});
    // 30 s of slots, then the one stall of writing them, after anything measured.
    if (g_interp_slots.size() == g_interp_slots.capacity() ||
        began - g_interp_slots.front().began > 30 * galaxy::qpc_frequency()) {
        write_interp_slots();
        trace = false;
    }
}
}  // namespace

void RenderCore::advance_interp_sequence(bool flush) {
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
            const std::string snapshot = present_snapshot_path();  // sub-frames count as presents
            if (!snapshot.empty()) {
                renderer_.set_debug_capture_paths({}, snapshot);
                renderer_.set_subframe_capture(true);
                renderer_.set_capture_waits(true);
            }
            const std::int64_t subframe_start = galaxy::qpc_now();
            const bool presented = render_interp_subframe(static_cast<float>(t));
            interp_subframe_qpc_ += static_cast<std::uint64_t>(galaxy::qpc_now() - subframe_start);
            note_interp_slot(slot, subframe_start, t, static_cast<std::uint32_t>(interp_sequence_serial_), 0u);
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
            } else {
                ++interp_failed_;
            }
            ++sequence.next;
            // Rates that are not multiples of 60 rarely present real frames.
            report_interp_stats();
            return;
        }
        sequence.active = false;
        interp_presenting_real_ = true;
        const std::int64_t real_start = galaxy::qpc_now();
        present_cached_display_on_thread(false, false, true, sequence.picture_slot);
        note_interp_slot(slot, real_start, 1.0, static_cast<std::uint32_t>(interp_sequence_serial_), 1u);
        note_present_vblank(slot);
        interp_presenting_real_ = false;
        interp_shown_scene_ = static_cast<std::int32_t>(sequence.scene);
        ++interp_sequences_;
        report_interp_stats();
        return;
    }
}

// A present while interpolating: the vblank interval it was issued in, against the one before (0: two in one
// interval, one of them never shown on its own; 2 or more at one present a vblank: a refresh shown twice), and the
// time from its slot to the vblank ending that interval. The picture shows its slot's moment, so with even motion
// that time stays the same; its spread is the motion's unevenness on the display.
void RenderCore::note_present_vblank(std::int64_t slot) {
    static const bool trace = ::galaxy::run_option_u64(::galaxy::run_options().trace_present_stats, 0u) != 0u;
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

// A game frame presented while interpolating, without sub-frames: why.
void RenderCore::note_interp_real_present() {
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

void RenderCore::report_interp_stats() {
    static const bool trace = ::galaxy::run_option_u64(::galaxy::run_options().trace_present_stats, 0u) != 0u;
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
bool RenderCore::render_interp_subframe(float t) {
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
    // The scene before is the frame presented before this one: a log in between that was not recorded (shown as it
    // was: two 3D scenes, or the first of a run of frames without one) leaves it older, and sub-frames from it would
    // step back behind the frame shown last. A chained sequence maps its records through the skipped scenes.
    const bool before_valid = before_scene.valid &&
                              (interp_sequence_.chained || before_scene.serial == 0u || scene.serial == 0u ||
                               before_scene.serial + 1u == scene.serial);
    // The shadow's palette before, or none.
    const auto before_palette = [&](std::int32_t shadow) -> RowView {
        return before_valid && shadow >= 0 && static_cast<std::size_t>(shadow) < before_scene.palette_count
            ? before_scene.palette_view(static_cast<std::size_t>(shadow))
            : RowView{};
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
            const VertexConstants& now = scene.constants[r];
            const VertexConstants* before = prev_data ? &before_scene.constants[static_cast<std::size_t>(m)] : nullptr;
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
                const std::int32_t pair_before = !before_palette(before_shadow).empty() ? before_shadow : -1;
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
                entry.light_mask |= scene.light_masks[r];
                if (scene.per_vertex[r] != 0u) {
                    entry.position_mask = kAllPaletteSlots;
                    entry.normal_mask = kAllPaletteSlots;
                } else if ((now.flags & 0x02u) != 0u) {
                    const std::uint32_t slot = (now.draw_rows[0][0] & 0x3Fu) / 3u;
                    if (slot < 10u) entry.normal_mask |= static_cast<std::uint16_t>(1u << slot);
                }
                motion.palette = index;
            }
            if (motion.position_moves || motion.tex_moves || motion.projection_moves ||
                motion.palette >= 0) {
                interp_pending_.push_back(motion);
            }
        }
        const auto palettes_start = std::chrono::steady_clock::now();
        // Each palette pair once, with the slots its records read.
        for (InterpPaletteMotion& entry : interp_palette_motions_) {
            palette_motion(before_palette(entry.before), scene.palette_view(static_cast<std::size_t>(entry.shadow)),
                           layer_camera(entry.layer),
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
                motion.palette >= 0) {
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
bool RenderCore::prepare_interp_sequence(const InterpScene& scene, const InterpScene& before_scene,
                                        bool before_valid) {
    const std::size_t count = scene.constants.size();
    // The shadow's palette before, or none.
    const auto before_palette = [&](std::int32_t shadow) -> RowView {
        return before_valid && shadow >= 0 && static_cast<std::size_t>(shadow) < before_scene.palette_count
            ? before_scene.palette_view(static_cast<std::size_t>(shadow))
            : RowView{};
    };
    interp_program_.clear();
    interp_program_.set_endpoint_cameras(camera_at(interp_camera_, 0.0f), camera_at(interp_camera_, 1.0f));
    interp_vs_slot_.assign(count, -1);
    interp_record_palette_slot_.assign(count, -1);
    interp_palette_slot_.assign(interp_palette_motions_.size(), -1);
    interp_palette_source_.clear();
    interp_palette_entry_.clear();
    for (const InterpRecordMotion& moving : interp_moving_) {
        if (moving.palette < 0) continue;
        std::int32_t& slot = interp_palette_slot_[static_cast<std::size_t>(moving.palette)];
        if (slot < 0) {
            slot = static_cast<std::int32_t>(interp_palette_source_.size());
            const VertexConstants& c = scene.constants[moving.record];
            interp_palette_source_.push_back({c.pos_base, c.row_bases[1], c.row_bases[0], c.row_bases[2],
                                              c.row_bases[3]});
            interp_palette_entry_.push_back(moving.palette);
        }
    }
    // Temporal upscaling of the replays: the motion vector fields of each
    // moving block at the frame presented before (blocks the program leaves
    // alone carry their own transform there: no motion).
    const bool motion = renderer_.temporal_interpolation();
    interp_layout_ = {static_cast<std::uint32_t>(interp_moving_.size()),
                      static_cast<std::uint32_t>(interp_palette_source_.size()),
                      motion ? static_cast<std::uint32_t>(interp_palette_source_.size()) : 0u};
    const interp_gpu::Layout& layout = interp_layout_;
    for (std::size_t s = 0; s < interp_moving_.size(); ++s) {
        const InterpRecordMotion& moving = interp_moving_[s];
        interp_vs_slot_[moving.record] = static_cast<std::int32_t>(s);
        const std::uint32_t base = layout.vs_offset(static_cast<std::uint32_t>(s)) / 4u;
        const VertexConstants& recorded = scene.constants[moving.record];
        const VertexConstants* before = moving.prev >= 0
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
                const bool spatial = ((scene.spatial_texgens[moving.record] >> i) & 1u) != 0u;
                interp_program_.tex_matrix(base + interp_gpu::kVsTex + 12u * i, &before->inline_tex_matrices[i][0][0],
                                           &recorded.inline_tex_matrices[i][0][0], spatial);
                interp_program_.tex_matrix(base + interp_gpu::kVsPost + 12u * i,
                                           &before->inline_post_matrices[i][0][0],
                                           &recorded.inline_post_matrices[i][0][0], spatial);
            }
        }
        if (moving.projection_moves && before != nullptr) {
            interp_program_.projection(base, *before, recorded);
        }
        std::int32_t palette_slot = -1;
        if (moving.palette >= 0) {
            const std::int32_t q = interp_palette_slot_[static_cast<std::size_t>(moving.palette)];
            if (q >= 0) {
                palette_slot = q;
                interp_record_palette_slot_[moving.record] = q;  // the replay binds the output's rows
                // Its blocks: the slot's rows (the output's from its palette base), in the draw_rows numbering.
                const std::uint32_t rows =
                    (layout.palette_offset(static_cast<std::uint32_t>(q)) - layout.palette_base()) / 16u +
                    interp_gpu::kPaletteRows / 4u;
                const std::uint32_t row_bases[4]{rows + draw_rows::kNormal, rows + draw_rows::kTexture,
                                                 rows + draw_rows::kPost, rows + draw_rows::kLight};
                interp_program_.store(base + interp_gpu::kVsPosBase, &rows, 1u);
                interp_program_.store(base + interp_gpu::kVsRowBases, row_bases, 4u);
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
                                16u + interp_gpu::kPrevPaletteRows / 4u};
                interp_program_.store(base + interp_gpu::kVsMotionCtrl, ctrl, 2u);
            }
            interp_program_.at_previous(false);
        } else if (palette_slot >= 0) {
            // The record reads its rows from the output's slot: its previous rows (a motion-vector draw's) are the
            // slot's own too, not the ring rows its copied constants name.
            const std::uint32_t ctrl[2]{0u, kMotionOwnRows};
            interp_program_.store(base + interp_gpu::kVsMotionCtrl, ctrl, 2u);
        }
    }
    for (std::size_t q = 0; q < interp_palette_entry_.size(); ++q) {
        const InterpPaletteMotion& entry = interp_palette_motions_[static_cast<std::size_t>(interp_palette_entry_[q])];
        const RowView now_rows = scene.palette_view(static_cast<std::size_t>(entry.shadow));
        const RowView before_rows = before_palette(entry.before);
        interp_program_.palette(layout.palette_offset(static_cast<std::uint32_t>(q)) / 4u + interp_gpu::kPaletteRows,
                                entry.motion, before_rows, now_rows, layer_camera(entry.layer));
        if (q < layout.prev_palette_count) {
            interp_program_.at_previous(true);
            interp_program_.palette_positions(
                layout.prev_palette_offset(static_cast<std::uint32_t>(q)) / 4u + interp_gpu::kPrevPaletteRows,
                                              entry.motion, before_rows, now_rows);
            interp_program_.at_previous(false);
        }
    }
    interp_program_ops_ += interp_program_.ops.size();
    interp_program_words_ += interp_program_.payload.size();
    Renderer::ReplaySequence sequence{};
    sequence.layout = layout;
    sequence.record_vs_slot = interp_vs_slot_;
    sequence.record_palette_slot = interp_record_palette_slot_;
    sequence.palette_source = interp_palette_source_;
    sequence.program = &interp_program_;
    sequence.vertex_blend = true;
    return renderer_.begin_replay_sequence(sequence);
}

bool RenderCore::skip_for_present_rate() {
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

void RenderCore::present_cached_display(
    bool black,
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
                back.black = black;
                queue_cv_.notify_one();
                return;
            }
            if (!back.present_only && !back.present_swap_chain) {
                back.present_swap_chain = true;
                back.black = black;
                queue_cv_.notify_one();
                return;
            }
        }
        FrameChunk chunk;
        chunk.present_only = true;
        chunk.present_swap_chain = true;
        chunk.black = black;
        frame_queue_.push_back(std::move(chunk));
    }
    queue_cv_.notify_one();
}

// Consumer loop (render thread): frames execute in submission order; queued
// frames are drained before a stop so the last presents land.
void RenderCore::render_thread_main() {
    galaxy::host::name_current_thread(L"Nebula Render");
    galaxy::host::perf_hint_add_current_thread();
#if defined(_WIN32)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif
    {
        // Opt the process out of EcoQoS throttling, so hybrid CPUs do not park the
        // simulation, render or audio threads on efficiency cores.
        static std::once_flag power_throttling_once;
        std::call_once(power_throttling_once, [] {
            galaxy::host::opt_out_of_power_throttling_process();
#if defined(_WIN32)
            // 1 ms timer granularity: otherwise sleeps, condition-variable timeouts and
            // waitable timers round to the 15.6 ms scheduler tick, delaying fence
            // polling and every sub-frame wait.
            galaxy::host::hold_high_resolution_timer(true);
#endif
        });
    }
    // Called with queue_mutex_ held.
    const auto fail_queued_utilities = [&](std::exception_ptr error) {
        render_thread_error_ = error;
        stop_render_thread_ = true;
        queue_space_cv_.notify_all();
    };
    try {
    for (;;) {
        try {
            poll_screen_probes();
        } catch (...) {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            fail_queued_utilities(std::current_exception());
            break;
        }
        FrameChunk chunk;
        bool have_chunk = false;
        bool idle_present = false;
        bool overlay_idle = false;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            const RenderConfig cfg = get_render_config();
            idle_present = cfg.frame_rate_decouple &&
                (cfg.vsync_interval != 0u || presented_max_fps(cfg) > 0.0f);
            const bool poll_probes = probe_flights_active_ != 0u;
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
            } else if (idle_present || poll_probes) {
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
                    break;  // stop requested, queue drained
                }
            } else {
                chunk = std::move(frame_queue_.front());
                frame_queue_.pop_front();
                if (!chunk.present_only && !chunk.present_swap_chain &&
                    !frame_queue_.empty() &&
                    frame_queue_.front().present_only) {
                    FrameChunk present = std::move(frame_queue_.front());
                    frame_queue_.pop_front();
                    chunk.present_swap_chain = present.present_swap_chain;
                    chunk.black = present.black;
                }
                if (chunk.present_only) {
                    ++present_chunks_in_flight_;
                } else {
                    galaxy::timeline::record(galaxy::timeline::Event::RenderBegin, 0u, 0u);
                    render_chunk_started_ns_ = static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count());
                    ++render_chunks_in_flight_;
                }
                have_chunk = true;
            }
        }

        try {
            if (have_chunk) {
                queue_space_cv_.notify_all();
                if (chunk.present_only) {
                    // An interpolation sequence presents the latest frame itself.
                    if (chunk.black) {
                        supersede_interp_sequence();
                        interp_log_waiting_ = false;
                        present_cached_display_on_thread(true, false);
                    } else if (interp_log_waiting_ && renderer_.replay_available()) {
                        supersede_interp_sequence();
                        interp_log_waiting_ = false;
                        start_interp_sequence(interp_log_slot_);
                        advance_interp_sequence(false);
                    } else if (!interp_sequence_.active) {
                        present_cached_display_on_thread(false, false);
                    } else {
                        ++interp_present_skipped_;
                    }
                } else {
                    if (interp_sequence_.active && interp_sequence_.flush_requested) {
                        advance_interp_sequence(true);
                    }
                    render_frame_on_thread(chunk);
                    run_pending_interp_start();
                    {
                        std::lock_guard<std::mutex> lock(queue_mutex_);
                        render_chunk_started_ns_ = 0u;
                    }
                }
            } else if (idle_present) {
                present_latest_frame_on_thread();
            } else if (overlay_idle) {
                // A due slot presents again the picture last presented (never a newer one between the game's own
                // presents) unless a present of the game's own landed in it.
                const std::int64_t now = galaxy::qpc_now();
                if (now >= overlay_idle_due_qpc_) {
                    if (renderer_.last_present_qpc() < overlay_idle_due_qpc_ - interp_slot_period_qpc_) {
                        present_cached_display_on_thread(false, false, false,
                                                     last_presented_slot_.load(std::memory_order_relaxed));
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
                if (chunk.present_only) {
                    if (present_chunks_in_flight_ != 0u) {
                        --present_chunks_in_flight_;
                    }
                } else if (render_chunks_in_flight_ != 0u) {
                    --render_chunks_in_flight_;
                }
            }
            fail_queued_utilities(std::current_exception());
            break;
        }

        if (have_chunk && chunk.present_only) {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (present_chunks_in_flight_ != 0u) {
                --present_chunks_in_flight_;
            }
            queue_space_cv_.notify_all();
        } else if (have_chunk) {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            // Every chunk returns its buffers here. Past kChunkPoolRetainedBuffers a buffer is
            // dropped (one re-allocation later) rather than kept at its peak capacity.
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

std::uint32_t RenderCore::screen_probe_results(std::uint32_t channel, ScreenProbe* probes, std::uint32_t capacity) {
    if (channel >= kScreenProbeChannels) {
        return 0u;
    }
    const std::lock_guard guard(probe_answers_lock_);
    const ScreenProbeAnswers& answers = probe_answers_[channel];
    const std::uint32_t count = std::min(answers.count, capacity);
    std::memcpy(probes, answers.probes.data(), count * sizeof(ScreenProbe));
    return count;
}

void RenderCore::record_screen_probes(const shapes::Record& record) {
    const auto channel = static_cast<std::uint32_t>(record.key);
    if (channel >= kScreenProbeChannels || record.vertex_count == 0u || probe_flights_active_ == probe_flights_.size()) {
        return;
    }
    ScreenProbeFlight& flight = probe_flights_[(probe_flight_first_ + probe_flights_active_) % probe_flights_.size()];
    const std::span<const ScreenProbe> probes(current_record_pool_->probes.data() + record.vertex_first, record.vertex_count);
    flight.kind = record.marker_aux != 0u ? ScreenProbeKind::Depth : ScreenProbeKind::Alpha;
    flight.slot = (probe_flight_first_ + probe_flights_active_) % probe_flights_.size();
    if (renderer_.record_screen_probes(flight.slot, flight.kind, probes)) {
        flight.channel = channel;
        flight.count = record.vertex_count;
        std::copy(probes.begin(), probes.end(), flight.probes.begin());
        ++probe_flights_active_;
    }
}

void RenderCore::poll_screen_probes() {
    while (probe_flights_active_ != 0u) {
        ScreenProbeFlight& flight = probe_flights_[probe_flight_first_];
        const std::uint8_t* const rgba = renderer_.screen_probes_finished(flight.slot);
        if (rgba == nullptr) {
            return;
        }
        for (std::uint32_t i = 0; i < flight.count; ++i) {
            const std::uint8_t* const pixel = rgba + std::size_t{i} * 4u;
            flight.probes[i].value = flight.probes[i].x < 0.0f ? 0.0f
                : flight.kind == ScreenProbeKind::Depth
                    ? static_cast<float>((std::uint32_t{pixel[0]} << 16u) | (std::uint32_t{pixel[1]} << 8u) | pixel[2]) / 16777215.0f
                    : static_cast<float>(pixel[3]) / 255.0f;
        }
        {
            const std::lock_guard guard(probe_answers_lock_);
            ScreenProbeAnswers& answers = probe_answers_[flight.channel];
            answers.count = flight.count;
            std::copy_n(flight.probes.begin(), flight.count, answers.probes.begin());
        }
        probe_flight_first_ = (probe_flight_first_ + 1u) % probe_flights_.size();
        --probe_flights_active_;
    }
}

void RenderCore::present_vi_black(bool in_frame) {
    const DisplayTexture* layout = display_pictures_.latest();
    if (layout == nullptr || (black_shown_ && !native_settings_overlay_visible())) {
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
    black_shown_ = true;
    // The frame shown next is new to the display, even when it repeats the last display picture.
    last_presented_display_serial_.store(~std::uint64_t{0}, std::memory_order_relaxed);
}

void RenderCore::present_latest_frame_on_thread() {
    present_cached_display_on_thread(false, true, false);
}

void RenderCore::present_cached_display_on_thread(
    bool black,
    bool sleep_if_missing,
    bool count_game_display_present,
    std::optional<std::uint32_t> slot) {
    if (count_game_display_present) {
        ++stat_display_present_requests_;
    }
    const std::uint64_t current_present_epoch =
        present_epoch_ + (count_game_display_present ? 1u : 0u);
    if (black) {
        if (count_game_display_present) {
            present_epoch_ = current_present_epoch;
            present_vi_black(false);
        }
        return;
    }
    black_shown_ = false;
    const DisplayTexture* selected = slot ? display_pictures_.picture(*slot) : display_pictures_.latest();
    if (selected == nullptr) {
        if (sleep_if_missing) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return;
    }
    const bool duplicate_stamp =
        selected->copy_serial ==
        last_presented_display_serial_.load(std::memory_order_relaxed);
    const bool skip_duplicate_present =
        count_game_display_present &&
        duplicate_stamp &&
        !native_settings_overlay_visible();
    if (count_game_display_present && !skip_duplicate_present &&
        !native_settings_overlay_visible() && skip_for_present_rate()) {
        return;
    }
    if (skip_duplicate_present) {
        record_skipped_duplicate_display_present(*selected);
        return;
    }
    const std::string snapshot_path = !count_game_display_present ? std::string{} : present_snapshot_path();
    if (!snapshot_path.empty()) {
        renderer_.set_debug_capture_paths({}, snapshot_path);
        renderer_.set_capture_waits(true);
    }
    const bool game_frame_only =
        interp_recording_ && count_game_display_present && !interp_presenting_real_ && !duplicate_stamp;
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
        // Present-only frames acquire no display picture textures, so DisplayPictures retirement advances with
        // render frames only; otherwise render-then-present would age pictures twice per frame.
        (void)renderer_.present(selected, !snapshot_path.empty());
    }
    if (!snapshot_path.empty()) {
        renderer_.set_debug_capture_paths({}, {});
        renderer_.set_capture_waits(false);
    }
    if (!replayed) {
        renderer_.end_frame();
    }
    if (count_game_display_present) {
        present_epoch_ = current_present_epoch;
        record_presented_display_copy(*selected);
        last_presented_slot_.store(selected->slot, std::memory_order_relaxed);
    }
    renderer_.debug_log_backbuffer_readback();
}

void RenderCore::render_frame_on_thread(FrameChunk& chunk) {
    // GALAXY_TEST_GPU_LOST_FRAME=n (with GALAXY_TEST_GPU_LOST_TIMES, default 1, processes): the card is lost at frame n.
    static const std::uint64_t lose_at = ::galaxy::run_option_u64(::galaxy::run_options().test_gpu_lost_frame, 0u);
    if (lose_at != 0u && frame_index_ == lose_at) [[unlikely]] {
        if (read_env_u64("NEBULA_GPU_RESTARTS", 0u) < ::galaxy::run_option_u64(::galaxy::run_options().test_gpu_lost_times, 1u)) {
            renderer_.test_lose_device();
        }
    }
    pipeline_cache_.merge_prepared();
    render_pointer_response_=chunk.pointer_response;
    render_movie_content_=chunk.prerecorded_movie;
    render_movie_frame_=chunk.movie_frame;
    frame_texture_decode_join_us_ = 0;
    pending_draw_batch_ = {};
    const RenderServices* services = chunk.services;

    // begin_frame() drains PSO completions and rewinds ring segments.
    {
        const cadence::ScopedPhaseTimer render_begin_timer{
            cadence_diagnostics_, cadence::TimingPhase::RenderBegin};
        {
            // GALAXY_TEST_RT_EFFECT_AB: the frame's parity by the present it ends in (the snapshots' numbering, as
            // GALAXY_RT_AB), set for all its chunks before anything asks rt_effect_on.
            set_rt_effect_frame(g_snapshot_presents + 1u);
            const RenderConfig render = get_render_config();
            const bool enhanced = render.enhanced_lighting;
            const bool per_pixel = render.per_pixel_lighting;
            curved_refine_ = render.curved_surfaces && curved_surfaces_offered();
            ambient_occlusion_ =
                render.ambient_occlusion && renderer_.ambient_occlusion_ready();
            // A lighting-mode change re-keys every lit draw's pipeline.
            if (per_pixel != per_pixel_lighting_ || enhanced != enhanced_lighting_) {
                per_pixel_lighting_ = per_pixel;
                enhanced_lighting_ = enhanced;
                shader_key_hashes_valid_ = false;
                current_pipeline_ = nullptr;
            }
        }
        renderer_.begin_frame(chunk.present_swap_chain);
        display_copied_in_chunk_ = false;
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
                interp_recording_ || renderer_.temporal_mode();
            if (draw_identity_wanted_.load(std::memory_order_relaxed) != wanted) {
                draw_identity_wanted_.store(wanted, std::memory_order_relaxed);
            }
            // Whether the renderer's bloom is built (NEBULA_OPTION_BLOOM): without it, no bloom.
            bloom_route_.store(renderer_.game_bloom_ready() ? 1u : 0u, std::memory_order_relaxed);
            interp_scenes_in_chunk_ = 0u;
        }
        // begin_frame() applies a pending internal-resolution change, so copies take the scale the scene target
        // has now, not the setting, which can change in between.
        if (renderer_.frame_map() != display_pictures_.frame_map()) {
            display_pictures_.set_frame_map(renderer_.frame_map());
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
        display_pictures_.begin_frame(renderer_.frame_slot(), kFramesInFlight);
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
        // New decoded textures upload through this frame's command list.
        renderer_.attach_texture_uploads(texture_cache_);
        {
            const RenderConfig render = get_render_config();
            if (const std::uint64_t generation = texture_pack_mod_folders_generation();
                generation != texture_pack_mods_generation_) {
                texture_pack_mods_generation_ = generation;
                texture_pack_mod_folders_ = texture_pack_mod_folders();
                texture_pack_mod_categories_off_ = texture_pack_mod_categories_off();
            }
            static const std::filesystem::path dumps = texture_dump_directory();
            TexturePackSettings settings{};
            settings.enabled = render.texture_packs;
            settings.preload = render.texture_pack_preload;
            settings.dump = render.texture_dump;
            // Each mod's pack, the lowest in the mods list first (the last wins a texture).
            if (std::filesystem::path packs = texture_pack_test_directory(); !packs.empty()) {
                settings.packs.push_back(std::move(packs));
                settings.pack_categories_off.push_back(0u);
            }
            settings.packs.insert(settings.packs.end(), texture_pack_mod_folders_.begin(),
                                  texture_pack_mod_folders_.end());
            settings.pack_categories_off.insert(settings.pack_categories_off.end(),
                                                texture_pack_mod_categories_off_.begin(),
                                                texture_pack_mod_categories_off_.end());
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
            const std::uint64_t planes[3] = {render_movie_frame_.y, render_movie_frame_.u, render_movie_frame_.v};
            const auto stale = [&](const auto& item) {
                return item.first.address == planes[0] || item.first.address == planes[1] ||
                       item.first.address == planes[2] || texture_cache_.movie_handle(item.second.srv_index);
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
    // GPU addresses and tables belong to the ring segment begin_frame() selected, so every binding
    // is written again in this frame slot even when the state did not change.
    frame_bindings_dirty_ = true;
    current_matrix_palette_ = 0;
    matrix_palette_stale_ = true;
    inline_matrices_stale_ = true;
    // The matrix ring segment for this frame slot is reused from the previous round trip, so a row
    // appended then is not at a known offset: the frame's first table appends every row again.
    matrix_blocks_valid_ = false;
    scene_chunk_restart_ = true;

    // Bind scene target with default (null) viewport. The viewport
    // will provide the actual values once its records run.
    renderer_.bind_scene();

    // game keeps the scene target across frames; only an scene target copy with the clear bit set clears it (on_frame_output,
    // with the copy's clear values). One initial clear (depth = far) seeds the targets.
    if (!scene_seeded_) {
        scene_seeded_ = true;
        renderer_.clear_scene(0u, true, true, true);
    }

    const std::uint64_t diagnostic_parse_start_ns =
        cadence::steady_now_ns_if_enabled(cadence_diagnostics_);
    if (!chunk.shape_records.empty() || !chunk.stripe_particles.empty()) {
        frame_services_ = services;
        // The images are the game's handed-over copies, unchanged while they are read: first-use decodes may run
        // on workers.
        texture_cache_.set_async_decode_allowed(true);
        try {
            parse_stripes_ = interp_recording_ ? std::span<const StripeParticleMark>(chunk.stripe_particles)
                                               : std::span<const StripeParticleMark>{};
            parse_stripe_next_ = 0u;
            current_record_pool_ = &chunk.record_pool;
            for (const shapes::Record& record : chunk.shape_records) {
                on_shape_record(record);
            }
            parse_stripes_ = {};
            if (!chunk.stripe_particles.empty()) [[unlikely]] {
                note_stripe_particles(chunk);
            }
        } catch (...) {
            texture_cache_.set_async_decode_allowed(false);
            texture_cache_.finish_pending_decodes_noexcept();
            frame_services_ = nullptr;
            throw;
        }
        texture_cache_.set_async_decode_allowed(false);
        frame_services_ = nullptr;
    }
    if (cadence_diagnostics_ != nullptr) {
        const std::uint64_t diagnostic_parse_end_ns =
            cadence::steady_now_ns_if_enabled(cadence_diagnostics_);
        cadence_diagnostics_->record_phase(
            cadence::TimingPhase::RenderRecords,
            diagnostic_parse_end_ns >= diagnostic_parse_start_ns
                ? diagnostic_parse_end_ns - diagnostic_parse_start_ns
                : 0u);
    }

    {
        const cadence::ScopedPhaseTimer post_parse_flush_timer{
            cadence_diagnostics_,
            cadence::TimingPhase::RenderPostParseFlush};
        flush_pending_draw_batch();
    }

    // The chunk with a game frame's end presents its picture (present_on_copy_enabled); a present between frames
    // shows the newest. Not while the output is black, the settings overlay draws over every present, or presents are paced elsewhere
    // (interpolation, a temporal upscaler, frame generation).
    const bool copy_present = !chunk.present_swap_chain && display_copied_in_chunk_ &&
        frame_end_presents_.load(std::memory_order_relaxed) &&
        !interp_recording_ && !black_shown_ && !native_settings_overlay_visible() && !renderer_.temporal_mode() &&
        !frame_generation_running();
    const bool presents = chunk.present_swap_chain || copy_present;
    if (presents) {
        ++stat_display_present_requests_;
    }
    const std::uint64_t current_present_epoch =
        presents ? present_epoch_ + 1u : present_epoch_;
    const DisplayTexture* const selected_display = chunk.black ? nullptr : display_pictures_.latest();
    const bool duplicate_display_present =
        selected_display != nullptr &&
        selected_display->copy_serial ==
            last_presented_display_serial_.load(std::memory_order_relaxed);
    const bool skip_duplicate_present =
        presents &&
        duplicate_display_present &&
        !native_settings_overlay_visible();
    const bool skip_rate_present =
        presents &&
        selected_display != nullptr &&
        !skip_duplicate_present &&
        !native_settings_overlay_visible() &&
        skip_for_present_rate();
    bool present_swap_chain =
        presents && selected_display != nullptr &&
        !skip_duplicate_present && !skip_rate_present;
    const bool present_black = chunk.present_swap_chain && chunk.black;
    if (present_black) {
        supersede_interp_sequence();
        interp_log_waiting_ = false;
    } else if (presents) {
        black_shown_ = false;
    }
    if (interp_recording_) {
        note_interp_log_completion();
        // The ready frame's own present: its sub-frames come first, the real
        // frame after them (started by the render loop after this chunk).
        // As on the present path, a present while a completed log waits
        // starts that log's sequence, even one the duplicate or rate checks
        // would drop: the frame clock may still name the previous display picture (the game flips
        // at the next retrace), and the waiting frame would be skipped when
        // the next one closes first.
        if (interp_log_waiting_ && chunk.present_swap_chain && selected_display != nullptr &&
            renderer_.replay_available()) {
            interp_start_pending_ = true;
            present_swap_chain = false;
        } else if (interp_sequence_.active && present_swap_chain) {
            // Another frame presents now: the playing one is superseded.
            supersede_interp_sequence();
        }
        if (present_swap_chain && !duplicate_display_present) {
            note_interp_real_present();
        }
    }
    if (present_swap_chain) {
        if (copy_present) {
            renderer_.wait_for_frame_latency_before_present();
            copy_presented_serial_ = selected_display->copy_serial;
        }
        const std::string snapshot_path = present_snapshot_path();
        if (!snapshot_path.empty()) {
            renderer_.set_debug_capture_paths({}, snapshot_path);
            renderer_.set_capture_waits(true);
        }
        renderer_.present(selected_display, !snapshot_path.empty());
        if (!snapshot_path.empty()) {
            renderer_.set_debug_capture_paths({}, {});
            renderer_.set_capture_waits(false);
        }
    } else if (present_black) {
        present_vi_black(true);
    }
    // A present naming the frame its copy already presented skipped nothing.
    if (skip_duplicate_present && selected_display->copy_serial != copy_presented_serial_) {
        record_skipped_duplicate_display_present(*selected_display);
    }
    struct SubmissionContext {
        RenderCore* backend;
        FrameChunk* chunk;
    } submission_context{this, &chunk};
    // Every staging byte the command list copies from must be written.
    {
        texture_cache_.finish_pending_decodes();
    }
    (void)renderer_.end_frame(
        present_swap_chain,
        [](void* opaque, std::uint64_t fence_value) {
            auto& context = *static_cast<SubmissionContext*>(opaque);
            (void)context;
            galaxy::timeline::record(galaxy::timeline::Event::RenderSubmitted, 0u, fence_value);
        },
        &submission_context);
    if (presents && present_swap_chain &&
        selected_display != nullptr) {
        present_epoch_ = current_present_epoch;
        record_presented_display_copy(*selected_display);
        last_presented_slot_.store(selected_display->slot, std::memory_order_relaxed);
    } else if (present_black) {
        present_epoch_ = current_present_epoch;
    }
    // Nonblocking: a later frame writes a finished present snapshot.
    renderer_.debug_log_backbuffer_readback();

    ++frame_index_;

}

RenderCore::TextureBindingKey RenderCore::capture_texture_binding_key(
    std::uint8_t map_mask) const {
    TextureBindingKey key{};
    key.map_mask = map_mask;
    key.scene_bias_x32 = scene_mip_bias_x32_;
    for (unsigned m = 0; m < kMaxTextureMaps; ++m) {
        if ((map_mask & (1u << m)) == 0u) {
            continue;
        }
        key.maps[m] = draw_state_.texture[m];
    }
    return key;
}

RenderCore::TextureHandleKey RenderCore::texture_handle_key(std::uint64_t texture, const TextureSampler& mode) const {
    const TextureInfo image = texture_cache_.info_of(texture);
    unsigned levels = 1u;
    bool generated_mips = false;
    const unsigned mip_mode = min_filter_code(mode) & 0x3u;
    const unsigned full_chain =
        snapshot_full_mip_chain_levels(image.width, image.height);
    if (mip_mode != 0u) {
        const unsigned wanted =
            1u + (static_cast<unsigned>(lod_x16(mode.max_lod)) + 15u) / 16u;
        levels = std::min(wanted, full_chain);
    } else if (snapshot_should_generate_native_mips(image, mode)) {
        levels = full_chain;
        generated_mips = levels > 1u;
    }
    return TextureHandleKey{
        image.key,
        image.width,
        image.height,
        image.layout,
        static_cast<std::uint8_t>(std::min<unsigned>(levels, 255u)),
        static_cast<std::uint8_t>(generated_mips ? 1u : 0u),
    };
}

std::array<std::uint32_t, kMaxTextureMaps> RenderCore::capture_sampler_keys(
    std::uint8_t map_mask,
    const std::array<TextureHandle, kMaxTextureMaps>& handles) const {
    std::array<std::uint32_t, kMaxTextureMaps> keys{};
    const auto mip_lines_log2_x32 = static_cast<std::uint8_t>(std::clamp(
        std::lround(32.0 * std::log2(std::max(1.0, static_cast<double>(renderer_.frame_map().height) / kAuthoredLines))), 0l, 127l));
    for (unsigned map = 0u; map < kMaxTextureMaps; ++map) {
        if ((map_mask & (1u << map)) == 0u) {
            continue;
        }
        const TextureHandle& handle = handles[map];
        keys[map] = pack_sampler_key(
            draw_state_.texture[map].mode, handle.generated_mips, handle.mip_levels,
            handle.lod_shift_x16, handle.lod_arbitrary, mip_lines_log2_x32, scene_mip_bias_x32_);
    }
    // Unused slots still get valid zero-key descriptors, but cannot fragment
    // the table cache. map_mask includes both direct and indirect sampling.
    return keys;
}

void RenderCore::clear_frame_texture_binding_table_cache() {
    frame_texture_tables_.clear();
    frame_texture_binding_tables_.clear();
    frame_texture_handle_cache_.clear();
    // Dropping the tables invalidates the remembered binding key with them, so flush_draw_state can
    // trust a valid key without re-deriving it from the state.
    current_texture_binding_key_valid_ = false;
}

void RenderCore::clear_texture_binding_table_cache() {
    clear_frame_texture_binding_table_cache();
    // frame_texture_binding_tables_ own CPU descriptors valid for the current frame only;
    // texture_handle_cache_ is the cache that persists across frames.
    dirty_texture_aliases_.clear();
}

void RenderCore::invalidate_texture_handle_cache_dependency(
    const TextureBindingDependency& dependency) {
    // A texture is its key: the handles made for that key go.
    const auto erase_matching = [&dependency](auto& cache) {
        for (auto it = cache.begin(); it != cache.end();) {
            if (it->first.address == dependency.address) {
                it = cache.erase(it);
            } else {
                ++it;
            }
        }
    };
    erase_matching(texture_handle_cache_);
    erase_matching(frame_texture_handle_cache_);
}

void RenderCore::invalidate_texture_binding_cache_key(std::uint64_t key) {
    const auto erase_matching_handles = [key](auto& cache) {
        for (auto it = cache.begin(); it != cache.end();) {
            if (it->first.address == key) {
                it = cache.erase(it);
            } else {
                ++it;
            }
        }
    };
    const auto erase_matching_tables = [key](auto& cache) {
        for (auto it = cache.begin(); it != cache.end();) {
            const TextureBindingTables& tables = it->second;
            bool names_key = false;
            for (unsigned i = 0; i < tables.dependency_count && !names_key; ++i) {
                names_key = tables.dependencies[i].address == key;
            }
            if (names_key) {
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

void RenderCore::mark_texture_binding_alias_dirty(
    const FramePassRequest& params) {
    DirtyTextureAlias dirty{};
    dirty.dependency.address = params.key;
    dirty.dependency.source_bytes = 1u;  // the picture is found by its key (the capture buffer) alone
    dirty_texture_aliases_.push_back(dirty);
    invalidate_texture_handle_cache_dependency(dirty.dependency);
}

void RenderCore::prune_dirty_texture_binding_tables() {
    if (dirty_texture_aliases_.empty()) {
        return;
    }
    const auto dependencies_overlap = [](const TextureBindingDependency& a, const TextureBindingDependency& b) {
        return a.address == b.address;  // a texture is its key
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
                it = cache.erase(it);
            } else {
                ++it;
            }
        }
    };
    prune_cache(frame_texture_binding_tables_);
    dirty_texture_aliases_.clear();
}

gpu::Pipeline* RenderCore::flush_draw_state(
    gpu::TopologyType primitive_topology_type) {
    // Always counted: the denominator of every *_us field.
    if (screen_run_ && !frame_scene_seen_ && !screen_frame_open_ && interp_recording_ && !history_scene_) [[unlikely]] {
        begin_screen_frame();
    }
    const auto topology_key =
        static_cast<std::uint8_t>(primitive_topology_type);
    // Every draw takes its material's native shader (E-04); one without a native material is not drawn. The count of
    // those in the log every 65,536 (GALAXY_LOG_NATIVE_MATERIALS=1).
    if (native_material_ == nullptr) [[unlikely]] {
        galaxy::loss::note(galaxy::loss::Kind::DrawNoMaterial);
        return nullptr;
    }
    // Consume once, propagating upload invalidation before considering reuse.
    // What changed: the typed state's flags, the register file's for matrix memory (until the matrix arena).
    const std::uint32_t dirty =
        draw_state_.consume_dirty() | (matrices_.consume_any_change() ? kStateDirtyMatrices : 0u);
    // Matrix words do not affect the shader/render keys, but they invalidate both upload snapshots.
    // Propagate this before considering reuse: a prior draw may have cleared the flags before
    // LOAD_XF/LOAD_INDX changed the bank.
    const bool mtx_dirty = (dirty & kStateDirtyMatrices) != 0;
    if (mtx_dirty) {
        matrix_palette_stale_ = true;
        inline_matrices_stale_ = true;
    }
    constexpr std::uint32_t kDirtySatisfiedByCachedUploads =
        kStateDirtyMatrices;
    const bool dirty_needs_body =
        (dirty & ~kDirtySatisfiedByCachedUploads) != 0u;
    // The draw's vertex inputs: a native shape record's, else none (an immediate draw's vertices carry no matrix
    // indices). Another source (a record of another model, or an immediate after a record) rebuilds the keys that
    // depend on it.
    const std::uint32_t matrix_bits = record_inputs_ != nullptr ? record_inputs_->matrix_bits : 0u;
    const std::uint64_t vertex_source =
        record_inputs_ != nullptr
            ? ((record_inputs_->positions * 0x9E3779B97F4A7C15ull) ^ record_inputs_->colors ^
               (static_cast<std::uint64_t>(matrix_bits) << 1u) ^ 1u)
            : static_cast<std::uint64_t>(matrix_bits) << 1u;
    const bool vertex_source_changed = vertex_source != vertex_source_;
    vertex_source_ = vertex_source;
    if (!dirty_needs_body &&
        !vertex_source_changed &&
        !texture_bindings_dirty_ &&
        !frame_bindings_dirty_ &&
        (!matrix_palette_stale_ || !current_palette_required_) &&
        !inline_matrices_stale_ &&
        current_pipeline_ != nullptr &&
        current_vs_constants_ != 0 &&
        current_ps_constants_ != 0 &&
        (current_texture_map_mask_ == 0u ||
         current_texture_table_.ptr != 0) &&
        render_state_key_.primitive_topology == topology_key) {
        return current_pipeline_;
    }


    // Rebuild canonical keys only for their dirty inputs; hash only changed
    // keys. Uniform updates still upload their constants below.
    const bool rs_dirty   = (dirty & kStateDirtyRenderState)  != 0;
    const bool xf_dirty   = (dirty & kStateDirtyVertexShader)    != 0;
    const bool vcd_dirty  = dirty == ~0u;  // the vertex inputs' layout: only after a reset
    const bool vs_c_dirty = (dirty & kStateDirtyVertexConstants) != 0;

    // The maps the native material samples.
    current_texture_map_mask_ = native_texture_mask_;
    current_texture_map_mask_valid_ = true;
    const std::uint8_t texture_map_mask = current_texture_map_mask_;
    // TextureBindingKey depends only on the map mask and the sampled maps' images, modes and palettes, whose
    // changes set kDirtyTex, so the last key is reused until one of them changes (dropping the table cache also
    // invalidates it).
    const bool texture_binding_key_fresh =
        current_texture_binding_key_valid_ &&
        dirty != ~0u &&
        (dirty & kStateDirtyTex) == 0u &&
        !texture_bindings_dirty_ &&
        texture_map_mask == current_texture_binding_key_mask_;
    TextureBindingKey texture_binding_key = texture_binding_key_fresh
        ? current_texture_binding_key_
        : capture_texture_binding_key(texture_map_mask);
    lit_translucent_ = false;
    lit_late_ = false;
    // A dirty input requires a fresh capture, but writes to unused maps,
    // image1/2 or restored register values can leave the effective key equal.
    // Explicit frame/resource invalidation still forces work through tex_dirty.
    const bool texture_binding_changed =
        !current_texture_binding_key_valid_ ||
        (!texture_binding_key_fresh &&
         !(texture_binding_key == current_texture_binding_key_)) ||
        texture_binding_key.lit_translucent != current_texture_binding_key_.lit_translucent;
    const bool tex_dirty = frame_bindings_dirty_ ||
        texture_bindings_dirty_ || texture_binding_changed;

    if (rs_dirty) {
        render_state_key_ = for_device_blending(build_render_state_key(draw_state_));
    }
    // The scene target's variant (its samples per pixel, MSAA, the output scene target has one; the HDR scene scene target) decides the
    // pipeline's too.
    const std::uint8_t samples_log2 = renderer_.scene_variant();
    render_state_key_.primitive_topology = topology_key;
    render_state_key_.samples_log2 = samples_log2;
    // Motion writers: opaque triangles with color writes in a temporal scene
    // (blending, logic ops and dual-source alpha cannot share a second
    // target). Keys only change while a motion scene is (or was) active.
    // Blended triangles (no dual-source alpha) write the reactive mask.
    // The game's scene runs on an 8-bit scene target (MainLoopFramework::preGX), where every blend that reads the source alpha
    // is dual-source (needs_dual_src_alpha): it blends at the material's 8-bit alpha, not the scene target's 6-bit one, and under
    // constant destination alpha (the state: the translucent lists, Mario, shadows) writes the constant instead.
    // Dual-source blending binds no second target, so none of those effects wrote the reactive mask (DLSS's, and the
    // water's and clear surfaces' cover: a fall's spray was painted over). In a temporal scene such a draw blends
    // single-source instead, at the same 8-bit alpha (ShaderGenerator's reactive variant), and writes the mask; a
    // constant destination alpha of 0 (every one the game sets) its alpha channel's own blend writes
    // (kRenderStateZeroAlpha). Another constant stays dual-source.
    bool motion_key_changed = false;
    if (motion_scene_ || motion_bits_set_) [[unlikely]] {
        const BlendMode blend_mode = draw_blend();
        const auto src_alpha_factor = [](BlendFactor f) {
            return static_cast<std::uint32_t>(f) == 4u || static_cast<std::uint32_t>(f) == 5u;
        };
        const bool constant_alpha = blend_mode.const_alpha_enable && blend_mode.alpha_update;
        const bool single_source =
            blend_mode.blend_enable && !blend_mode.subtract &&
            (src_alpha_factor(blend_mode.src_factor) || src_alpha_factor(blend_mode.dst_factor)) &&
            draw_state_.frame_alpha && (!constant_alpha || blend_mode.const_alpha == 0u);
        const std::uint32_t dual_bit = motion_scene_ && single_source ? 0u : (1u << 27);
        std::uint32_t bb = render_state_key_.blend_bits;
        const bool candidate = motion_scene_ &&
            topology_key == static_cast<std::uint8_t>(gpu::kTopologyTypeTriangle) &&
            (bb & ((1u << 8) | dual_bit)) == 0u && (bb & (1u << 25)) != 0u;
        // A pass over the screen (no depth test: the fill between the scene's opaque models and its shadows) is no surface:
        // it leaves the motion the surfaces wrote.
        const bool writer = candidate && (bb & 1u) == 0u && (render_state_key_.zmode_bits & 1u) != 0u;
        const bool reactive = candidate && (bb & 1u) != 0u;
        // Dual-source and constant alpha as the key builders made them, but for a reactive draw drawn single-source.
        if (single_source) {
            const bool dual = !reactive && dual_source_blending();
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
        }
        // The effects layer (RendererD3D12::begin_layer_window): a reactive draw (single-source) whose factors never
        // read the scene target (its colour or, on an scene target with alpha, its alpha) blends the same into the layer's targets.
        const auto reads_scene = [has_alpha = draw_state_.frame_alpha](BlendFactor f, bool source) {
            const auto v = static_cast<std::uint32_t>(f);
            (void)source;
            return v == 8u || v == 9u || (has_alpha && (v == 6u || v == 7u));  // the destination's colour or alpha
        };
        const bool layer = reactive && renderer_.layer_window() &&
                           (blend_mode.subtract ||
                            (!reads_scene(blend_mode.src_factor, true) && !reads_scene(blend_mode.dst_factor, false)));
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

    {
        // The native material's pipeline: its build-compiled shaders under the draw's render state.
        current_pipeline_ = pipeline_cache_.get_native(native_material_->shader, render_state_key_,
                                                       per_pixel_lighting_ || enhanced_lighting_);
        // GALAXY_LOG_NATIVE_MATERIALS=1: each native shader's pipelines as draws first take them.
        current_pso_key_valid_ = false;
    }
    if (current_pipeline_ == nullptr) {
        // The driver refused the pipeline in every form and no uber stand-in exists (the pipeline cache logged
        // why): the draw is skipped.
        galaxy::loss::note(galaxy::loss::Kind::DrawNoPipeline);
        return nullptr;
    }
    // An effects-layer draw binds the reactive mask and the layer's targets, uber stand-in or not (its variant writes
    // them); other stand-ins stay single-target.
    current_pipeline_aux_ = (render_state_key_.blend_bits & kRenderStateLayerTarget) != 0u ? 3u
        : (render_state_key_.blend_bits & kRenderStateMotionTarget) != 0u ? 1u
        : (render_state_key_.blend_bits & kRenderStateReactiveTarget) != 0u ? 2u : 0u;

    // A draw reads its matrices and lights from blocks in the matrix ring (E-05), one a kind (positions, textures,
    // normals, post-transform matrices, lights): a kind's block is appended when its rows changed, and the draw's
    // moved lights (the ray tracer's aim) take a light block of their own. A draw names its blocks by their first rows
    // (VertexConstants::pos_base, row_bases), valid until begin_frame() rewinds the ring.
    // A native material's shader may read any block (palette positions, normals, lights, per-vertex texture matrices).
    const bool palette_required = true;
    current_palette_required_ = palette_required;
    if (palette_required && (matrix_palette_stale_ || current_matrix_palette_ == 0)) {
        // Clock reads on detail frames only; the upload counts below are always kept.
        std::uint32_t kinds = matrices_.changed();
        if (!matrix_blocks_valid_ || renderer_.matrix_ring().epoch() != matrix_blocks_epoch_) {
            // Blocks from before the ring's latest rewind are another segment's: every kind is appended again.
            kinds = kAllMatrixKinds;
            matrix_blocks_epoch_ = renderer_.matrix_ring().epoch();
        }
        scene_kinds_pending_ |= kinds;
        if (kinds != 0u || matrix_blocks_forced_) {
            // The changed kinds' blocks, then the draw's moved lights (a light block of its own: the game's stay).
            constexpr bool moved = false;  // no moved light blocks (they were the ray tracer's)
            std::uint32_t rows = moved ? draw_rows::kLightRows : 0u;
            for (const DrawMatrices::Kind kind : kMatrixKinds) {
                rows += (kinds & kind) != 0u ? DrawMatrices::row_count(kind) : 0u;
            }
            const std::size_t bytes = static_cast<std::size_t>(rows) * 16u;
            // The moved lights going back to the game's need no block.
            UploadRing::Allocation mtx_alloc{};
            if (bytes != 0u) {
                mtx_alloc = renderer_.matrix_ring().allocate(bytes, /*alignment=*/256u);
            }
            const gpu::Address ring_base = renderer_.matrix_ring().gpu_base();
            std::uint32_t next = bytes != 0u ? static_cast<std::uint32_t>((mtx_alloc.gpu - ring_base) / 16u) : 0u;
            auto* out = reinterpret_cast<std::uint32_t*>(mtx_alloc.cpu);
            for (std::size_t k = 0; k < kMatrixKinds.size(); ++k) {
                const DrawMatrices::Kind kind = kMatrixKinds[k];
                if ((kinds & kind) == 0u) {
                    continue;
                }
                const std::uint32_t count = DrawMatrices::row_count(kind);
                std::memcpy(out, matrices_.rows(kind), count * 16u);
                out += 4u * count;
                matrix_block_base_[k] = next;
                next += count;
            }
            draw_light_base_ = matrix_block_base_[kLightKind];
            std::array<std::uint32_t, draw_rows::kLightRows * 4u> moved_lights{};
            current_matrix_palette_ = bytes != 0u ? mtx_alloc.gpu : current_matrix_palette_;
                if (interp_recording_ && history_scene_) [[unlikely]] {
                InterpScene& scene = interp_scenes_[interp_scene_];
                // The kinds that changed since the scene's last shadow (all of them for its first) join the scene's
                // rows; the shadow's table names each row's current one.
                const bool first_shadow = scene.palette_count == 0u;
                for (const DrawMatrices::Kind kind : kMatrixKinds) {
                    if (!first_shadow && (scene_kinds_pending_ & kind) == 0u) {
                        continue;
                    }
                    const std::uint32_t first = matrix_kind_first_row(kind);
                    const std::uint32_t count = DrawMatrices::row_count(kind);
                    const DrawMatrices::Row* source = matrices_.rows(kind);
                    for (std::uint32_t r = 0; r < count; ++r) {
                        scene_row_index_[first + r] = static_cast<std::uint32_t>(scene.palette_rows.size() / 4u);
                        scene.palette_rows.insert(scene.palette_rows.end(), source[r].begin(), source[r].end());
                    }
                }
                scene.palette_tables.insert(scene.palette_tables.end(), scene_row_index_.begin(),
                                            scene_row_index_.end());
                if (moved) {
                    // The moved lights' rows: this shadow's own, the game's stay for the next.
                    std::uint32_t* const table = scene.palette_tables.data() + scene.palette_count * draw_rows::kRows;
                    for (std::uint32_t r = 0; r < draw_rows::kLightRows; ++r) {
                        table[draw_rows::kLight + r] = static_cast<std::uint32_t>(scene.palette_rows.size() / 4u);
                        scene.palette_rows.insert(scene.palette_rows.end(), moved_lights.data() + 4u * r,
                                                  moved_lights.data() + 4u * r + 4u);
                    }
                }
                scene_kinds_pending_ = 0u;
                current_palette_shadow_ = static_cast<std::int32_t>(scene.palette_count++);
            }
            matrix_blocks_valid_ = true;
            matrix_blocks_forced_ = false;
            matrix_bases_changed_ = true;
            matrices_.clear_changed();
        }
        // Every chunk is valid in this frame slot, so current_matrix_palette_ already points into this
        // frame's segment.
        matrix_palette_stale_ = false;
    }
    // The rows' binding: the ring from its start (draws name their blocks' rows from it).
    draw_matrix_palette_ = current_matrix_palette_ != 0u ? renderer_.matrix_ring().gpu_base() : 0u;
    cpu_vs_constants_.pos_base = matrix_block_base_[0];
    cpu_vs_constants_.row_bases[0] = matrix_block_base_[2];
    cpu_vs_constants_.row_bases[1] = matrix_block_base_[1];
    cpu_vs_constants_.row_bases[2] = matrix_block_base_[3];
    cpu_vs_constants_.row_bases[3] = draw_light_base_;
    // The shape's per-vertex position matrix indices (VertexConstants::flags bit 0; the uber shaders take the key's
    // bit): the native vertex shaders and the ray tracer read the constants' bit, so a change uploads them again.
    const std::uint32_t per_vertex = matrix_bits & 1u;
    const bool per_vertex_changed = std::exchange(cpu_vs_constants_.per_vertex, per_vertex) != per_vertex;

    // A native material's draw: the shape's per-vertex texture matrix indices (texgen n's bit; native_material.hlsli
    // native_tex_matrix reads the vertex's index for those texgens).
    const std::uint32_t native_texmtx = (matrix_bits >> 1u) & 0xFFu;
    // The header word as the last upload wrote it (a draw's from its key): any change uploads again, or a native
    // draw after a game one would read that draw's mask.
    // A native draw's pixel engine word (native_material.hlsli native_ps): bit 8 constant destination alpha with the
    // alpha at bits 9-16, on a frame with alpha (as the uber shader's dst-alpha flag). Colour keeps 8 bits on every
    // frame, as the old path's keys (build_pixel_shader_key: no six-bit colour or ordered pattern).
    const BlendMode& pe_blend = draw_state_.blend;
    const bool frame_alpha = draw_state_.frame_alpha;
    const std::uint32_t native_pe = frame_alpha && pe_blend.const_alpha_enable && pe_blend.alpha_update &&
                                            (render_state_key_.blend_bits & kRenderStateZeroAlpha) == 0u
                                        ? 0x100u | static_cast<std::uint32_t>(pe_blend.const_alpha) << 9u
                                        : 0u;
    const std::uint32_t header_texmtx = native_texmtx | native_pe << 8u;
    const bool native_texmtx_changed = std::exchange(native_texmtx_, header_texmtx) != header_texmtx;
    // Upload VS constants when scalar state, fixed inline matrices or the matrix blocks change.
    if (native_texmtx_changed || per_vertex_changed || std::exchange(sky_up_dirty_, false) || vs_c_dirty ||
        xf_dirty || vcd_dirty || inline_matrices_stale_ ||
        frame_bindings_dirty_ || dirty == ~0u || std::exchange(matrix_bases_changed_, false) ||
        (history_scene_ && current_matrix_palette_ != temporal_vs_palette_va_)) {
        const VertexConstants& vs_c = cpu_vs_constants_.update(
            matrices_, draw_state_, dirty, RasterScale{renderer_.frame_map().sx, renderer_.frame_map().sy}, frame_bindings_dirty_);
        UploadRing::Allocation vs_alloc =
            renderer_.constant_ring().allocate(sizeof(VertexConstants), 256u);
        std::memcpy(vs_alloc.cpu, &vs_c, sizeof(vs_c));
        if (history_scene_) [[unlikely]] {
            write_scene_vs_constants(vs_c, *reinterpret_cast<VertexConstants*>(vs_alloc.cpu));
        }
        {
            // The per-vertex texture matrix bits and each texgen's post-transform row (| 0x100 normalize).
            auto* native_vs = reinterpret_cast<VertexConstants*>(vs_alloc.cpu);
            native_vs->draw_header[1] = renderer_.frame_map().height;
            native_vs->draw_header[2] = native_pe;
            native_vs->draw_header[3] = native_texmtx;
            for (unsigned i = 0; i < 8u; ++i) {
                native_vs->post_rows[i] = (draw_state_.post_texgen[i].matrix_index & 0x3Fu) |
                                                 (draw_state_.post_texgen[i].normalize ? 0x100u : 0u);
            }
        }
        current_vs_constants_ = vs_alloc.gpu;
        inline_matrices_stale_ = false;
    }

    // Texture tables are refreshed only when a texture input changed. tex_dirty covers every input:
    // the binding key is captured only under kDirtyTex or the two binding flags, so otherwise the
    // result would be the current tables. dirty_texture_aliases_ outlives a flush (cleared by
    // prune_dirty_texture_binding_tables()) and is honoured even without tex_dirty, or an scene target-alias
    // copy followed by matrix-only flushes would keep its entries for the rest of the frame. Most
    // flushes carry kDirtyXfMatrices alone (the game loads matrices per object with LOAD_INDX).
    if (tex_dirty || !dirty_texture_aliases_.empty()) {
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
                binding_cache_hit = true;
            }

            if (!binding_cache_hit) {
                std::array<TextureHandle, kMaxTextureMaps> texture_handles{};
                TextureBindingTables binding_tables{};
                TextureTableKey table_key{};
                table_key.map_mask = texture_map_mask;
                for (unsigned m = 0; m < kMaxTextureMaps; ++m) {
                    if ((texture_map_mask & (1u << m)) != 0u) {
                        const std::uint64_t texture = draw_state_.texture[m].texture;
                        const TextureSampler mode = draw_state_.texture[m].mode;
                        const TextureHandleKey handle_key = texture_handle_key(texture, mode);
                        if (renderer_.target_picture(texture, texture_handles[m])) {
                            // A target pass's picture (E-07) under its key.
                        } else if (const auto handle_it =
                                frame_texture_handle_cache_.find(handle_key);
                            handle_it != frame_texture_handle_cache_.end()) {
                            texture_handles[m] = handle_it->second;
                        } else {
                            if (const auto persistent_it =
                                    texture_handle_cache_.find(handle_key);
                                persistent_it != texture_handle_cache_.end()) {
                                texture_handles[m] = persistent_it->second;
                            } else {
                                texture_handles[m] = texture_cache_.get(texture, mode);
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
                                texture,
                                texture_handles[m].width,
                                texture_handles[m].height,
                                0u,
                                texture_handles[m].source_bytes,
                            };
                    }
                    const TextureHandle& th = texture_handles[m];
                    table_key.resources[m] =
                        reinterpret_cast<std::uintptr_t>(th.resource);
                    table_key.srv_indices[m] = th.srv_index;
                    table_key.widths[m] = th.width;
                    table_key.heights[m] = th.height;
                }
                if (const auto table_it =
                        frame_texture_tables_.find(table_key);
                    table_it != frame_texture_tables_.end()) {
                    current_texture_table_ = table_it->second;
                } else {
                    // Reserve one SRV slot per texture map regardless of
                    // stage count, so the root table has a stable size
                    // matching the root signature.
                    current_texture_table_ = renderer_.texture_table_for(
                        texture_handles.data(), kMaxTextureMaps, texture_cache_);
                    frame_texture_tables_.emplace(table_key, current_texture_table_);
                }

                // Per-map sampler state from TexMode0/1 (wrap, filters, LOD
                // bias and range).  Packed key layout documented at
                // RendererD3D12::sampler_table_for; tables are
                // cached for this frame's fence-owned sampler heap.
                const auto samp_keys = capture_sampler_keys(
                    texture_map_mask, texture_handles);
                current_sampler_table_ =
                    renderer_.sampler_table_for(samp_keys.data());
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
            }
        }
        current_texture_map_mask_ = texture_map_mask;
        // GALAXY_LOG_NATIVE_MATERIALS=1: each native shader's maps as the texture cache resolved them (once per map
        // and image).
        current_texture_binding_key_ = texture_binding_key;
        current_texture_binding_key_valid_ = true;
        current_texture_binding_key_mask_ = texture_map_mask;
    }

    {
        // The native material's constants (its MaterialConstants), uploaded once per material.
        if (native_constants_dirty_ || frame_bindings_dirty_ || current_ps_constants_ == 0u) {
            UploadRing::Allocation alloc =
                renderer_.constant_ring().allocate(sizeof(native_material_->constants), 256u);
            std::memcpy(alloc.cpu, native_material_->constants, sizeof(native_material_->constants));
            current_ps_constants_ = alloc.gpu;
            current_ps_record_ = -1;
            native_constants_dirty_ = false;
        }
    }

    frame_bindings_dirty_ = false;
    texture_bindings_dirty_ = false;
    // The frame's detail flag guards both endpoints. A zero on an ordinary
    // frame denotes an unavailable timing, not zero renderer work.
    return current_pipeline_;
}

bool RenderCore::draw_calls_batch_compatible(
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
        lhs.has_viewport != rhs.has_viewport) {
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
               lhs.viewport,
               rhs.viewport,
               sizeof(lhs.viewport)) == 0;
}

void RenderCore::flush_pending_draw_batch() {
    if (!pending_draw_batch_.active) {
        return;
    }

    if (!pending_stripe_vertices_.empty()) [[unlikely]] {
        renderer_.note_stripe_vertices(pending_stripe_vertices_);
        pending_stripe_vertices_.clear();
    }
    if (!pending_draw_batch_.hidden) {
        const DrawCall& call = pending_draw_batch_.call;
        renderer_.draw(call);
    }
    pending_draw_batch_ = {};
}

bool RenderCore::begin_cached_draw_run(
    Primitive primitive,
    std::uint8_t vtxfmt,
    std::size_t draw_count) {
    (void)draw_count;
    if (cached_draw_run_active_) {
        throw std::runtime_error(
            "RenderCore: nested cached draw run");
    }
    gpu::Topology topology{};
    gpu::TopologyType topology_type{};
    topology_for_primitive(topology, topology_type);

    const ScissorRect sc = draw_state_.scissor;
    // The reject flag also covers fully culled polygons; lines and points stay visible under
    // cull all.
    cached_draw_run_scissor_reject_ =
        cull_all_suppresses_primitive(draw_state_.gen.cull, primitive) ||
        sc.x1 < sc.x0 || sc.y1 < sc.y0 ||
        sc.x1 < 0 || sc.y1 < 0 ||
        sc.x0 >= static_cast<std::int32_t>(kFrameUnits) ||
        sc.y0 >= static_cast<std::int32_t>(kFrameUnits);
    cached_draw_run_primitive_ = primitive;
    cached_draw_run_vtxfmt_ = vtxfmt;
    cached_draw_run_primitive_indexed_ =
        primitive != Primitive::Triangles;
    cached_draw_run_no_pso_ = false;
    cached_draw_run_batch_compatible_known_ = false;
    cached_draw_run_state_call_ = {};
    cached_draw_run_active_ = true;
    if (cached_draw_run_scissor_reject_) {
        return true;
    }

    gpu::Pipeline* pso = nullptr;
    {
        pso = flush_draw_state(topology_type);
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
    cached_draw_run_state_call_.matrix_rows = renderer_.matrix_ring().gpu_base();
    cached_draw_run_state_call_.aux_target     = current_pipeline_aux_;
    cached_draw_run_state_call_.replay_record  = current_replay_record_;
    cached_draw_run_state_call_.ps_record      = current_ps_record_;
    cached_draw_run_state_call_.topology       = topology;
    cached_draw_run_state_call_.scissor        = scissor_rect;
    for (unsigned i = 0; i < 6; ++i) {
        cached_draw_run_state_call_.viewport[i] = draw_state_.viewport[i];
    }
    cached_draw_run_state_call_.has_viewport =
        cached_draw_run_state_call_.viewport[2] > 0.0f &&
        cached_draw_run_state_call_.viewport[3] > 0.0f;
    return true;
}

void RenderCore::end_cached_draw_run() {
    cached_draw_run_active_ = false;
    cached_draw_run_scissor_reject_ = false;
    cached_draw_run_primitive_indexed_ = false;
    cached_draw_run_no_pso_ = false;
    cached_draw_run_batch_compatible_known_ = false;
    cached_draw_run_primitive_ = Primitive::Points;
    cached_draw_run_vtxfmt_ = 0;
    cached_draw_run_state_call_ = {};
}

void RenderCore::on_shape_record(const shapes::Record& record) {
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
    case shapes::Record::Kind::Forget:
        for (auto it = shape_arrays_.begin(); it != shape_arrays_.end();) {
            it = it->first >= record.key && it->first < record.position ? shape_arrays_.erase(it) : std::next(it);
        }
        for (auto it = record_shapes_.begin(); it != record_shapes_.end();) {
            if (it->first >= record.key && it->first < record.position) {
                release_record_shape(it->second);
                it = record_shapes_.erase(it);
            } else {
                ++it;
            }
        }
        curved::forget_models(record.key, record.position);
        flush_pending_draw_batch();
        for (const std::uint64_t key : texture_cache_.forget_range(record.key, record.position)) {
            invalidate_texture_binding_cache_key(key);
        }
        // A map still set to an image of that memory (the state outlives the scene, and a first draw may set no texture) holds nothing now.
        for (TextureMap& map : draw_state_.texture) {
            if (map.texture >= record.key && map.texture < record.position) {
                map.texture = 0u;
            }
        }
        apply_native_material(nullptr);
        texture_bindings_dirty_ = true;
        current_texture_binding_key_valid_ = false;
        return;
    case shapes::Record::Kind::Draw:
        draw_state_.apply_pass(record.pass);
        draw_record_shape(record);
        return;
    case shapes::Record::Kind::NativeMaterial:
        apply_native_material(record.native_index != shapes::kNoNative ? &current_record_pool_->natives[record.native_index] : nullptr);
        return;
    case shapes::Record::Kind::State: {
        // A native material stays through state changes (the game folds a change of its values into it and ends it
        // before a change of its program: J3DNativeMaterial); its fixed-function state and textures take them here.
        for (std::uint32_t op_index = 0; op_index < record.op_count; ++op_index) {
            const state::Op& op = current_record_pool_->ops[record.op_first + op_index];
            if (op.kind == state::kLightAttenuation || op.kind == state::kLightPosition) {
                load_light(op);
            }
            draw_state_.apply(op);
        }
        return;
    }
    case shapes::Record::Kind::TextureImage:
        flush_pending_draw_batch();
        texture_cache_.register_image(record.key, record.image);
        // New bytes for that key: bindings made from what it held before are out of date too (a buffer the game
        // reuses for its next image, as the Mii library's part textures).
        if (record.image != nullptr) {
            invalidate_texture_binding_cache_key(record.key);
        }
        texture_bindings_dirty_ = true;
        current_texture_binding_key_valid_ = false;
        return;
    case shapes::Record::Kind::TextureHd:
        flush_pending_draw_batch();
        texture_cache_.register_replacement(record.key, record.image);
        invalidate_texture_binding_cache_key(record.key);
        texture_bindings_dirty_ = true;
        current_texture_binding_key_valid_ = false;
        return;
    case shapes::Record::Kind::Scene: {
        const auto marker = static_cast<SceneMarker>(static_cast<int>(record.key));
        if (marker == SceneMarker::Scene2DBegin) {
            on_scene_2d_begin();
        } else if (marker == SceneMarker::SceneOpaqueEnd) {
            apply_scene_ao(current_record_pool_->scenes[record.native_index].view);
        } else if (marker != SceneMarker::Scene2DEnd) {
            const shapes::SceneInfo& info = current_record_pool_->scenes[record.native_index];
            on_scene_marker(marker, info.view_valid ? &info.view : nullptr, info.sky_up, info.sky_up_valid, &info.stage);
        }
        return;
    }
    case shapes::Record::Kind::Immediate:
        apply_record_matrices(record);
        draw_state_.apply_pass(record.pass);
        draw_record_immediate(record);
        return;
    case shapes::Record::Kind::Marker:
        on_host_marker(static_cast<std::uint8_t>(record.key), static_cast<std::uint16_t>(record.marker_aux),
                       record.marker[0], record.marker[1]);
        return;
    case shapes::Record::Kind::Readback:
        // The picture here at the game's points.
        flush_pending_draw_batch();
        record_screen_probes(record);
        return;
    case shapes::Record::Kind::TargetBegin:
        begin_target_pass(record);
        return;
    case shapes::Record::Kind::TargetEnd:
        end_target_pass(record);
        return;
    case shapes::Record::Kind::FramePass: {
        FramePassRequest request{};
        request.key = record.key;
        request.channels = record.target_channels < 7u ? record.target_channels : 0u;
        on_frame_output(request);
        return;
    }
    case shapes::Record::Kind::FrameTake: {
        // In the settings that cannot swap the target (MSAA, HDR, interpolation), a pass over the frame.
        flush_pending_draw_batch();
        FramePassRequest request{};
        request.key = record.key;
        bool changed = false;
        if (renderer_.can_take_scene()) {
            const FrameMap& map = renderer_.frame_map();
            const auto width = static_cast<std::uint16_t>(map.width);
            const auto height = static_cast<std::uint16_t>(map.height);
            const bool forgot = texture_cache_.forget(record.key) != 0u;
            gpu::TextureRef released;
            changed = renderer_.take_scene(record.key, display_pictures_.take_idle_texture(width, height), texture_cache_, released);
            display_pictures_.release_texture(std::move(released), width, height);
            changed = changed || forgot;
            frame_bindings_dirty_ = true;
        } else {
            changed = renderer_.draw_frame_pass(request, texture_cache_);
        }
        if (changed) {
            mark_texture_binding_alias_dirty(request);
            texture_bindings_dirty_ = true;
        }
        return;
    }
    case shapes::Record::Kind::FrameShow: {
        FramePassRequest request{};
        request.display = true;
        on_frame_output(request);
        return;
    }
    case shapes::Record::Kind::FrameClear: {
        // The values as the game gave them (alpha 255 on a frame without alpha).
        flush_pending_draw_batch();
        renderer_.clear_scene(record.target_clear_rgba, (record.target_channels & 1u) != 0u,
                            (record.target_channels & 2u) != 0u, (record.target_channels & 4u) != 0u, record.target_x,
                            record.target_y, record.target_width, record.target_height,
                            frame_format());
        return;
    }
    case shapes::Record::Kind::TargetClear:
        if (renderer_.target_pass_active()) {
            flush_pending_draw_batch();
            renderer_.clear_scene(record.target_clear_rgba, (record.target_channels & 1u) != 0u,
                                (record.target_channels & 2u) != 0u, (record.target_channels & 4u) != 0u, 0u, 0u,
                                target_pass_size_[0], target_pass_size_[1], frame_format());
        }
        return;
    }
}

// A pass into a texture target (E-07): the draws until its end go to the target set, whose top left the pass clears
// as the game asked.
void RenderCore::begin_target_pass(const shapes::Record& record) {
    flush_pending_draw_batch();
    // The frame sampled in the pass: its pictures come first, while the frame is still the scene target.
    const bool over_frame = record.target_frame_key != 0u || record.target_depth_key != 0u;
    if (over_frame) {
        for (const std::uint64_t key : {record.target_frame_key, record.target_depth_key}) {
            if (key != 0u) {
                texture_cache_.forget(key);
                FramePassRequest alias{};
                alias.key = key;
                mark_texture_binding_alias_dirty(alias);
            }
        }
        renderer_.open_frame_for_pass(record.target_frame_key, record.target_channels, record.target_depth_key, texture_cache_);
        texture_bindings_dirty_ = true;
    }
    if (!renderer_.begin_target_pass(record.key, record.target_width, record.target_height)) {
        if (over_frame) {
            renderer_.close_frame_for_pass(texture_cache_);
        }
        static bool warned = false;
        if (!warned) {
            warned = true;
            std::fprintf(stderr, "[target] a pass into %llx could not begin (one is open, or no target)\n",
                         static_cast<unsigned long long>(record.key));
        }
        return;
    }
    target_pass_size_ = {record.target_width, record.target_height};
    frame_bindings_dirty_ = true;
    renderer_.clear_scene(record.target_clear_rgba, true, true, true, 0u, 0u,
                        record.target_width, record.target_height, frame_format());
}

// The pass's end: its picture, at full precision, becomes the texture at its key, then the frame's targets come
// back.
void RenderCore::end_target_pass(const shapes::Record& record) {
    if (!renderer_.target_pass_active()) {
        return;
    }
    flush_pending_draw_batch();
    renderer_.end_target_pass(record.target_channels, texture_cache_);
    frame_bindings_dirty_ = true;
    texture_bindings_dirty_ = true;
}

void RenderCore::apply_native_material(const material::Native* native) {
    if (native == nullptr) {
        end_native_material();  // NEBULA_SHADER_NONE: the game ended it
        return;
    }
    const material::Native& m = *native;
    if ((m.constants_only & 1u) == 0u) {
    }
    if ((m.constants_only & 1u) != 0u && native_material_ != nullptr && native_material_->shader == m.shader) {
        native_material_storage_ = m;
        native_material_ = &native_material_storage_;
        native_constants_dirty_ = true;
        return;
    }
    native_texture_mask_ = 0u;
    for (unsigned map = 0; map < 8u; ++map) {
        const auto& t = m.textures[map];
        if (t.image == 0u) {
            continue;
        }
        // The map as the native material names it: its image, size and format, and its sampling.
        TextureMap& out = draw_state_.texture[map];
        out.texture = t.image;
        out.mode.wrap_s = static_cast<TexWrap>(std::min<std::uint8_t>(t.repeat_s, 2u));
        out.mode.wrap_t = static_cast<TexWrap>(std::min<std::uint8_t>(t.repeat_t, 2u));
        out.mode.mag_linear = t.mag_linear != 0u;
        out.mode.min_linear = t.min_linear != 0u;
        out.mode.mip = static_cast<std::uint8_t>(std::min<std::uint8_t>(t.mip_filter, 2u));
        out.mode.lod_bias = t.lod_bias;
        out.mode.anisotropy = static_cast<std::uint8_t>(t.anisotropy & 3u);
        out.mode.min_lod = t.min_lod;
        out.mode.max_lod = t.max_lod;
        out.edge_lod = t.edge_lod != 0u;
        out.bias_clamp = false;
        native_texture_mask_ = static_cast<std::uint8_t>(native_texture_mask_ | (1u << map));
    }
    // Its texgens' post-transform matrices, as a material's texgens set them (a draw's own texgens replace them:
    // apply_record_matrices).
    for (unsigned i = 0; i < 8u; ++i) {
        if (m.texgen_post[i] == material::kNativeKept) {
            continue;  // the draw's texgens as they are (an instance's differed list that loads none)
        }
        draw_state_.post_texgen[i] = PostTexGen{static_cast<std::uint8_t>(m.texgen_post[i] & 0x3Fu),
                                                  (m.texgen_post[i] & 0x80u) != 0u};
    }
    draw_state_.dirty |= kStateDirtyVertexConstants;
    // The material's own lights (a J3D colour block that loads them) into the light rows its shader reads.
    for (unsigned n = 0; n < 8u; ++n) {
        if ((m.light_mask & (1u << n)) != 0u) {
            matrices_.set_light(n, m.light[n]);
        }
    }
    draw_state_.dirty |= kStateDirtyRenderState | kStateDirtyTex;
    native_material_storage_ = m;
    native_material_ = &native_material_storage_;
    native_constants_dirty_ = true;
    current_texture_map_mask_valid_ = false;
    texture_bindings_dirty_ = true;
}

void RenderCore::end_native_material() {
    if (native_material_ == nullptr) {
        return;
    }
    native_material_ = nullptr;
    // The state's pipeline, constants and textures are rebuilt at the next draw, and the next material loads
    // its whole program (the native material wrote blend, depth, culling, alpha test and textures).
    draw_state_.dirty |= kStateDirtyRenderState | kStateDirtyTex;
    current_pso_key_valid_ = false;
    current_texture_map_mask_valid_ = false;
    texture_bindings_dirty_ = true;
}

void RenderCore::release_record_shape(RecordShape& shape) noexcept {
    if (shape.resident_epoch != 0u && shape.resident_epoch == resident_vertices_.epoch()) {
        resident_vertices_.release(shape.resident_offset, shape.resident_units);
    }
    shape.resident_epoch = 0u;
}

void RenderCore::apply_record_matrices(const shapes::Record& record) {
    for (std::uint32_t matrix_index = 0; matrix_index < record.matrix_count; ++matrix_index) {
        const shapes::Matrix& m = current_record_pool_->matrices[record.matrix_first + matrix_index];
        switch (m.kind) {
        case shapes::MatrixKind::Position:
            matrices_.set_position(m.place, m.values.data(), m.model_matrix);
            break;
        case shapes::MatrixKind::Normal:
            matrices_.set_normal(m.place, m.values.data(), m.model_matrix);
            break;
        case shapes::MatrixKind::Texture:
            matrices_.set_texture(m.place, m.values.data(), 3u);
            break;
        case shapes::MatrixKind::Texture2x4:
            matrices_.set_texture(m.place, m.values.data(), 2u);
            break;
        case shapes::MatrixKind::PostTexture:
            matrices_.set_post(m.place, m.values.data());
            break;
        }
    }
    if (record.has_current) {
        const auto& c = record.current;
        const std::uint8_t before_position = draw_state_.position_matrix;
        std::uint8_t before_texture[8];
        std::memcpy(before_texture, draw_state_.texture_matrix, sizeof(before_texture));
        draw_state_.position_matrix = static_cast<std::uint8_t>(c[0] % 10u);
        for (unsigned i = 0; i < 8u; ++i) {
            draw_state_.texture_matrix[i] = static_cast<std::uint8_t>(10u + std::min<unsigned>(c[1u + i], 10u));
        }
        if (draw_state_.position_matrix != before_position ||
            std::memcmp(before_texture, draw_state_.texture_matrix, sizeof(before_texture)) != 0) {
            draw_state_.dirty |= kStateDirtyVertexConstants;
        }
    }
    for (std::uint8_t texgen_index = 0; texgen_index < record.texgen_count; ++texgen_index) {
        const auto& g = record.texgens[texgen_index];
        const TexGen gen = material::tex_gen(g[1], g[2]);
        const PostTexGen post = material::post_tex_gen(g[4], g[3]);
        if (!(draw_state_.texgen[g[0] & 7u] == gen) || !(draw_state_.post_texgen[g[0] & 7u] == post)) {
            draw_state_.texgen[g[0] & 7u] = gen;
            draw_state_.post_texgen[g[0] & 7u] = post;
            draw_state_.dirty |= kStateDirtyVertexShader | kStateDirtyVertexConstants;
        }
    }
}

void RenderCore::draw_record_shape(const shapes::Record& record) {
    apply_record_matrices(record);
    if (record.key == 0u) {
        return;  // state only (the matrices, current matrices and texgens set before a material record)
    }
    const auto found = record_shapes_.find(record.key);
    if (found == record_shapes_.end() || found->second.geometry == nullptr) {
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
        (geometry.attributes & shapes::kPositionMatrix) != 0u ? 0u : draw_state_.position_matrix);
    if (!ensure_resident_vertex_buffer()) {
        return;
    }
    const bool stale = !shape.expanded || shape.versions != versions || shape.position_matrix != position_matrix ||
                       shape.refined != curved_refine_;
    if (stale || shape.resident_epoch != resident_vertices_.epoch()) {
        // The model's analysis for its curved surfaces (its first draw waits for it).
        shape.curved = curved::find_model(keys[0]);
        if (!shapes::expand(geometry, arrays, position_matrix, shape.curved.get(), expand_scratch_) ||
            expand_scratch_.empty() || expand_scratch_.size() > 0x10000u) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                std::fprintf(stderr, "[native-shapes] group %llx reads an array the game never registered\n",
                             static_cast<unsigned long long>(record.key));
            }
            shape.expanded = false;
            return;
        }
        if (stale) {
            release_record_shape(shape);
            shape.expanded = true;
            shape.versions = versions;
            shape.position_matrix = position_matrix;
            shape.object_slots_built = false;
        }
        // No patch stages: the bending triangles refined here, once, for every draw of the shape.
        shape.refined = curved_refine_;
        shape.refined_indices.clear();
        const bool refine = curved_refine_ && !shape.refine_failed && shape.curved != nullptr && shape.curved->bends &&
                            (geometry.attributes & shapes::kOwnPositions) == 0u;
        if (refine) {
            std::vector<RenderVertex> refined;
            std::vector<std::uint16_t> refined_indices;
            if (shapes::refine_curved(expand_scratch_, geometry.indices, kCurveRefineTolerance, refined, refined_indices)) {
                expand_scratch_ = std::move(refined);
                shape.refined_indices = std::move(refined_indices);
            }
        }
        // The block holds the vertices and, behind them, the indices: both written once, for every draw of the shape.
        shape.resident_epoch = 0u;
        auto block_size = [&](std::uint32_t& vertices_units, std::uint32_t& bytes) {
            vertices_units = static_cast<std::uint32_t>(expand_scratch_.size());
            const std::size_t index_count = !shape.refined_indices.empty() ? shape.refined_indices.size() : geometry.indices.size();
            bytes = static_cast<std::uint32_t>(index_count * sizeof(std::uint16_t));
            return vertices_units + (bytes + static_cast<std::uint32_t>(sizeof(RenderVertex)) - 1u) / static_cast<std::uint32_t>(sizeof(RenderVertex));
        };
        std::uint32_t expanded_units = 0u;
        std::uint32_t index_bytes = 0u;
        std::uint32_t block_units = block_size(expanded_units, index_bytes);
        std::optional<std::uint32_t> offset = resident_vertices_.allocate(block_units);
        if (!offset.has_value() && !shape.refined_indices.empty()) {
            // No room for the refined group: the model's own triangles, not no model, and no second try for it.
            shape.refined_indices.clear();
            shape.refine_failed = true;
            if (!shapes::expand(geometry, arrays, position_matrix, shape.curved.get(), expand_scratch_)) {
                return;
            }
            block_units = block_size(expanded_units, index_bytes);
            offset = resident_vertices_.allocate(block_units);
        }
        if (!offset.has_value()) {
            return;
        }
        // Written once while the block belongs to this group, so no in-flight frame sees a partial write.
        std::byte* const staged = renderer_.stage_resident_vertices(
            static_cast<std::uint64_t>(*offset) * sizeof(RenderVertex),
            static_cast<std::uint64_t>(block_units) * sizeof(RenderVertex));
        std::memcpy(staged, expand_scratch_.data(), static_cast<std::size_t>(expanded_units) * sizeof(RenderVertex));
        std::memcpy(staged + static_cast<std::size_t>(expanded_units) * sizeof(RenderVertex),
                    !shape.refined_indices.empty() ? shape.refined_indices.data() : geometry.indices.data(),
                    index_bytes);
        shape.resident_epoch = resident_vertices_.epoch();
        shape.resident_offset = *offset;
        shape.resident_units = block_units;
        shape.resident_vertices = expanded_units;
    }
    const std::uint32_t units = shape.resident_vertices;

    // The scene's opaque models (the depth and colour they leave) cast the occlusion its pass reads.
    if (collect_ao_objects_ && record.pass.depth_write != 0u && record.pass.color_write != 0u && record.pass.blend_op == 0u &&
        (geometry.attributes & shapes::kOwnPositions) == 0u) {
        if (!shape.object_slots_built) {
            shape.object_slots_built = true;
            shape.object_slots.clear();
            if (shapes::expand(geometry, arrays, position_matrix, shape.curved.get(), expand_scratch_)) {
                for (const RenderVertex& v : expand_scratch_) {
                    const std::uint32_t row = v.mtx_indices[0] & 0xFFu;
                    auto slot = std::find_if(shape.object_slots.begin(), shape.object_slots.end(),
                                             [row](const ao::ObjectSlot& s) { return s.row == row; });
                    if (slot == shape.object_slots.end()) {
                        ao::ObjectSlot made;
                        made.row = row;
                        std::copy_n(v.position, 3u, made.min);
                        std::copy_n(v.position, 3u, made.max);
                        shape.object_slots.push_back(made);
                        slot = std::prev(shape.object_slots.end());
                    }
                    for (unsigned i = 0u; i < 3u; ++i) {
                        slot->min[i] = std::min(slot->min[i], v.position[i]);
                        slot->max[i] = std::max(slot->max[i], v.position[i]);
                    }
                }
            }
        }
        ao_objects_.add(keys[0], shape.object_slots,
                        [this](std::uint32_t row, std::uint32_t column) { return matrices_.matrix_value(row, column); });
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
        RenderCore* backend = nullptr;
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
    if (!begin_cached_draw_run(Primitive::Triangles, 0u, 1u)) {
        return;
    }
    scope.run = true;
    if (cached_draw_run_scissor_reject_) {
        return;
    }
    if (cached_draw_run_no_pso_) {
        ++stat_draws_no_pso_;
        return;
    }
    flush_pending_draw_batch();
    cached_draw_run_batch_compatible_known_ = false;

    const std::vector<std::uint16_t>& indices = !shape.refined_indices.empty() ? shape.refined_indices : geometry.indices;
    const auto index_count = static_cast<std::uint32_t>(indices.size());
    const std::size_t vertex_byte_offset = static_cast<std::size_t>(shape.resident_offset) * sizeof(RenderVertex);
    ++stat_draws_issued_;

    const gpu::VertexView vbv = gpu::vertex_view(resident_vertex_gpu_ + vertex_byte_offset,
                                                 units * static_cast<std::uint32_t>(sizeof(RenderVertex)),
                                                 static_cast<std::uint32_t>(sizeof(RenderVertex)));
    const gpu::IndexView ibv = gpu::index_view(
        resident_vertex_gpu_ + vertex_byte_offset + static_cast<std::size_t>(units) * sizeof(RenderVertex),
        index_count * static_cast<std::uint32_t>(sizeof(std::uint16_t)));
    DrawCall call = cached_draw_run_state_call_;
    call.vertex_view = vbv;
    call.index_view = ibv;
    call.index_count = index_count;
    pending_draw_batch_.active = true;
    pending_draw_batch_.call = call;
    pending_draw_batch_.rt = false;
    pending_draw_batch_.hidden = false;
    pending_draw_batch_.traced = false;
    pending_draw_batch_.indexed = true;
    pending_draw_batch_.base_vertex = shape.resident_offset;
    pending_draw_batch_.vertex_count = units;
    pending_draw_batch_.vertex_byte_end = vertex_byte_offset + static_cast<std::size_t>(units) * sizeof(RenderVertex);
    pending_draw_batch_.first_index = 0u;
    pending_draw_batch_.index_count = index_count;
    pending_draw_batch_.draw_count = 1u;
    pending_draw_batch_.resident = true;
    pending_draw_batch_.sealed = true;
}

void RenderCore::draw_record_immediate(const shapes::Record& record) {
    display_copy_on_output_ = false;
    const auto primitive = record.primitive == shapes::kWideQuads ? Primitive::Triangles : static_cast<Primitive>(record.primitive);
    const std::uint32_t count = record.vertex_count;
    shapes::primitive_indices(record.primitive, count, immediate_indices_);
    if (immediate_indices_.empty() || count > 0x10000u) {
        return;
    }
    RecordVertexInputs inputs;
    inputs.matrix_bits = (record.attributes & shapes::kPositionMatrix) != 0u ? 1u : 0u;
    inputs.colored = (record.attributes & shapes::kColor0) != 0u;
    inputs.own_positions = true;
    struct RecordScope {
        RenderCore* backend = nullptr;
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
        return;
    }
    if (cached_draw_run_no_pso_) {
        ++stat_draws_no_pso_;
        return;
    }
    const DrawCall& state_call = cached_draw_run_state_call_;
    // Consecutive draws with the same state share one GPU draw : the vertices and indices
    // follow the batch's in the rings, the indices counted from its first vertex.
    bool append = pending_draw_batch_.active && !pending_draw_batch_.resident && pending_draw_batch_.indexed &&
                  pending_draw_batch_.vertex_count + count <= 0x10000u &&
                  draw_calls_batch_compatible(pending_draw_batch_.call, state_call, true, true);
    if (!append) {
        flush_pending_draw_batch();
    }
    cached_draw_run_batch_compatible_known_ = false;
    const UploadRing::Allocation vb =
        renderer_.vertex_ring().allocate(static_cast<std::size_t>(count) * sizeof(RenderVertex), 16u);
    // The rings are write-combined: this copy is the only touch, never a read.
    std::memcpy(vb.cpu, current_record_pool_->vertices.data() + record.vertex_first, static_cast<std::size_t>(count) * sizeof(RenderVertex));
    const auto index_count = static_cast<std::uint32_t>(immediate_indices_.size());
    const UploadRing::Allocation ib = renderer_.index_ring().allocate(
        static_cast<std::size_t>(index_count) * sizeof(std::uint16_t), sizeof(std::uint16_t));
    const auto first_index = static_cast<std::uint32_t>(ib.offset / sizeof(std::uint16_t));
    const auto base_vertex = static_cast<std::uint32_t>(vb.offset / sizeof(RenderVertex));
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
    if (append) {
        pending_draw_batch_.call.index_count += index_count;
        pending_draw_batch_.call.vertex_view.SizeInBytes += count * static_cast<std::uint32_t>(sizeof(RenderVertex));
        pending_draw_batch_.call.index_view.SizeInBytes += index_count * static_cast<std::uint32_t>(sizeof(std::uint16_t));
        pending_draw_batch_.vertex_count += count;
        pending_draw_batch_.vertex_byte_end = vb.offset + static_cast<std::size_t>(count) * sizeof(RenderVertex);
        pending_draw_batch_.index_count += index_count;
        ++pending_draw_batch_.draw_count;
        return;
    }
    const gpu::VertexView vbv =
        gpu::vertex_view(renderer_.vertex_ring().gpu_base() + vb.offset,
                         count * static_cast<std::uint32_t>(sizeof(RenderVertex)),
                         static_cast<std::uint32_t>(sizeof(RenderVertex)));
    const gpu::IndexView ibv = gpu::index_view(
        renderer_.index_ring().gpu_base() + static_cast<std::size_t>(first_index) * sizeof(std::uint16_t),
        index_count * static_cast<std::uint32_t>(sizeof(std::uint16_t)));
    DrawCall call = state_call;
    call.vertex_view = vbv;
    call.index_view = ibv;
    call.index_count = index_count;
    pending_draw_batch_.active = true;
    pending_draw_batch_.call = call;
    pending_draw_batch_.rt = false;
    pending_draw_batch_.hidden = false;
    pending_draw_batch_.traced = false;
    pending_draw_batch_.indexed = true;
    pending_draw_batch_.base_vertex = base_vertex;
    pending_draw_batch_.vertex_count = count;
    pending_draw_batch_.vertex_byte_end = vb.offset + static_cast<std::size_t>(count) * sizeof(RenderVertex);
    pending_draw_batch_.first_index = first_index;
    pending_draw_batch_.index_count = index_count;
    pending_draw_batch_.draw_count = 1u;
    pending_draw_batch_.resident = false;
    pending_draw_batch_.sealed = false;
}

void RenderCore::on_frame_output(const FramePassRequest& params) {
    flush_pending_draw_batch();
    if (params.display) {
        galaxy::timeline::record(galaxy::timeline::Event::DisplayCopy, stat_display_copies_);
        display_copied_in_chunk_ = true;
    }


    if (params.display) {
        DisplayTexture* display_picture = display_pictures_.acquire_display(params, frame_index_);
        if (display_picture != nullptr) {
            end_side_layer();
            mirror_opened_scene_ = false;
            // The frame ends: the next is recorded as screen-only from its first draw when this one drew no scene.
            screen_run_ = !frame_scene_seen_;
            frame_scene_seen_ = false;
            screen_frame_open_ = false;
            if (renderer_.can_hand_over_display_copy(params, *display_picture)) {
                // Native presentation: the display picture takes the scene target's colour target, and the scene target continues on a texture of
                // its size the display picture pool released.
                const FrameMap& map = renderer_.frame_map();
                display_pictures_.adopt_picture(
                    *display_picture, renderer_.hand_over_scene_color(display_pictures_.take_idle_texture(
                              static_cast<std::uint16_t>(map.width), static_cast<std::uint16_t>(map.height))));
            } else {
                renderer_.copy_scene_to_display(params, *display_picture);
            }
            // The display copy ends the 2D layer: later work and the next
            // frame use the render-resolution scene target again.
            display_copy_on_output_ = renderer_.output_scene_active();
            if (renderer_.output_scene_active()) {
                renderer_.end_output_scene();
                display_pictures_.set_frame_map(renderer_.frame_map());
                frame_bindings_dirty_ = true;
            }
            display_picture->pointer_response=render_pointer_response_;
            display_picture->prerecorded_movie=render_movie_content_;
            display_pictures_.mark_display_copied(*display_picture, frame_index_);
            ++stat_display_copies_;
            record_monotonic_cadence_sample(display_copy_production_cadence_);
        }
    } else {
        // The capture replaces what its buffer (the key) held: an image handed over there no longer describes it.
        const bool decoded_evicted = texture_cache_.forget(params.key) != 0u;
        // A colour copy right after a display copy takes the picture it showed (with the 2D layer), as the
        // game's single scene target holds it, not the scene the output-resolution 2D layer was drawn over.
        const bool from_display = display_copy_on_output_;
        const bool alias_updated = from_display
            ? renderer_.draw_frame_pass_from_output(params, texture_cache_)
            : renderer_.draw_frame_pass(params, texture_cache_);
        if (alias_updated || decoded_evicted) {
            // One address can alternate between format-keyed GPU resources, and the game need not
            // rewrite the texture state after a copy, so the next draw refreshes every frame table
            // that referenced this alias. A reused texture is updated in place; its SRV stays valid.
            mark_texture_binding_alias_dirty(params);
            texture_bindings_dirty_ = true;
        } else {
        }
    }

}

void RenderCore::load_light(const state::Op& op) {
    // Light a[0]: its colour (a[1-4]) and angle and distance terms (f[0-5]), or its position and direction (f[0-5]).
    const unsigned n = op.a[0] & 7u;
    const float first[3] = {op.f[0], op.f[1], op.f[2]};
    const float second[3] = {op.f[3], op.f[4], op.f[5]};
    if (op.kind == state::kLightAttenuation) {
        const std::uint8_t color[4] = {op.a[1], op.a[2], op.a[3], op.a[4]};
        matrices_.set_light_attenuation(n, color, first, second);
    } else {
        matrices_.set_light_position(n, first, second);
    }
}

void boot_step(const char* step) noexcept {
    static std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    std::fprintf(stderr, "[boot] renderer %s %.0f ms\n", step,
                 std::chrono::duration<double, std::milli>(now - last).count());
    last = now;
}

unsigned layer_2d_lines() noexcept {
    return s_layer_2d_lines.load(std::memory_order_relaxed);
}


}  // namespace galaxy::render
