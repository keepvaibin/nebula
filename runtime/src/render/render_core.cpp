// RenderCore: takes the game's records and drives the renderer.
// render_api.cpp exposes it through the galaxy::render API in render_api.h.

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#pragma warning(push, 0)
#include "galaxy/portable.h"
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
#include "galaxy/benchmark.h"
#include "galaxy/pacing_recorder.h"
#include "galaxy/profiled_threads.h"
#include "galaxy/loss_report.h"
#include "galaxy/crash_report.h"
#include "galaxy/host_extension.h"
#include "galaxy/platform_time.h"
#include "galaxy/platform_paths.h"
#include "galaxy/precise_wait.h"
#include "galaxy/perf_hint.h"

#include "galaxy/render/present_clock.h"
#include "galaxy/portable.h"
#include "galaxy/render/render_core.h"
#include "galaxy/render/render_queue.h"
#include "galaxy/launch_args.h"
#include "galaxy/render/host_window.h"
#include "galaxy/render/resolution_fit.h"
#include "galaxy/render/device_memory.h"
#if defined(NEBULA_GPU_METAL)
#include "galaxy/render/mtl/mtl_context.h"
#endif

#include "galaxy/display_aspect.h"
#include "galaxy/content_viewport.h"
#include "galaxy/render/render_config.h"
#include "galaxy/windows_power.h"
#include "galaxy/frame_timeline.h"
#include "galaxy/render/idle_clock.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <charconv>
#include <cstddef>
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
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

namespace galaxy::render {

SamplerFrameBias sampler_frame_bias(const TextureSampler& mode, bool arbitrary, bool authored) noexcept {
    // Levels drawn by hand show the level the game showed: the frame's lines over the authored lines undo the finer
    // levels a taller frame picks, and the scene's bias (an upscaler's) is for detail these levels do not hold.
    if ((min_filter_code(mode) & 3u) == 0u) {
        return SamplerFrameBias::none;
    }
    return arbitrary || authored ? SamplerFrameBias::lines : SamplerFrameBias::scene;
}

SamplerKey make_sampler_key(const TextureSampler& mode, std::uint32_t mip_levels, float lod_shift, bool arbitrary,
                            bool authored) noexcept {
    std::uint32_t min_filter = min_filter_code(mode);
    float min_lod = mode.min_lod, max_lod = mode.max_lod;
    const bool source_mips = (min_filter & 3u) != 0u;
    const bool by_hand = sampler_frame_bias(mode, arbitrary, authored) == SamplerFrameBias::lines;
    float bias = mode.lod_bias;
    std::uint32_t authored_bit = 0u;
    if (by_hand) {
        bias -= lod_shift;
        authored_bit = kSamplerAuthoredLevels;
        if (arbitrary) {
            // A replacement's levels go on past the game's range, filtered between levels.
            max_lod = static_cast<float>(mip_levels != 0u ? mip_levels - 1u : 0u);
            min_filter = (min_filter & 4u) | 3u;
        }
    } else if (lod_shift != 0.0f) {
        if (source_mips) {
            min_lod = min_lod == 0.0f ? 0.0f : min_lod + lod_shift;
            max_lod += lod_shift;
        } else {
            min_filter = min_filter >= 4u ? 6u : 1u;
            min_lod = 0.0f; max_lod = lod_shift;
        }
    }
    const std::uint32_t filter = static_cast<std::uint32_t>(mode.wrap_s)
        | static_cast<std::uint32_t>(mode.wrap_t) << 2u | (mode.mag_linear ? 1u << 4u : 0u) | min_filter << 5u | authored_bit;
    return {filter, min_lod, max_lod, bias};
}

void boot_step(const char* step) noexcept;

namespace {


std::atomic<unsigned> s_layer_2d_lines{0u};

// lose_gpu_device's request, taken at the renderer's next frame.
std::atomic<bool> g_lose_device{false};
// RenderCore::restart_device runs: a new graphics device is being made (device_rebuilding).
std::atomic<bool> g_device_rebuilding{false};

// Render thread: the game frame of the newest list drawn (NebulaFrameList.game_frame).
std::uint32_t g_drawn_game_frame = 0u;

// The file an extension keeps this present of the game's picture in (host_extension.h), or none. `kind` is a
// NEBULA_EXT_PICTURE_*: the picture the game made shown for the first time, shown again, or a frame generated between
// two of the game's, so a capture with interpolation on compares with one with it off by its game frames alone.
std::string present_snapshot_path(std::uint32_t kind) {
    if (!galaxy::extensions::keeps_pictures()) {
        return {};
    }
    const char* const path = galaxy::extensions::picture_path(g_drawn_game_frame, kind);
    if (path == nullptr) {
        return {};
    }
    // Its time, for latency measurements against the input's.
    static constexpr const char* kKinds[] = {"game", "repeat", "generated"};
    std::cerr << "[snapshot] " << path << " qpc=" << galaxy::qpc_now() << " kind=" << kKinds[kind % 3u] << '\n';
    return path;
}

// A generated-frame cap as the statistics print it.
std::string generated_cap_text(unsigned cap) {
    return cap == kUnboundedGeneratedFrames ? std::string("unlimited") : std::to_string(cap);
}

// Adds its lifetime to the game thread's time blocked on the render thread (RenderCore::game_wait_qpc_).
struct GameWaitTimer {
    std::atomic<std::uint64_t>& total;
    const std::int64_t start = galaxy::qpc_now();
    ~GameWaitTimer() {
        total.fetch_add(static_cast<std::uint64_t>(galaxy::qpc_now() - start), std::memory_order_relaxed);
    }
};

}  // anonymous namespace

void lose_gpu_device() noexcept {
    g_lose_device.store(true, std::memory_order_release);
}

bool device_rebuilding() noexcept {
    return g_device_rebuilding.load(std::memory_order_acquire);
}

void RenderCore::reset_session_state() {
    // While no producer or render thread runs.
    frame_index_ = 0u;
    reset_monotonic_cadence(picture_made_cadence_);
    reset_monotonic_cadence(new_picture_shown_cadence_);
    last_presented_display_stamp_.store(~std::uint64_t{0}, std::memory_order_relaxed);
    last_presented_picture_.store(~std::uint64_t{0}, std::memory_order_relaxed);
    last_presented_slot_.store(0u, std::memory_order_relaxed);
}

bool RenderCore::initialize(int w, int h) {
    if (initialized_) {
        return true;
    }

    reset_session_state();

    boot_step("begins");
    // The scene at the window's picture: its exact size, square pixels.
    const auto start_aspect = galaxy::latched_display_aspect();
    const galaxy::ContentViewport start_content =
        galaxy::fit_content_viewport(w, h, start_aspect ? start_aspect->content_aspect : galaxy::kDesignContentAspect);
    const FrameMap start_frame = start_content.width > 0 && start_content.height > 0
                                     ? FrameMap(static_cast<unsigned>(start_content.width), static_cast<unsigned>(start_content.height))
                                     : FrameMap{};
    renderer_.set_window_owner(window_owner_);
    if (!renderer_.initialize(w, h, start_frame)) {
        std::cerr << "[RenderCore] RendererD3D12::initialize failed\n";
        return false;
    }
    boot_step("device rest");

    if (!display_pictures_.initialize(renderer_.device(), start_frame)) {
        std::cerr << "[RenderCore] DisplayPictures::initialize failed\n";
        renderer_.shutdown();
        return false;
    }

    device_gate_.set_device(renderer_.device());
    if (!texture_store_.initialize(renderer_.device(), device_gate_)) {
        std::cerr << "[RenderCore] TextureStore::initialize failed\n";
        display_pictures_.shutdown();
        renderer_.shutdown();
        return false;
    }
    boot_step("frame reads and texture cache");

    registries_.initialize(renderer_.device(), device_gate_);
    pipelines_.initialize(renderer_.device(), renderer_.pipeline_root(), renderer_.pipeline_backend());
    boot_step("pipelines");
    cap_texture_packs();

    // The first start (no config.ini, and no back end chose for it): a conservative height now, measured in the first 3D scene.
    // Not in a benchmark (it sets every height itself; bench::start runs before this, with the arguments).
    if (const RenderConfig chosen = get_render_config_chosen();
        render_config_first_start() && chosen.upscaler == 0u && chosen.internal_height == 0u && start_frame.height > 0u &&
        galaxy::ordinary_start() && !galaxy::bench::running()) {
        const bool shared = device_unified_memory();
        calibration_ = {};
        // The picture's size on the window (the renderer's output, which internal_extent scales).
        calibration_.width = start_frame.width;
        calibration_.lines = start_frame.height;
        calibration_.start = first_start_internal_height(shared, calibration_.lines);
        // The other height timed: 480 lines against 1080 or the panel.
        calibration_.heights[0] = calibration_.start;
        calibration_.heights[1] = calibration_.start == kFitLowestHeight ? (calibration_.lines > 1080u ? 1080u : 0u) : kFitLowestHeight;
        calibration_.stage = calibration_.lines > kFitLowestHeight ? 1u : 0u;
        RenderConfig start = chosen;
        start.internal_height = calibration_.start;
        set_render_config(start);
        calibration_.armed = get_render_config_chosen();
        std::fprintf(stderr, "[settings] first start: %s memory, %ux%u: internal height %u until measured\n",
                     shared ? "shared" : "dedicated", calibration_.width, calibration_.lines, calibration_.start);
    }

    initialized_ = true;
    stop_render_thread_ = false;
    render_chunks_in_flight_ = 0;
    present_chunks_in_flight_ = 0;
    render_thread_ = std::thread(&RenderCore::render_thread_main, this);
    if (::galaxy::diagnosing(::galaxy::kDiagnoseTelemetry)) {
        try {
            frame_telemetry_ = std::make_unique<galaxy::telemetry::Publisher>([this] {
                const auto stats = display_present_stats();
                return galaxy::telemetry::Counts{
                    pictures_made(), stats.new_pictures_shown, stats.presented,
                    stats.pictures_shown_again, stats.last_presented_picture,
                    stats.last_presented_frame_stamp, stats.last_presented_slot,
                    picture_made_cadence_.published_last_sample_ns.load(std::memory_order_relaxed),
                    new_picture_shown_cadence_.published_last_sample_ns.load(std::memory_order_relaxed)};
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

std::uint64_t RenderCore::pictures_made() const noexcept {
    return stat_pictures_made_.load(std::memory_order_relaxed);
}

DisplayPresentStats RenderCore::display_present_stats() const noexcept {
    const std::uint64_t last_stamp =
        last_presented_display_stamp_.load(std::memory_order_relaxed);
    const std::uint64_t last_serial =
        last_presented_picture_.load(std::memory_order_relaxed);
    DisplayPresentStats stats{};
    stats.new_pictures_shown = stat_new_pictures_shown_.load(std::memory_order_relaxed);
    stats.last_presented_frame_stamp = last_stamp == ~std::uint64_t{0} ? 0u : last_stamp;
    stats.last_presented_picture = last_serial == ~std::uint64_t{0} ? 0u : last_serial;
    stats.last_presented_slot = last_presented_slot_.load(std::memory_order_relaxed);
    stats.presented = stat_presents_.load(std::memory_order_relaxed);
    stats.pictures_shown_again =
        stat_pictures_shown_again_.load(std::memory_order_relaxed);
    return stats;
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
    const bool monitor=::galaxy::diagnosing(::galaxy::kDiagnoseFrameTails);
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

// --diagnostics memory: every ten seconds, what the renderer holds, in MB.
void RenderCore::trace_memory() {
    static galaxy::ClockInterval report{10'000};
    if (!::galaxy::diagnosing(::galaxy::kDiagnoseMemory) || !report.due()) {
        return;
    }
    const auto mb = [](std::uint64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); };
    std::fprintf(stderr, "[memory] meshes and slots %.1f, textures %.1f\n", mb(registries_.memory_bytes()),
                 mb(texture_store_.backend().heap_stats().used_bytes));
#if defined(_WIN32)
    // --diagnostics heaps: the process's heaps as well, each one's busy bytes and the block sizes holding the most.
    const bool heaps = ::galaxy::diagnosing(::galaxy::kDiagnoseHeaps);
    if (heaps) {
        std::vector<HANDLE> list(GetProcessHeaps(0, nullptr));  // as many as the process has
        const DWORD count = list.empty() ? 0u : std::min<DWORD>(GetProcessHeaps(static_cast<DWORD>(list.size()), list.data()), static_cast<DWORD>(list.size()));
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
    const bool enabled=::galaxy::diagnosing(::galaxy::kDiagnoseFrameTails);
    if(!enabled) return;
    const auto now=new_picture_shown_cadence_.last_sample_ns;
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
            // for a continuous process, plateaus for intervals quantized to display refreshes. Meaningful
            // only in a window with tens of intervals.
            row<<' '<<name<<"-pct-upper-us="
               <<percentile(5u)<<'/'<<percentile(10u)<<'/'<<percentile(25u)<<'/'
               <<percentile(50u)<<'/'<<percentile(75u)<<'/'<<percentile(90u);
            a.monitor_bins.fill(0u);a.monitor_intervals=a.monitor_total_ns=a.monitor_max_ns=0u;
        };
        append("production",picture_made_cadence_);append("first-present",new_picture_shown_cadence_);
        row<<'\n';std::cerr<<row.str();
    } catch (...) { /* Optional monitoring must not unwind rendering. */ }
}

void RenderCore::record_pointer_response(const DisplayTexture& texture) noexcept {
    const bool enabled=::galaxy::diagnosing(::galaxy::kDiagnosePointerLatency);
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
        row<<"[mouse-response-latency] boundary=selected-pad-sample-to-first-marked-display picture-Present-return physical-display-time=unmeasured"
           <<" samples="<<pointer_response_count_<<" mean-us="<<pointer_latency_total_/pointer_response_count_/1000u
           <<" p50-upper-us="<<percentile(50u)<<" p95-upper-us="<<percentile(95u)<<" p99-upper-us="<<percentile(99u)
           <<" max-us="<<pointer_latency_max_/1000u<<" source-serial="<<stamp.serial<<" generation="<<stamp.generation
           <<" display-serial="<<texture.picture_number<<" selected-ns="<<stamp.selected_ns<<" processed-ns="<<stamp.processed_ns
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

void RenderCore::note_picture_shown(
    const DisplayTexture& texture) noexcept {
    galaxy::timeline::record(galaxy::timeline::Event::DisplayPresented, texture.slot,
                         texture.picture_number);
    record_pointer_response(texture);
    last_presented_display_stamp_.store(
        texture.frame_stamp, std::memory_order_relaxed);
    last_presented_picture_.store(
        texture.picture_number, std::memory_order_relaxed);
    ++stat_presents_;
    pipelines_.picture_shown();
    if (texture.last_shown_number == texture.picture_number) {
        ++stat_pictures_shown_again_;
        return;
    }
    texture.last_shown_number = texture.picture_number;
    ++stat_new_pictures_shown_;
    record_monotonic_cadence_sample(new_picture_shown_cadence_);
    galaxy::bench::note_new_picture(new_picture_shown_cadence_.last_sample_ns);
    report_frame_tail_window();
    trace_memory();
}

// Producer (simulation thread): copies the frame and hands it to the render thread. Blocks only
// while the queue is full.
void RenderCore::render_frame(bool present_swap_chain, bool black, bool list_shows) {
    if (!initialized_) {
        return;
    }

    // Nothing to render: no list.
    const bool empty = producer_list_ == nullptr;
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
    FrameChunk chunk;
    chunk.pointer_response=producer_pointer_response_;
    chunk.list = std::exchange(producer_list_, nullptr);
    if (chunk.list != nullptr) {
        galaxy::bench::note_frame_list(chunk.list->item_count);
        const auto hold = device_gate_.hold();
        chunk.device_epoch = device_gate_.epoch();
        // Registries first: a table taken here names textures whose work is taken too.
        chunk.registry_ops = registries_.take_ops();
        chunk.texture_ops = texture_store_.take_ops();
        chunk.list_asked_qpc = galaxy::qpc_now();
        chunk.list_step_qpc = step_moment_for(chunk.list_asked_qpc);
        chunk.shows = list_shows;
    }
    chunk.prerecorded_movie=producer_movie_content_;
    chunk.movie_frame=producer_movie_frame_;
    chunk.present_swap_chain = present_swap_chain;
    chunk.black = black;
    chunk.present_asked_qpc = present_swap_chain ? galaxy::qpc_now() : 0;
    chunk.step_moment_qpc = present_swap_chain ? step_moment_for(chunk.present_asked_qpc) : 0;
    const RenderTimingConfig& timing = get_frame_timing_config();

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
        const std::size_t stalled_depth_limit =
            std::max(queue_depth_limit, kStalledRenderQueueDepth);
        const GameWaitTimer game_wait{game_wait_qpc_};
        for (;;) {
            if (stop_render_thread_ || render_thread_error_ != nullptr) {
                break;
            }
            const std::uint64_t now = steady_now_ns();
            const QueuePlace place = queue_place(queued_render_chunks(), queue_depth_limit, stalled_depth_limit,
                                                 render_work_started_ns_.load(std::memory_order_acquire), now,
                                                 kRenderStallNs);
            if (place.take) {
                break;
            }
            if (place.until_ns == 0u) {
                queue_space_cv_.wait(lock);
            } else {
                queue_space_cv_.wait_for(lock, std::chrono::nanoseconds(place.until_ns - now));
            }
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
        frame_queue_.push_back(std::move(chunk));
    }
    queue_cv_.notify_one();
    galaxy::timeline::record(galaxy::timeline::Event::SubmitEnd, 0u, 0u);
}

void RenderCore::render_work_begins() noexcept {
    render_work_started_ns_.store(steady_now_ns(), std::memory_order_release);
    galaxy::host::perf_hint_busy(true);
}

void RenderCore::render_work_ends() noexcept {
    render_work_started_ns_.store(0u, std::memory_order_release);
    galaxy::host::perf_hint_busy(false);
}

void RenderCore::finish_pictures_here() {
    try {
        renderer_.finish_pictures();
    } catch (const std::exception& error) {
        std::cerr << "[render] the pictures at exit: " << error.what() << '\n';
    } catch (...) {
        std::cerr << "[render] the pictures at exit: an unknown error\n";
    }
    pictures_finish_asked_.store(false, std::memory_order_relaxed);
    {
        const std::lock_guard<std::mutex> lock(queue_mutex_);
        pictures_finished_ = true;
    }
    pictures_finished_cv_.notify_all();
}

bool RenderCore::finish_pictures(std::chrono::milliseconds limit) {
    if (!initialized_) {
        return true;
    }
    if (render_thread_.get_id() == std::this_thread::get_id()) {
        pictures_finish_asked_.store(true, std::memory_order_relaxed);
        finish_pictures_here();
        return true;
    }
    std::unique_lock<std::mutex> lock(queue_mutex_);
    if (!render_thread_.joinable() || stop_render_thread_ || render_thread_error_ != nullptr) {
        return false;
    }
    pictures_finished_ = false;
    pictures_finish_asked_.store(true, std::memory_order_release);
    queue_cv_.notify_all();
    return pictures_finished_cv_.wait_for(lock, limit, [this] { return pictures_finished_; });
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
                present_chunks_in_flight_ == 0u);
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
SceneView RenderCore::scene_depth_view(const float terms[6], const ScaledViewport& viewport) const {
    const auto projection = [terms](unsigned i) {
        return terms[i];
    };
    // fill_vs_projection: D3D clip z = -(z E + F) (1 - 1e-7).
    constexpr float kFarEps = 1.0f - 1e-7f;
    SceneView view{};
    view.projection = {projection(0u), projection(1u), projection(2u), projection(3u)};
    // The scene's depth is drawn with the temporal upscaler's jitter (clip x + jx w, so the
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

ScheduleSettings RenderCore::ScheduleView::settings() const {
    const RenderConfig cfg = get_render_config();
    return {present_frame_rate(presented_max_fps(cfg), cfg.vsync_interval, core_.renderer_.display_refresh_hz()),
            cfg.vsync_interval != 0u};
}

bool RenderCore::ScheduleView::vblank_grid(std::int64_t& vblank, std::int64_t& period) const {
    return pictures_on_refreshes(core_.renderer_.swap_queue()) && core_.renderer_.vblank_grid(vblank, period);
}

// The renderer's answer (render/variable_refresh.h); its refresh is then the cap it holds the display under. While the
// presents' proof runs, the slots follow the pacer's clock at the probe rate in the same way.
bool RenderCore::ScheduleView::variable_refresh(double& cap) const {
    const double probe = g_refresh_probe_hz.load(std::memory_order_relaxed);
    if (probe > 1.0) {
        cap = probe;
        return true;
    }
    const bool variable = g_variable_refresh.load(std::memory_order_relaxed);
    cap = variable ? core_.renderer_.display_refresh_hz() : 0.0;
    return variable;
}

double RenderCore::ScheduleView::generated_gpu_ms() const {
    return core_.renderer_.interpolation_gpu_cost_ms();
}

bool RenderCore::ScheduleView::temporal_upscaling() const {
    return core_.renderer_.temporal_interpolation();
}

void RenderCore::ScheduleView::suspend_temporal_upscaling(double generated_ms) {
    core_.renderer_.suspend_temporal_interpolation(generated_ms);
}

void RenderCore::ScheduleView::reset_generated_gpu_ms() {
    core_.renderer_.reset_generated_gpu_ema();
}

bool RenderCore::ScheduleView::benchmark() const {
    return galaxy::bench::running();
}

bool RenderCore::ScheduleView::tracing() const {
    return ::galaxy::diagnosing(::galaxy::kDiagnosePresentStats);
}

// The renderer's display model (frame_schedule.h DisplayModel), where its platform answers; only where pictures show
// whole from a refresh on (a tearing swap chain shows them where they land).
bool RenderCore::ScheduleView::display_time(std::int64_t present, std::int64_t& shown) const {
    shown = 0;
    return pictures_on_refreshes(core_.renderer_.swap_queue()) && core_.renderer_.display_time(present, shown);
}

bool RenderCore::ScheduleView::last_shown(ShownPicture& picture) {
    return core_.renderer_.take_shown(picture);
}

void RenderCore::start_interp_sequence(ListEntry& entry, std::int64_t arrived, std::int64_t step) {
    // A list before the waiting one began: that one begins now, replacing the playing one.
    begin_waiting_interp_sequence();
    schedule_.start(galaxy::qpc_now(), arrived, step, interp_stopping_);
    if (schedule_.waiting()) {
        interp_next_entry_ = &entry;
    } else {
        interp_entry_ = &entry;
    }
}

void RenderCore::begin_waiting_interp_sequence() {
    if (!schedule_.waiting()) {
        return;
    }
    supersede_interp_sequence();
    interp_entry_ = std::exchange(interp_next_entry_, nullptr);
    schedule_.begin_waiting(galaxy::qpc_now());
}

void RenderCore::end_interp_sequences() {
    begin_waiting_interp_sequence();
    supersede_interp_sequence();
}

// The next frame's sequence replaces this one before its real frame (at rates that are not multiples of 60 that is the
// normal end: the next frame's generated frames continue from t = 0, its predecessor's image).
void RenderCore::supersede_interp_sequence() {
    if (!schedule_.supersede()) {
        return;
    }
    // What it writes, later frames read: its own time is walked, unseen. The temporal history goes on through it: its
    // motion from the picture shown last, and the next sequence's (whose frame before is this one) from it.
    ListEntry& entry = *interp_entry_;
    walk_frame(entry, interp_walk_time(entry, 1.0f, false), false);
    note_interp_walked(entry, 1.0f);
}

bool RenderCore::overlay_idle_presents() const noexcept {
    static const std::int64_t frequency = galaxy::qpc_frequency();
    return (native_settings_overlay_visible() || home_menu_visible()) && !schedule_.busy() &&
           (schedule_.unbounded() || (schedule_.slot_period() > 0 && schedule_.slot_period() * 61 < frequency));
}

// Render thread: the last part of the wait to a present slot (at most about 2 ms, after the coarse wait), ended on the
// slot's moment: a timer woken late would present the picture late for the moment it shows.
void RenderCore::interp_wait_until(std::int64_t due_qpc) {
    galaxy::PreciseWait::for_this_thread().until(due_qpc);
}

namespace {

// The game time a picture shows, on the step clock: t steps on from the moment its frame's step began (t = 1: the
// frame itself), or when that moment is unknown, the moment the picture is asked for.
std::int64_t content_moment_qpc(std::int64_t step_qpc, double t, std::int64_t asked_qpc) {
    static const double period = static_cast<double>(galaxy::qpc_frequency()) / kGameStepRate;
    return step_qpc != 0 ? step_qpc + static_cast<std::int64_t>(std::llround(t * period)) : asked_qpc;
}

}  // namespace

void RenderCore::advance_interp_sequence(bool flush) {
    const std::int64_t now = galaxy::qpc_now();
    SchedulePicture picture = schedule_.next(now, flush);
    if (picture.kind == SchedulePicture::Kind::Begin) {
        // The slot has reached the waiting sequence's reference.
        begin_waiting_interp_sequence();
        picture = schedule_.next(now, flush);
    }
    if (picture.kind == SchedulePicture::Kind::None || picture.kind == SchedulePicture::Kind::Begin) {
        return;
    }
    const std::int64_t slot = picture.slot;
    const double t = picture.t;
    const std::int64_t step_qpc = schedule_.sequence().step_qpc;
    const auto content_moment = [&](double shown) {
        if (interp_entry_ != nullptr && interp_entry_->data.list != nullptr) {
            galaxy::timeline::record_at(galaxy::timeline::Event::ContentTime, static_cast<std::uint64_t>(slot),
                                        interp_entry_->data.list->game_frame,
                                        static_cast<std::uint64_t>(std::llround(std::min(shown, 1.0) * 1e6)));
        }
    };
    if (picture.kind == SchedulePicture::Kind::Generated) {
        content_moment(t);
        const auto start = std::chrono::steady_clock::now();
        const std::int64_t generated_start = galaxy::qpc_now();
        // The picture shows the slot's moment (a vblank where they are timed): presentation times show it there.
        renderer_.set_present_moment(slot);
        galaxy::pacing::describe(slot, content_moment_qpc(step_qpc, t, slot), step_qpc, true);
        const bool presented = render_generated_frame(static_cast<float>(t));
        renderer_.set_present_moment(0);
        skip_slot_for_depth();
        const auto spent = static_cast<std::uint64_t>(galaxy::qpc_now() - generated_start);
        interp_cpu_ms_ += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (presented) {
            ++interp_presented_;
            note_present_vblank(slot);
        } else {
            ++interp_failed_;
        }
        schedule_.generated_drawn(spent, presented);
        report_interp_stats();
        return;
    }
    if (!flush) content_moment(1.0);
    interp_presenting_real_ = true;
    renderer_.set_present_moment(flush ? 0 : slot);
    {
        const std::int64_t asked = flush ? now : slot;
        galaxy::pacing::describe(asked, content_moment_qpc(step_qpc, 1.0, asked), step_qpc, false);
    }
    render_generated_frame(1.0f);
    renderer_.set_present_moment(0);
    note_present_vblank(slot);
    interp_presenting_real_ = false;
    ++interp_sequences_;
    report_interp_stats();
}

// The presentation times lower their depth by one picture fewer (PresentSlots::take_skip): the next slot is skipped,
// so the picture after this one shows a vsync after it and a vsync sooner after its moment.
void RenderCore::skip_slot_for_depth() {
    if (renderer_.take_present_skip()) {
        schedule_.skip_slot();
    }
}

// A present while interpolating: the vblank interval it was issued in, against the one before (0: two in one
// interval, one of them never shown on its own; 2 or more at one present a vblank: a refresh shown twice), and the
// time from its slot to the vblank ending that interval. The picture shows its slot's moment, so with even motion
// that time stays the same; its spread is the motion's unevenness on the display.
void RenderCore::note_present_vblank(std::int64_t slot) {
    const bool trace = ::galaxy::diagnosing(::galaxy::kDiagnosePresentStats);
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

// A game frame presented while interpolating, without generated frames: why.
void RenderCore::note_interp_real_present() {
    schedule_.note_step(galaxy::qpc_now(), game_wait_qpc_.load(std::memory_order_relaxed));
    ++interp_real_only_[native_settings_overlay_visible() ? 0u : 1u];
    report_interp_stats();
}

void RenderCore::report_interp_stats() {
    const bool trace = ::galaxy::diagnosing(::galaxy::kDiagnosePresentStats);
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
    renderer_.take_generated_gpu_ms(gpu_ms, gpu_samples);
    const double seconds = static_cast<double>(now - interp_stats_qpc_) / static_cast<double>(frequency);
    const double presented = static_cast<double>(interp_presented_);
    std::uint64_t real_only = 0u;
    for (const std::uint64_t count : interp_real_only_) real_only += count;
    std::ostringstream message;
    const ScheduleCounts counts = schedule_.take_counts();
    message << "[interp-stats] generated-per-frame=" << schedule_.configured()
            << " sequences=" << interp_sequences_
            << " presented-hz=" << presented / seconds
            << " dropped=" << interp_dropped_
            << " late-slots=" << counts.late_slots
            << " waited-slots=" << counts.waited_slots
            << " vblank-steps=" << interp_vblank_steps_[0] << '/' << interp_vblank_steps_[1] << '/'
            << interp_vblank_steps_[2] << '/' << interp_vblank_steps_[3]
            << " slot-to-vblank-ms=" << interp_slot_to_vblank_min_ms_ << '/' << interp_slot_to_vblank_max_ms_
            << " superseded=" << counts.superseded
            << " started=" << counts.started
            << " lists-skipped=" << interp_lists_skipped_
            << " presents-during-sequence=" << interp_present_skipped_
            << " failed=" << interp_failed_
            << " history-resets=" << counts.history_resets
            << " history-carried=" << counts.history_carried
            << " real-only=" << real_only
            << " (overlay=" << interp_real_only_[0] << " other=" << interp_real_only_[1] << ")"
            << " motion(camera-cuts/game-cuts/teleports/flips)=" << pairing_.camera_cuts << '/' << pairing_.game_cuts << '/'
            << pairing_.teleports << '/' << pairing_.flips
            << " wall-ms-per-generated=" << (interp_presented_ != 0u ? interp_cpu_ms_ / presented : 0.0)
            << " gpu-ms-per-generated=" << (gpu_samples != 0u ? gpu_ms / static_cast<double>(gpu_samples) : 0.0)
            << " gpu-generated-cap=" << generated_cap_text(schedule_.gpu_cap())
            << " cadence-generated-cap=" << generated_cap_text(schedule_.cadence_cap())
            << " cadence-drops=" << counts.cadence_drops
            << " game-fps=" << schedule_.game_fps()
            << " step-delay-ms(now/max)=" << static_cast<double>(schedule_.delay_qpc()) * 1000.0 / static_cast<double>(frequency)
            << '/' << counts.delay_ms_max
            << " capped-sequences=" << counts.gpu_capped
            << " generated-gpu-ema-ms=" << renderer_.generated_gpu_ema_ms()
            << " scene-gpu-ema-ms=" << renderer_.interpolation_gpu_cost_ms()
            << " items-culled=" << std::exchange(stat_items_culled_, 0u)
            << " draws-no-pipeline=" << std::exchange(stat_draws_no_pipeline_, 0u)
            << " pipelines(made/pending/missed/failed)=" << pipelines_.created() << '/' << pipelines_.pending() << '/'
            << pipelines_.misses() << '/' << pipelines_.failures()
            << " ring-peak-kb(v/i/c/m)="
            << renderer_.vertex_ring().take_peak_bytes_per_frame() / 1024u << '/'
            << renderer_.index_ring().take_peak_bytes_per_frame() / 1024u << '/'
            << renderer_.constant_ring().take_peak_bytes_per_frame() / 1024u << '/'
            << renderer_.matrix_ring().take_peak_bytes_per_frame() / 1024u
            << " measured-presents-hz=" << renderer_.measured_present_hz() << '\n';
    if (coverage_lists_ != 0u) {
        message << "[interp-coverage] lists=" << coverage_lists_ << " category:items/new/missed/ring-at-b/discrete";
        for (std::size_t c = 0; c < coverage_.size(); ++c) {
            const CoverageCount& count = coverage_[c];
            if (count.new_items + count.ring_at_b + count.discrete == 0u) {
                continue;
            }
            message << ' ' << (c >= NEBULA_CATEGORY_DRAW ? "draw" : "") << (c >= NEBULA_CATEGORY_DRAW ? c - NEBULA_CATEGORY_DRAW : c)
                    << ':' << count.items << '/' << count.new_items << '/' << count.missed << '/' << count.ring_at_b << '/'
                    << count.discrete;
        }
        message << '\n';
    }
    std::cerr << message.str();
    interp_presented_ = 0u;
    interp_dropped_ = 0u;
    interp_vblank_steps_ = {};
    interp_slot_to_vblank_samples_ = 0u;
    interp_lists_skipped_ = 0u;
    interp_present_skipped_ = 0u;
    interp_failed_ = 0u;
    interp_sequences_ = 0u;
    interp_real_only_ = {};
    interp_cpu_ms_ = 0.0;
    pairing_.camera_cuts = pairing_.game_cuts = pairing_.teleports = pairing_.flips = 0u;
    interp_stats_qpc_ = now;
}

bool RenderCore::render_generated_frame(float t) {
    ListEntry& entry = *interp_entry_;
    walk_frame(entry, interp_walk_time(entry, t, true), true);
    note_interp_walked(entry, t);
    return true;
}

core::WalkTime RenderCore::interp_walk_time(const ListEntry& entry, float t, bool count) {
    const bool cut = (entry.data.list->flags & NEBULA_FRAME_CAMERA_CUT) != 0u;
    const ScheduleWalk walk = schedule_.walk_time(&entry, entry.before, cut, t, count);
    core::WalkTime time;
    time.t = walk.t;
    time.t_prev = walk.t_prev;
    time.generated = t < 1.0f;
    time.reset = walk.reset;
    return time;
}

void RenderCore::note_interp_walked(const ListEntry& entry, float t) noexcept {
    schedule_.walked(&entry, t);
}

bool RenderCore::skip_for_present_rate() {
    const RenderConfig cfg = get_render_config();
    const double rate = present_frame_rate(presented_max_fps(cfg), cfg.vsync_interval, renderer_.display_refresh_hz());
    if (rate >= kGameStepRate - 1.0) {
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
        const std::int64_t asked = galaxy::qpc_now();
        if (!frame_queue_.empty()) {
            FrameChunk& back = frame_queue_.back();
            if (back.present_only) {
                back.black = black;
                back.present_asked_qpc = asked;
                back.step_moment_qpc = step_moment_for(asked);
                queue_cv_.notify_one();
                return;
            }
            if (!back.present_only && !back.present_swap_chain) {
                back.present_swap_chain = true;
                back.black = black;
                back.present_asked_qpc = asked;
                back.step_moment_qpc = step_moment_for(asked);
                queue_cv_.notify_one();
                return;
            }
        }
        FrameChunk chunk;
        chunk.present_only = true;
        chunk.present_swap_chain = true;
        chunk.black = black;
        chunk.present_asked_qpc = asked;
        chunk.step_moment_qpc = step_moment_for(asked);
        frame_queue_.push_back(std::move(chunk));
    }
    queue_cv_.notify_one();
}

// Consumer loop (render thread): frames execute in submission order; queued
// frames are drained before a stop so the last presents land.
void RenderCore::render_thread_main() {
    galaxy::host::name_current_thread(L"Nebula Render");
    galaxy::profiled_threads::add_current("render");
    galaxy::host::perf_hint_add_current_thread("render");
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
            // polling and every generated frame wait.
            galaxy::host::hold_high_resolution_timer(true);
#endif
        });
    }
    for (;;) {
        render_loop();
        if (!device_lost_ || !restart_device()) {
            return;
        }
    }
}

bool RenderCore::bear_device_loss(std::exception_ptr error) {
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& lost) {
        // By its text, or by the device itself (a failure the device's removal caused, whatever it says: P-062).
        if (!galaxy::crash::is_gpu_lost(lost.what()) && !gpu::device_removed(renderer_.device())) {
            return false;
        }
        std::cerr << "[render] the graphics card was reset: " << lost.what() << '\n';
    } catch (...) {
        return false;
    }
    // Twice in a row is borne. A third within five minutes of the one before is a fault to report.
    constexpr std::int64_t kInARowSeconds = 300;
    const std::int64_t now = galaxy::qpc_now();
    if (now - last_device_loss_qpc_ > kInARowSeconds * galaxy::qpc_frequency()) {
        device_losses_ = 0u;
    }
    last_device_loss_qpc_ = now;
    if (++device_losses_ > 2u) {
        device_gate_.give_up();
        return false;
    }
    return true;
}

void RenderCore::drop_device_state() {
    {
        const std::lock_guard<std::mutex> lock(queue_mutex_);
        for (const FrameChunk& queued : frame_queue_) {
            if (queued.list != nullptr) {
                nebula_frame_list_release(queued.list);
            }
        }
        frame_queue_.clear();
        render_work_started_ns_.store(0u, std::memory_order_release);
        queue_space_cv_.notify_all();
    }
    if (in_hand_list_ != nullptr) {
        nebula_frame_list_release(in_hand_list_);
        in_hand_list_ = nullptr;
    }
    for (const std::unique_ptr<ListEntry>& entry : lists_) {
        nebula_frame_list_release(entry->data.list);
    }
    lists_.clear();
    free_lists_.clear();
    freed_textures_.clear();
    retired_tables_.clear();

    schedule_.forget();
    interp_entry_ = nullptr;
    interp_next_entry_ = nullptr;
    interp_list_shown_ = false;
    interp_presenting_real_ = false;

    walk_ = {};
    walk_rows_ring_ = walk_vertex_ring_ = walk_ring_previous_ = 0u;
    walk_rows_cpu_ = nullptr;
    walk_views_.clear();
    walk_constants_known_.clear();
    walk_material_at_.clear();
    walk_lerped_.clear();
    walk_ring_cpu_ = nullptr;
    walk_index_cpu_ = nullptr;
    generated_written_.clear();
    scene_phase_ = target_open_ = temporal_scene_ = motion_scene_ = false;
    black_shown_ = picture_made_in_chunk_ = false;
    reset_monotonic_cadence(picture_made_cadence_);
    reset_monotonic_cadence(new_picture_shown_cadence_);
    last_presented_display_stamp_.store(~std::uint64_t{0}, std::memory_order_relaxed);
    last_presented_picture_.store(~std::uint64_t{0}, std::memory_order_relaxed);
    last_presented_slot_.store(0u, std::memory_order_relaxed);

    pipelines_queued_.store(0u, std::memory_order_relaxed);
    pipelines_settings_ = ~0u;
    texture_pack_settings_applied_ = false;
    movie_mods_generation_ = movie_mod_folders_generation() + 1u;
}

// The lists of tables dropped or retired: free again once the GPU has passed the work recorded so far.
void RenderCore::give_up_released_lists() {
    registries_.take_released_lists(released_lists_);
    for (const core::TextureLists::Place& place : released_lists_) {
        texture_lists_.release(place, renderer_.submitted_fence_value() + 1u);
    }
    released_lists_.clear();
}

bool RenderCore::restart_renderer(const FrameMap& frame, bool same_card) {
    Renderer::Kept kept = renderer_.take_apart_lost_device();
    std::destroy_at(&renderer_);
#if !defined(NEBULA_GPU_VULKAN) && !defined(NEBULA_GPU_METAL) && !defined(NEBULA_GPU_D3D11)
    Renderer::list_live_objects(kept);
#endif
    std::construct_at(&renderer_);
    renderer_.set_window_owner(window_owner_);
    renderer_.keep(kept, same_card);
    try {
        return renderer_.initialize(static_cast<int>(host_window::backbuffer_width),
                                    static_cast<int>(host_window::backbuffer_height), frame);
    } catch (const std::exception& error) {
        std::cerr << "[render] no new device: " << error.what() << "\n";
        return false;
    }
}

bool RenderCore::restart_device() {
    device_lost_ = false;
    const std::int64_t started = galaxy::qpc_now();
    g_device_rebuilding.store(true, std::memory_order_release);
    struct Steps {
        std::int64_t waited = 0, pipelines = 0, textures = 0, rest = 0, device = 0, starts = 0;
    } steps;
    struct RebuildMark {
        ~RebuildMark() { g_device_rebuilding.store(false, std::memory_order_release); }
    } rebuild_mark;
    try {
        std::unique_lock<std::shared_mutex> rebuilding = device_gate_.begin_rebuild();
        if (const std::uint32_t delay = galaxy::extensions::device_rebuild_delay_ms(); delay != 0u) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));  // a test's slow driver
        }
        const FrameMap frame = renderer_.frame_map();
        std::int64_t mark = started;
        const auto lap = [&](std::int64_t& ms) {
            const std::int64_t now = galaxy::qpc_now();
            ms = (now - mark) * 1000 / galaxy::qpc_frequency();
            mark = now;
        };
        lap(steps.waited);
        pipelines_.shutdown();
        lap(steps.pipelines);
        movie_playback_.shutdown();
        texture_store_.shutdown();
        lap(steps.textures);
        registries_.lose_device();
        texture_lists_.clear();
        target_slots_.clear_places();
        display_pictures_.shutdown();
        drop_device_state();
        lap(steps.rest);

        // The driver takes a moment to come back.
        constexpr unsigned kTries = 5u;
        bool back = false;
        for (unsigned attempt = 0u; attempt < kTries && !back; ++attempt) {
            if (attempt != 0u) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
            back = restart_renderer(frame, attempt + 1u != kTries);
        }
        lap(steps.device);
        if (!back || !display_pictures_.initialize(renderer_.device(), frame)) {
            throw std::runtime_error("[render] no device came back: DEVICE_LOST");
        }
        device_gate_.set_device(renderer_.device());
        texture_store_.restart(renderer_.device());
        registries_.restart(renderer_.device());
        pipelines_.initialize(renderer_.device(), renderer_.pipeline_root(), renderer_.pipeline_backend());
        cap_texture_packs();
        lap(steps.starts);
        device_gate_.end_rebuild(rebuilding);
    } catch (...) {
        device_gate_.give_up();
        const std::lock_guard<std::mutex> lock(queue_mutex_);
        render_thread_error_ = std::current_exception();
        stop_render_thread_ = true;
        queue_space_cv_.notify_all();
        return false;
    }
    // Each step's time, so a slow restore names the step that took it (N-1072).
    std::cerr << "[render] a new device in " << (galaxy::qpc_now() - started) * 1000 / galaxy::qpc_frequency()
              << " ms (the gate " << steps.waited << ", pipeline workers stopped " << steps.pipelines << ", movies and textures "
              << steps.textures << ", the rest " << steps.rest << ", the device " << steps.device << ", started again "
              << steps.starts << ")\n";
    if (asked_loss_frame_ != 0u) {
        std::cerr << "[gpu] recovered from the device loss asked at frame " << asked_loss_frame_ << " (rendering goes on)\n";
        asked_loss_frame_ = 0u;
    }
    return true;
}

void RenderCore::render_loop() {
    // A failure ends the queued utilities' waits; the caller holds queue_mutex_.
    const auto fail_queued_utilities = [&](std::exception_ptr error) {
        if (bear_device_loss(error)) {
            device_lost_ = true;
            return;
        }
        render_thread_error_ = error;
        stop_render_thread_ = true;
        queue_space_cv_.notify_all();
    };
    try {
    for (;;) {
        try {
            if (device_gate_.lost_seen()) {
                throw std::runtime_error("[render] device lost, seen by a service");
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            fail_queued_utilities(std::current_exception());
            break;
        }
        if (pictures_finish_asked_.load(std::memory_order_acquire)) {
            finish_pictures_here();
        }
        // Out of sight (minimised: the host stopped the game; covered: the game goes on and its frames are drawn but not presented,
        // RendererD3D12::present_to_swap_chain): no picture of its own is presented, no generated frame is made, and nothing
        // polls. A covered window is looked at every 100 ms to see whether the OS shows it again; the device and its
        // resources stay as they are, so a return costs no rebuild.
        const bool hidden = out_of_sight();
        // Open but not in front (set_window_background): the game's own frames at its own rate, no generated frames and no
        // repeated pictures.
        const bool background = window_background_.load(std::memory_order_relaxed);
        if (hidden || background) {
            try {
                end_interp_sequences();
                const std::int64_t now = galaxy::qpc_now();
                if (renderer_.occluded() && now >= occlusion_probe_due_qpc_) {
                    occlusion_probe_due_qpc_ = now + galaxy::qpc_frequency() / 10;
                    renderer_.probe_occlusion();
                }
            } catch (...) {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                fail_queued_utilities(std::current_exception());
                break;
            }
        }
        FrameChunk chunk;
        bool have_chunk = false;
        bool overlay_idle = false;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            // Draining for a stop, out of sight or behind another window: queued frames present without generated frames.
            interp_stopping_ = stop_render_thread_ || hidden || background;
            // A native overlay over a game showing no interpolated frames (the HOME Menu's paused game draws no 3D
            // scene): the latest frame again at each display slot, so the overlay keeps the rate play had instead of the
            // game's 60 Hz frame rate.
            overlay_idle = !hidden && !background && !stop_render_thread_ && overlay_idle_presents();
            if (!overlay_idle) {
                overlay_idle_due_qpc_ = 0;
            } else if (overlay_idle_due_qpc_ == 0) {
                overlay_idle_due_qpc_ = galaxy::qpc_now() + schedule_.slot_period();
            }
            if (schedule_.busy() && frame_queue_.empty() && !stop_render_thread_) {
                // Coarse and interruptible to about 1.5 ms before the slot
                // (the condition variable wakes on the 1 ms scheduler tick),
                // then a high-resolution timer for the rest.
                const std::int64_t remaining_us = schedule_.remaining_us(galaxy::qpc_now());
                if (remaining_us > 2000) {
                    queue_cv_.wait_for(lock, std::chrono::microseconds(remaining_us - 1500), [this] {
                        return stop_render_thread_ || !frame_queue_.empty() || pictures_finish_asked_.load(std::memory_order_relaxed);
                    });
                } else if (remaining_us > 0) {
                    const std::int64_t due = schedule_.slot_due();
                    lock.unlock();
                    interp_wait_until(due);
                    lock.lock();
                }
            } else if (overlay_idle && frame_queue_.empty()) {
                static const std::int64_t frequency = galaxy::qpc_frequency();
                const std::int64_t remaining_us = (overlay_idle_due_qpc_ - galaxy::qpc_now()) * 1'000'000 / frequency;
                if (remaining_us > 2000) {
                    queue_cv_.wait_for(lock, std::chrono::microseconds(remaining_us - 1500), [this] {
                        return stop_render_thread_ || !frame_queue_.empty() || pictures_finish_asked_.load(std::memory_order_relaxed);
                    });
                } else if (remaining_us > 0) {
                    const std::int64_t due = overlay_idle_due_qpc_;
                    lock.unlock();
                    interp_wait_until(due);
                    lock.lock();
                }
            } else {
                // A service waiting for a new card can't reach this thread any other way (DeviceGate).
                queue_cv_.wait_for(lock, std::chrono::milliseconds(hidden ? 100 : 250), [this] {
                    return stop_render_thread_ || !frame_queue_.empty() || pictures_finish_asked_.load(std::memory_order_relaxed);
                });
            }
            // A due slot goes before queued chunks, so presents keep their
            // cadence while a game frame's chunks stream in; except a slot
            // that would show the frame itself (t >= 1) while the next
            // frame's list is queued: that list's sequence may take the slot.
            const auto due_slot_first = [this] {
                if (schedule_.remaining_us(galaxy::qpc_now()) > 0) return false;
                // Skipping the real endpoint is valid only after this sequence has shown an intermediate
                // picture. Otherwise a saturated GPU can render every endpoint unseen and starve presentation.
                if (!schedule_.sequence().active || schedule_.sequence().next == 0u) return true;
                if (schedule_.slot_t() < 1.0 - 1e-3) return true;
                for (const FrameChunk& queued : frame_queue_) {
                    if (!queued.present_only && queued.list != nullptr) return false;
                }
                return true;
            };
            if (schedule_.busy() && !stop_render_thread_ &&
                (frame_queue_.empty() || due_slot_first())) {
                // An interpolation step walks a list too (its draw.native calls among it): work the producer may
                // run ahead of when it holds the thread.
                render_work_begins();
                lock.unlock();
                try {
                    advance_interp_sequence(false);
                } catch (...) {
                    render_work_ends();
                    std::lock_guard<std::mutex> relock(queue_mutex_);
                    fail_queued_utilities(std::current_exception());
                    break;
                }
                render_work_ends();
                continue;
            }
            if (frame_queue_.empty()) {
                if (stop_render_thread_) {
                    break;  // stop requested, queue drained
                }
            } else if (frame_queue_.front().list != nullptr && frame_queue_.front().device_epoch != device_gate_.epoch()) {
                // Made for the card that was lost.
                nebula_frame_list_release(frame_queue_.front().list);
                frame_queue_.pop_front();
                queue_space_cv_.notify_all();
                continue;
            } else {
                chunk = std::move(frame_queue_.front());
                frame_queue_.pop_front();
                in_hand_list_ = chunk.list;
                if (!chunk.present_only && !chunk.present_swap_chain &&
                    !frame_queue_.empty() &&
                    frame_queue_.front().present_only) {
                    FrameChunk present = std::move(frame_queue_.front());
                    frame_queue_.pop_front();
                    chunk.present_swap_chain = present.present_swap_chain;
                    chunk.black = present.black;
                    chunk.present_asked_qpc = present.present_asked_qpc;
                    chunk.step_moment_qpc = present.step_moment_qpc;
                }
                render_work_begins();
                if (chunk.present_only) {
                    ++present_chunks_in_flight_;
                } else {
                    galaxy::timeline::record(galaxy::timeline::Event::RenderBegin, 0u, 0u);
                    ++render_chunks_in_flight_;
                }
                have_chunk = true;
            }
        }

        // A present of the latest picture again is work too (a chunk's began as it was taken).
        if (!have_chunk && overlay_idle) {
            render_work_begins();
        }
        try {
            if (have_chunk) {
                queue_space_cv_.notify_all();
                if (chunk.present_only) {
                    // An interpolation sequence presents the latest frame itself: the present a step asks for
                    // supersedes it only when it is black, and else only feeds the cadence count.
                    if (chunk.black) {
                        end_interp_sequences();
                        interp_list_shown_ = false;
                        present_cached_display_on_thread(true);
                    } else if (interp_list_shown_) {
                        schedule_.note_step(chunk.present_asked_qpc, game_wait_qpc_.load(std::memory_order_relaxed));
                    } else if (!schedule_.busy()) {
                        cached_present_asked_qpc_ = chunk.present_asked_qpc;
                        cached_present_step_qpc_ = chunk.step_moment_qpc;
                        present_cached_display_on_thread(false);
                    } else {
                        ++interp_present_skipped_;
                    }
                } else {
                    render_frame_on_thread(chunk);
                }
            } else if (overlay_idle) {
                // A due slot presents again the picture last presented (never a newer one between the game's own
                // presents) unless a present of the game's own landed in it.
                const std::int64_t now = galaxy::qpc_now();
                if (schedule_.unbounded()) {
                    // No slot clock (an unlimited rate): again as soon as the last present is done.
                    present_cached_display_on_thread(false, false,
                                                     last_presented_slot_.load(std::memory_order_relaxed));
                    overlay_idle_due_qpc_ = now;
                } else if (now >= overlay_idle_due_qpc_) {
                    const std::int64_t period = schedule_.slot_period();
                    if (renderer_.last_present_qpc() < overlay_idle_due_qpc_ - period) {
                        present_cached_display_on_thread(false, false,
                                                     last_presented_slot_.load(std::memory_order_relaxed));
                    }
                    overlay_idle_due_qpc_ += period;
                    if (now >= overlay_idle_due_qpc_) {
                        overlay_idle_due_qpc_ += ((now - overlay_idle_due_qpc_) / period + 1) * period;
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

        render_work_ends();
        if (have_chunk && chunk.present_only) {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (present_chunks_in_flight_ != 0u) {
                --present_chunks_in_flight_;
            }
            queue_space_cv_.notify_all();
        } else if (have_chunk) {
            std::lock_guard<std::mutex> lock(queue_mutex_);
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

void RenderCore::present_black_picture(bool in_frame) {
    const DisplayTexture* layout = display_pictures_.latest();
    if (layout == nullptr || (black_shown_ && !native_settings_overlay_visible())) {
        return;
    }
    const std::string snapshot_path = present_snapshot_path(NEBULA_EXT_PICTURE_GAME);
    if (!snapshot_path.empty()) {
        renderer_.set_debug_capture_paths(snapshot_path);
    }
    if (!in_frame) {
        renderer_.begin_frame();
    }
    renderer_.present(layout, !snapshot_path.empty(), true);
    if (!snapshot_path.empty()) {
        renderer_.set_debug_capture_paths({});
    }
    if (!in_frame) {
        renderer_.end_frame();
    }
    black_shown_ = true;
    // The frame shown next is new to the display, even when it repeats the last display picture.
    last_presented_picture_.store(~std::uint64_t{0}, std::memory_order_relaxed);
}

void RenderCore::present_cached_display_on_thread(
    bool black,
    bool count_game_display_present,
    std::optional<std::uint32_t> slot) {
    const std::int64_t asked = std::exchange(cached_present_asked_qpc_, 0);
    const std::int64_t step = std::exchange(cached_present_step_qpc_, 0);
    if (black) {
        if (count_game_display_present) {
            present_black_picture(false);
        }
        return;
    }
    black_shown_ = false;
    const DisplayTexture* selected = slot ? display_pictures_.picture(*slot) : display_pictures_.latest();
    if (selected == nullptr) {
        return;
    }
    const bool duplicate_stamp =
        selected->picture_number ==
        last_presented_picture_.load(std::memory_order_relaxed);
    const bool skip_duplicate_present =
        count_game_display_present &&
        duplicate_stamp &&
        !native_settings_overlay_visible();
    if (count_game_display_present && !skip_duplicate_present &&
        !native_settings_overlay_visible() && skip_for_present_rate()) {
        return;
    }
    if (skip_duplicate_present) {
        return;
    }
    const std::string snapshot_path = !count_game_display_present ? std::string{} : present_snapshot_path(duplicate_stamp ? NEBULA_EXT_PICTURE_REPEAT : NEBULA_EXT_PICTURE_GAME);
    if (!snapshot_path.empty()) {
        renderer_.set_debug_capture_paths(snapshot_path);
    }
    const bool game_frame_only =
        interp_on_ && count_game_display_present && !interp_presenting_real_ && !duplicate_stamp;
    if (game_frame_only) {
        note_interp_real_present();
    }
    renderer_.set_present_game_frame(game_frame_only);
    renderer_.begin_frame();
    if (count_game_display_present) {
        const std::int64_t when = asked != 0 ? asked : galaxy::qpc_now();
        galaxy::pacing::describe(when, content_moment_qpc(step, 1.0, when), step, false);
    }
    // Present-only frames acquire no display picture textures, so DisplayPictures retirement advances with
    // render frames only; otherwise render-then-present would age pictures twice per frame.
    (void)renderer_.present(selected, !snapshot_path.empty());
    if (!snapshot_path.empty()) {
        renderer_.set_debug_capture_paths({});
    }
    renderer_.end_frame();
    galaxy::pacing::drop_description();
    if (count_game_display_present) {
        note_picture_shown(*selected);
        last_presented_slot_.store(selected->slot, std::memory_order_relaxed);
    }
    renderer_.debug_log_backbuffer_readback();
}

RenderCore::ListEntry& RenderCore::list_arrived(FrameChunk& chunk) {
    in_hand_list_ = nullptr;
    std::unique_ptr<ListEntry> entry;
    if (!free_lists_.empty()) {
        entry = std::move(free_lists_.back());
        free_lists_.pop_back();
        core::ListData kept = std::move(entry->data);
        entry->data = core::ListData{};
        // The tables keep their memory from list to list.
        entry->data.block_before = std::move(kept.block_before);
        entry->data.block_rows = std::move(kept.block_rows);
        entry->data.item_before = std::move(kept.item_before);
        entry->data.joint_motion = std::move(kept.joint_motion);
        entry->data.motions = std::move(kept.motions);
        // The rest keep their memory too, emptied: each is what a new list's empty table is but for its capacity.
        const auto emptied = [](auto& to, auto& from) {
            to = std::move(from);
            to.clear();
        };
        emptied(entry->data.camera_before, kept.camera_before);
        emptied(entry->data.camera_motion, kept.camera_motion);
        emptied(entry->data.motion_first, kept.motion_first);
        emptied(entry->data.constants_before, kept.constants_before);
        emptied(entry->data.view_before, kept.view_before);
        emptied(entry->data.light_set_before, kept.light_set_before);
        emptied(entry->data.eye_motions, kept.eye_motions);
        emptied(entry->data.early_targets, kept.early_targets);
        emptied(entry->data.late_depth_clears, kept.late_depth_clears);
        emptied(entry->data.block_camera, kept.block_camera);
        emptied(entry->data.light_camera, kept.light_camera);
        emptied(entry->data.block_world_wanted, kept.block_world_wanted);
        emptied(entry->data.view_terms, kept.view_terms);
    } else {
        entry = std::make_unique<ListEntry>();
    }
    entry->data.list = chunk.list;
    entry->data.serial = ++list_serial_;
    entry->before = lists_.empty() ? nullptr : lists_.back().get();
    entry->pointer_response = chunk.pointer_response;
    entry->prerecorded_movie = chunk.prerecorded_movie;
    entry->movie_frame = chunk.movie_frame;
    entry->registry_ops = std::move(chunk.registry_ops);
    entry->texture_ops = std::move(chunk.texture_ops);
    entry->ops_waiting = true;
    lists_.push_back(std::move(entry));
    return *lists_.back();
}

// A list is read while it is the newest walked or the one before the newest walked.
void RenderCore::release_lists_before(const ListEntry& entry) noexcept {
    while (lists_.size() > 1u && lists_.front().get() != entry.before && lists_.front().get() != &entry) {
        // The game writes the list's memory again once it sees the flag: every read of it comes first, on every CPU.
        std::atomic_thread_fence(std::memory_order_release);
        nebula_frame_list_release(lists_.front()->data.list);
        lists_[1]->before = nullptr;
        free_lists_.push_back(std::move(lists_.front()));
        lists_.pop_front();
    }
}

// Video memory: no size of it is assumed anywhere. The adapter's own figures (DXGI's local segment budget, told by the OS when it
// moves; Vulkan's device-local heap budget) are read at each frame boundary; past the budget the store leaves the top levels out
// of the textures no frame in flight names, and puts them back when they are named again and there is room (TextureStore
// enforce_budget). A host extension's vram_budget_mb replaces the budget with that many megabytes of the same reading.
// One line when the budget or what is left out changes (looked at once a second), and one an hour (at 60 frames a second) whatever
// changed.
// Half the device's memory budget: the replacements stand beside the game's own textures (kept for when one is evicted), the
// scene targets, meshes, rings and staging, and on a shared-memory device beside the game's and the host's RAM too (board
// rc.md, item 4). A dedicated card's budget (4 GB and up) leaves the 2 GB default as it is.
void RenderCore::cap_texture_packs() {
    std::uint64_t usage = 0u;
    std::uint64_t budget = 0u;
    if (!renderer_.video_memory(usage, budget)) {
        budget = 0u;
    }
    if (const std::uint64_t asked = galaxy::extensions::vram_budget_bytes(); asked != 0u) {
        budget = asked;
    }
    texture_pack_cap_bytes_ = budget / 2u;
    // The settings are compared again at the next frame.
    texture_pack_settings_applied_ = false;
    if (budget != 0u) {
        std::fprintf(stderr, "[texture-pack] replacements up to %llu MB on this device (half its %llu MB budget)\n",
                     static_cast<unsigned long long>(texture_pack_cap_bytes_ >> 20u), static_cast<unsigned long long>(budget >> 20u));
    }
}

void RenderCore::enforce_video_memory() {
    std::uint64_t usage = 0u;
    std::uint64_t budget = 0u;
    if (!renderer_.video_memory(usage, budget)) {
        return;
    }
    if (const std::uint64_t asked = galaxy::extensions::vram_budget_bytes(); asked != 0u) {
        budget = asked;
    }
    texture_store_.enforce_budget(usage, budget);
    // Looked at once a second and reported once an hour (game frames), or when the budget or what it left out changes.
    const std::uint64_t game_frame = list_serial_;
    if (game_frame >= vram_log_at_) {
        vram_log_at_ = game_frame + 60u;
        const TextureStore::BudgetStats stats = texture_store_.budget_stats();
        const std::uint64_t budget_mb = budget >> 20u;
        const std::uint64_t left_out_mb = stats.reduced_bytes >> 20u;
        if (budget_mb != vram_logged_budget_mb_ || left_out_mb != vram_logged_left_out_mb_ || game_frame >= vram_report_at_) {
            vram_logged_budget_mb_ = budget_mb;
            vram_logged_left_out_mb_ = left_out_mb;
            vram_report_at_ = game_frame + 216000u;
            std::fprintf(stderr, "[vram] budget %llu MB used %llu MB evicted %llu MB (%u textures reduced of %u)\n",
                         static_cast<unsigned long long>(budget_mb), static_cast<unsigned long long>(usage >> 20u),
                         static_cast<unsigned long long>(left_out_mb), stats.reduced, stats.resident);
        }
    }
}

void RenderCore::begin_walk_frame(ListEntry& entry) {
    const RenderConfig render = get_render_config();
    enhanced_lighting_ = render.enhanced_lighting;
    pixel_lit_ = lights_at_pixel(render);
    curved_refine_ = render.curved_surfaces;
    wide_scene_ = scene_wide(render);
    ambient_occlusion_ = render.ambient_occlusion && renderer_.ambient_occlusion_ready();
    // The complete native list is available before begin_frame can resize a
    // ring. Include all eye/world rows, even rows a particular program omits.
    const NebulaFrameList& list = *entry.data.list;
    std::size_t matrix_bytes = std::size_t{list.item_count} * 16u + 32u;
    // The frame's incoming models (a load's) by id, sorted once: an id's first entry, the one a search from the front finds.
    const auto& models = entry.registry_ops.models;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> incoming;
    if (!models.empty()) {
        incoming.reserve(models.size());
        for (std::uint32_t i = 0; i < models.size(); ++i) {
            incoming.emplace_back(models[i].first, i);
        }
        std::sort(incoming.begin(), incoming.end());
    }
    for (std::uint32_t b = 0; b < list.block_count; ++b) {
        const auto& block = list.blocks[b];
        std::size_t matrices = block.joint_count;
        if (block.model != 0u) {
            const auto found = std::lower_bound(incoming.begin(), incoming.end(), std::pair{block.model, 0u});
            matrices = found != incoming.end() && found->first == block.model ? models[found->second].second.draw_matrices.size()
                : registries_.model(block.model).draw_matrices.size();
        }
        matrix_bytes += matrices * core::kRowsPerMatrix * 16u * 2u;
    }
    matrix_bytes += std::size_t{core::light_set_rows(0u)} * 16u;
    for (std::uint32_t s = 0; s < list.light_set_count; ++s) {
        matrix_bytes += std::size_t{core::light_set_rows(list.light_sets[s].count)} * 16u;
    }
    matrix_bytes += std::size_t{list.instance_row_count} * 16u;
    std::size_t constant_bytes = std::size_t{list.view_count} * (sizeof(core::ViewRows) + 255u);
    for (std::uint32_t c = 0; c < list.constants_count; ++c) {
        const auto& constants = list.constants[c];
        constant_bytes += (std::size_t{constants.vertex_rows} + constants.pixel_rows) * 16u + 512u;
    }
    renderer_.matrix_ring().require_frame_bytes(matrix_bytes);
    renderer_.vertex_ring().require_frame_bytes(list.ring_vertex_bytes);
    renderer_.index_ring().require_frame_bytes(std::size_t{list.ring_index_count} * sizeof(std::uint32_t));
    renderer_.constant_ring().require_frame_bytes(constant_bytes);
    // A ring gives memory back only on a frame without the 3D scene (a load's, a fade's): making it again there shows nothing.
    const bool shrink_moment = (list.flags & NEBULA_FRAME_HAS_SCENE) == 0u;
    for (UploadRing* ring : {&renderer_.matrix_ring(), &renderer_.vertex_ring(), &renderer_.index_ring(), &renderer_.constant_ring()}) {
        ring->set_shrink_moment(shrink_moment);
    }
    renderer_.begin_frame();
    picture_made_in_chunk_ = false;
    // Whether the renderer's bloom is built (NEBULA_OPTION_BLOOM): without it, no bloom.
    bloom_route_.store(renderer_.game_bloom_ready() ? 1u : 0u, std::memory_order_relaxed);
    // begin_frame() applies a pending internal-resolution change, so copies take the scale the scene target
    // has now, not the setting, which can change in between.
    if (renderer_.frame_map() != display_pictures_.frame_map()) {
        display_pictures_.set_frame_map(renderer_.frame_map());
    }
    display_pictures_.begin_frame(renderer_.frame_slot(), kFramesInFlight);
    texture_store_.begin_frame(renderer_.frame_slot(), kFramesInFlight);
    enforce_video_memory();
    // New textures upload through this frame's command list.
    renderer_.attach_texture_uploads(texture_store_);
    {
        bool mods_moved = false;
        if (const std::uint64_t generation = texture_pack_mod_folders_generation();
            generation != texture_pack_mods_generation_) {
            texture_pack_mods_generation_ = generation;
            texture_pack_mod_folders_ = texture_pack_mod_folders();
            texture_pack_mod_categories_off_ = texture_pack_mod_categories_off();
            mods_moved = true;
        }
        std::uint64_t budget_bytes = static_cast<std::uint64_t>(render.texture_pack_budget_mb) << 20u;
        if (texture_pack_cap_bytes_ != 0u && budget_bytes > texture_pack_cap_bytes_) {
            budget_bytes = texture_pack_cap_bytes_;
        }
        // The settings are built and compared only when something they are made of may have changed.
        const bool moved = !texture_pack_settings_applied_ || mods_moved ||
                           texture_pack_settings_.enabled != render.texture_packs ||
                           texture_pack_settings_.preload != render.texture_pack_preload ||
                           texture_pack_settings_.dump != render.texture_dump ||
                           texture_pack_settings_.budget_bytes != budget_bytes;
        static const std::filesystem::path dumps = texture_dump_directory();
        TexturePackSettings settings{};
        settings.enabled = render.texture_packs;
        settings.preload = render.texture_pack_preload;
        settings.dump = render.texture_dump;
        settings.budget_bytes = budget_bytes;
        if (moved) {
            // Each mod's pack, the lowest in the mods list first (the last wins a texture).
            settings.packs = texture_pack_mod_folders_;
            settings.pack_categories_off = texture_pack_mod_categories_off_;
            settings.dumps = dumps;
        }
        if (moved && (!texture_pack_settings_applied_ || !(settings == texture_pack_settings_))) {
            texture_store_.configure_texture_packs(settings);
            texture_pack_settings_ = std::move(settings);
            texture_pack_settings_applied_ = true;
        }
    }
    // HD movie replacements: the movies folder beside the settings, then the mods' movies/ folders.
    if (const std::uint64_t generation = movie_mod_folders_generation(); generation != movie_mods_generation_) {
        movie_mods_generation_ = generation;
        std::vector<std::filesystem::path> folders;
        if (std::filesystem::path movies = movie_directory(); !movies.empty()) {
            folders.push_back(std::move(movies));
        }
        for (std::filesystem::path& folder : movie_mod_folders()) folders.push_back(std::move(folder));
        movie_playback_.configure(std::move(folders), [this](std::uint64_t size) {
            return texture_store_.backend().create_upload_chunk(size);
        });
    }
    // What the services queued before this list: its ids exist when it walks, and the lists before it have walked.
    if (entry.ops_waiting) {
        entry.ops_waiting = false;
        galaxy::timeline::record(galaxy::timeline::Event::ListOpsBegin, entry.data.serial, 0u);
        g_drawn_game_frame = entry.data.list->game_frame;
        advance_idle_clock(entry.data.serial);
        registries_.apply(entry.registry_ops, renderer_.submitted_fence_value());
        freed_textures_.clear();
        if (entry.texture_ops != nullptr) {
            texture_store_.apply_ops(*entry.texture_ops, freed_textures_);
            entry.texture_ops.reset();
        } else {
            texture_store_.release_held(freed_textures_);
        }
        retired_tables_.clear();
        registries_.forget_textures(freed_textures_, retired_tables_);
        give_up_released_lists();
#if defined(NEBULA_GPU_VULKAN) || defined(NEBULA_GPU_METAL) || defined(NEBULA_GPU_D3D11)
        // Draws with texture lists keep no tables: their lists are written each walk.
        if (!renderer_.bindless()) {
            for (const gpu::Table table : retired_tables_) {
                renderer_.retire_texture_table(table);
            }
        }
#endif
        for (const std::uint32_t id : freed_textures_) {
            if (TextureStore::is_target(id)) {
                renderer_.free_target(id, texture_store_);
                target_slots_.forget(id);  // forget_textures dropped the tables naming it
            }
        }
        // Tables no walk has named for a long while give up what was built for them (core::Registries::retire_idle_tables).
        // The idle time and the sweep interval are game frames (this list's serial), whatever the rate shows them at.
        const std::uint64_t game_frame = entry.data.serial;
        if (game_frame >= table_sweep_at_) {
            table_sweep_at_ = game_frame + core::Registries::kTableSweepFrames;
            retired_tables_.clear();
            registries_.retire_idle_tables(game_frame, retired_tables_, core::Registries::kTableIdleFrames);
            give_up_released_lists();
#if defined(NEBULA_GPU_VULKAN) || defined(NEBULA_GPU_METAL) || defined(NEBULA_GPU_D3D11)
            if (!renderer_.bindless()) {
                for (const gpu::Table table : retired_tables_) {
                    renderer_.retire_texture_table(table);
                }
            }
#endif
        }
        // --diagnostics memory: every 600 game frames, the texture tables and where the video memory is.
        if (game_frame >= table_log_at_ && ::galaxy::diagnosing(::galaxy::kDiagnoseMemory)) {
            table_log_at_ = game_frame + 600u;
            const core::Registries::TableCounts tables = registries_.table_counts();
            std::fprintf(stderr, "[render] texture tables: %u live, %u retired (%llu released since the start)\n", tables.live,
                         tables.retired, static_cast<unsigned long long>(tables.retirements));
#if !defined(NEBULA_GPU_VULKAN) && !defined(NEBULA_GPU_METAL) && !defined(NEBULA_GPU_D3D11)
            // The same 600 frames: where the process's video memory is (the renderer adds its own targets, rings and heaps).
            Renderer::VideoMemoryOutside outside;
            const PlacedTextureAllocator::Stats texture_heap = texture_store_.backend().heap_stats();
            outside.texture_heap_bytes = texture_heap.heap_bytes;
            outside.texture_used_bytes = texture_heap.used_bytes;
            outside.texture_heaps = texture_heap.heaps;
            outside.texture_committed = texture_heap.committed_fallbacks;
            outside.staging_bytes = TextureStore::staging_bytes();
            const core::Registries::MemoryStats meshes = registries_.memory_stats();
            outside.mesh_pages = meshes.pages;
            outside.mesh_empty_pages = meshes.empty_pages;
            outside.mesh_page_bytes = meshes.page_bytes;
            outside.mesh_local_bytes = meshes.local_bytes;
            outside.mesh_live_bytes = meshes.live_bytes;
            outside.slot_bytes = meshes.slot_bytes;
            outside.slots = meshes.slots;
            display_pictures_.for_each_texture([&](gpu::TextureObject* texture) {
                outside.picture_bytes += renderer_.allocation_bytes(texture);
                ++outside.picture_count;
            });
            outside.pipelines = pipelines_.created();
            renderer_.log_video_memory(outside);
#else
            // The same 600 frames: the device-local heap as the driver and the allocator read it (the renderer adds its images and rings).
            Renderer::VideoMemoryOutside outside;
            outside.staging_bytes = TextureStore::staging_bytes();
            const core::Registries::MemoryStats meshes = registries_.memory_stats();
            outside.mesh_pages = meshes.pages;
            outside.mesh_empty_pages = meshes.empty_pages;
            outside.mesh_page_bytes = meshes.page_bytes;
            outside.mesh_local_bytes = meshes.local_bytes;
            outside.mesh_live_bytes = meshes.live_bytes;
            outside.slot_bytes = meshes.slot_bytes;
            outside.slots = meshes.slots;
            display_pictures_.for_each_texture([&](gpu::TextureObject* texture) {
                outside.picture_bytes += Renderer::allocation_bytes(texture);
                ++outside.picture_count;
            });
            outside.pipelines = pipelines_.created();
            renderer_.log_video_memory(outside);
#endif
        }
        galaxy::timeline::record(galaxy::timeline::Event::ListOpsEnd, entry.data.serial, 0u);
    }
    registries_.reclaim(renderer_.completed_fence_value());
    // Finished replacements upload here and take the place of their originals.
    texture_store_.apply_texture_packs();
    // The movie frame this walk draws, as the movie quad's planes.
    if (render_movie_frame_.frame != MovieFrameMark::kNone || texture_store_.movie_override_active()) {
        if (movie_stats_report_.due()) {
            const MoviePlayback::Stats stats = movie_playback_.stats();
            if (stats.decoded != 0u) {
                std::fprintf(stderr, "[movie] decoded=%llu shown=%llu late=%llu seeks=%llu convert-ms=%.2f/frame\n",
                             static_cast<unsigned long long>(stats.decoded), static_cast<unsigned long long>(stats.shown),
                             static_cast<unsigned long long>(stats.late), static_cast<unsigned long long>(stats.seeks),
                             stats.convert_ms / static_cast<double>(stats.decoded));
            }
            if (stats.cooked_decoded != 0u) {
                std::fprintf(stderr,
                             "[movie] cooked decoded=%llu shown=%llu waits=%llu seeks=%llu failed=%llu decode-ms=%.3f/frame "
                             "wait-ms=%.3f\n",
                             static_cast<unsigned long long>(stats.cooked_decoded),
                             static_cast<unsigned long long>(stats.cooked_shown),
                             static_cast<unsigned long long>(stats.cooked_waits),
                             static_cast<unsigned long long>(stats.cooked_seeks),
                             static_cast<unsigned long long>(stats.cooked_failed),
                             stats.cooked_decode_ms / static_cast<double>(stats.cooked_decoded), stats.cooked_wait_ms);
            }
        }
        texture_store_.set_movie_override(render_movie_frame_, movie_playback_.frame(render_movie_frame_));
    }
    // Pipelines not yet asked for, for the settings in use.
    {
        const bool wide = wide_scene_;
        // Native interpolation replays geometry and does not write temporal reconstruction targets.
        const bool motion = renderer_.temporal_mode();
        const bool one_pass = motion && renderer_.motion_pass_only();
        const bool layer = motion && renderer_.has_layer_targets();
        const unsigned samples = renderer_.scene_samples_log2();
        const std::uint32_t settings =
            samples | (wide ? 4u : 0u) | (motion ? 8u : 0u) | (pixel_lit_ ? 16u : 0u) | (one_pass ? 32u : 0u);
        if (settings != pipelines_settings_) {
            pipelines_settings_ = settings;
            pipelines_queued_.store(0u, std::memory_order_relaxed);
        }
        const std::uint32_t count = registries_.pipeline_count();
        if (pipelines_queued_.load(std::memory_order_relaxed) != count) {
            std::vector<unsigned> variants{samples, 8u};
            if (samples != 0u) variants.push_back(0u);
            if (wide) variants.push_back(samples | kSceneVariantHdr);
            std::vector<core::Pipelines::Prepared> entries;
            entries.reserve(count - pipelines_queued_.load(std::memory_order_relaxed));
            for (std::uint32_t id = pipelines_queued_.load(std::memory_order_relaxed); id < count; ++id) {
                const core::PipelineEntry& pipeline = registries_.pipeline(id);
                if (id >= pipelines_noted_) {
                    pipelines_.note_registered(pipeline);  // after the first picture: not in the cook's list
                }
                if (pipeline.lit_form && !pixel_lit_) {
                    continue;
                }
                entries.push_back({id, std::make_shared<const core::PipelineEntry>(pipeline)});
            }
            pipelines_.prepare(entries, variants, motion, layer, one_pass);
            pipelines_queued_.store(count, std::memory_order_relaxed);
            pipelines_noted_ = std::max(pipelines_noted_, count);
        }
        // Fixed steps: a missing pipeline is waited for, not left out, and a scene's load gate drains the queue and the left
        // forms here.
        pipelines_.set_wait_for_missing((galaxy::extensions::flags() & NEBULA_EXT_FIXED_STEPS) != 0u);
        if (pipelines_drain_.exchange(false, std::memory_order_acq_rel)) {
            pipelines_.drain();
        }
    }
    renderer_.bind_scene();
}

// One frame on the display's clock: `entry` at its time, into a picture. Real frames (t = 1) end what the list writes.
void RenderCore::walk_frame(ListEntry& entry, const core::WalkTime& time, bool present) {
    render_pointer_response_ = entry.pointer_response;
    render_movie_content_ = entry.prerecorded_movie;
    render_movie_frame_ = entry.movie_frame;
    begin_walk_frame(entry);
    galaxy::timeline::record(galaxy::timeline::Event::WalkBegin, entry.data.serial, time.generated ? 1u : 0u);
    galaxy::pacing::walk_began();
    walk_list(entry.data, entry.before != nullptr ? &entry.before->data : nullptr, time);
    galaxy::pacing::walk_ended();
    galaxy::timeline::record(galaxy::timeline::Event::WalkEnd, entry.data.serial, 0u);
    const bool real = time.t >= 1.0f;
    const DisplayTexture* selected = present ? display_pictures_.latest() : nullptr;
    const std::string snapshot_path = selected != nullptr ? present_snapshot_path(real ? NEBULA_EXT_PICTURE_GAME : NEBULA_EXT_PICTURE_GENERATED) : std::string{};
    if (selected != nullptr) {
        if (!snapshot_path.empty()) {
            renderer_.set_debug_capture_paths(snapshot_path);
        }
        renderer_.present(selected, !snapshot_path.empty());
        if (!snapshot_path.empty()) {
            renderer_.set_debug_capture_paths({});
        }
    }
    (void)renderer_.end_frame(
        selected != nullptr,
        [](void*, std::uint64_t fence_value) {
            galaxy::timeline::record(galaxy::timeline::Event::RenderSubmitted, 0u, fence_value);
        },
        nullptr, time.generated);
    galaxy::pacing::drop_description();
    if (selected != nullptr && real) {
        note_picture_shown(*selected);
        last_presented_slot_.store(selected->slot, std::memory_order_relaxed);
    } else if (selected != nullptr) {
        // A picture drawn between the game's frames is a frame shown too: the benchmark measures every one.
        galaxy::bench::note_new_picture(static_cast<std::uint64_t>(std::max<std::int64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(), 0)));
    }
    // Nonblocking: a later frame writes a finished present snapshot.
    renderer_.debug_log_backbuffer_readback();
    ++frame_index_;
    if (real) {
        release_lists_before(entry);
    }
}

void RenderCore::render_frame_on_thread(FrameChunk& chunk) {
    // An extension's lose_gpu_device: Direct3D 12 removes its device; the others have no call for that, so Vulkan's next
    // submission reports VK_ERROR_DEVICE_LOST, Direct3D 11's next present DXGI_ERROR_DEVICE_REMOVED and Metal's next
    // command buffer a lost device. All go on through the loss recovery below.
    if (g_lose_device.exchange(false, std::memory_order_acq_rel)) [[unlikely]] {
        asked_loss_frame_ = frame_index_;
        std::cerr << "[gpu] device lost (asked) at frame " << frame_index_ << '\n';
        renderer_.lose_device();
    }
    {
        // Interpolated frames: a present rate above 60. Decided per list.
        const RenderConfig cfg = get_render_config();
        const double rate = present_frame_rate(presented_max_fps(cfg), cfg.vsync_interval, renderer_.display_refresh_hz());
        interp_on_ = renderer_.interpolation_mode();
        const unsigned frames = interp_on_ ? interpolated_frames_per_game_frame(rate) : 0u;
        g_in_between_on.store(frames != 0u, std::memory_order_relaxed);
        if (frames != schedule_.configured()) {
            schedule_.configure(frames, galaxy::qpc_now());
        }
        if (!interp_on_) {
            schedule_.forget_walked();
        }
    }
    ListEntry& entry = list_arrived(chunk);
    const NebulaFrameList& list = *chunk.list;
    bool shows = (list.flags & NEBULA_FRAME_HOLD) == 0u;
    if (shows) {
        shows = false;
        for (std::uint32_t p = list.pass_count; p != 0u && !shows; --p) {
            shows = list.passes[p - 1u].kind == NEBULA_PASS_SHOW;
        }
    }
    if (interp_on_ && shows && !interp_stopping_ && chunk.shows && !(chunk.present_swap_chain && chunk.black)) {
        // It walks on the display's clock, after the frames between it and the list before: its sequence starts as it
        // arrives (I-004), on its own step's moment. A present already asked for it only feeds the cadence count.
        start_interp_sequence(entry, chunk.list_asked_qpc, chunk.list_step_qpc);
        interp_list_shown_ = true;
        if (chunk.present_swap_chain) {
            schedule_.note_step(chunk.present_asked_qpc, game_wait_qpc_.load(std::memory_order_relaxed));
        }
        g_step_work.record(chunk.list_step_qpc, galaxy::qpc_now());
        advance_interp_sequence(false);
        return;
    }
    // A list the game does not show yet (black, the start skipping frames), shown by the present the next step asks
    // for, or none with in-between pictures: walked as it comes, after the sequences end.
    interp_list_shown_ = false;
    end_interp_sequences();

    // Presents with this frame only when asked or when a frame's end presents (not while the output is black, the settings
    // overlay draws over every present, or a temporal upscaler or frame generation keeps its own time).
    render_pointer_response_ = entry.pointer_response;
    render_movie_content_ = entry.prerecorded_movie;
    render_movie_frame_ = entry.movie_frame;
    begin_walk_frame(entry);
    const std::int64_t walk_began = calibration_.stage != 0u ? galaxy::qpc_now() : 0;
    galaxy::timeline::record(galaxy::timeline::Event::WalkBegin, entry.data.serial, 0u);
    galaxy::pacing::walk_began();
    walk_list(entry.data, entry.before != nullptr ? &entry.before->data : nullptr, core::WalkTime{});
    galaxy::pacing::walk_ended();
    galaxy::timeline::record(galaxy::timeline::Event::WalkEnd, entry.data.serial, 0u);
    if (calibration_.stage != 0u) [[unlikely]] {
        calibrate_first_start((list.flags & NEBULA_FRAME_HAS_SCENE) != 0u,
                              static_cast<double>(galaxy::qpc_now() - walk_began) * 1000.0 / static_cast<double>(galaxy::qpc_frequency()));
    }

    const bool frame_end_present = !chunk.present_swap_chain && picture_made_in_chunk_ && frame_end_presents_.load(std::memory_order_relaxed) &&
                              !interp_on_ && !black_shown_ && !native_settings_overlay_visible() && !renderer_.temporal_mode() &&
                              !frame_generation_running();
    const bool presents = chunk.present_swap_chain || frame_end_present;
    const DisplayTexture* selected_display = chunk.black ? nullptr : display_pictures_.latest();
    const bool duplicate_display_present =
        selected_display != nullptr && selected_display->picture_number == last_presented_picture_.load(std::memory_order_relaxed);
    const bool skip_duplicate_present = presents && duplicate_display_present && !native_settings_overlay_visible();
    const bool skip_rate_present =
        presents && selected_display != nullptr && !skip_duplicate_present && !native_settings_overlay_visible() && skip_for_present_rate();
    const bool present_swap_chain = presents && selected_display != nullptr && !skip_duplicate_present && !skip_rate_present;
    const bool present_black = chunk.present_swap_chain && chunk.black;
    if (!present_black && presents) {
        black_shown_ = false;
    }
    if (interp_on_ && present_swap_chain && !duplicate_display_present) {
        note_interp_real_present();
    }
    if (present_swap_chain) {
        const std::string snapshot_path = present_snapshot_path(duplicate_display_present ? NEBULA_EXT_PICTURE_REPEAT : NEBULA_EXT_PICTURE_GAME);
        if (!snapshot_path.empty()) {
            renderer_.set_debug_capture_paths(snapshot_path);
        }
        {
            const std::int64_t asked = chunk.present_asked_qpc != 0 ? chunk.present_asked_qpc : galaxy::qpc_now();
            galaxy::pacing::describe(asked, content_moment_qpc(chunk.step_moment_qpc, 1.0, asked), chunk.step_moment_qpc,
                                     false);
        }
        renderer_.present(selected_display, !snapshot_path.empty());
        if (!snapshot_path.empty()) {
            renderer_.set_debug_capture_paths({});
        }
    } else if (present_black) {
        present_black_picture(true);
    }
    (void)renderer_.end_frame(
        present_swap_chain,
        [](void*, std::uint64_t fence_value) {
            galaxy::timeline::record(galaxy::timeline::Event::RenderSubmitted, 0u, fence_value);
        },
        nullptr);
    galaxy::pacing::drop_description();
    if (presents && present_swap_chain && selected_display != nullptr) {
        note_picture_shown(*selected_display);
        last_presented_slot_.store(selected_display->slot, std::memory_order_relaxed);
    }
    // The step's list walked and handed to the display: its work ends here for the host's frame phase.
    g_step_work.record(chunk.list_step_qpc, galaxy::qpc_now());
    // Nonblocking: a later frame writes a finished present snapshot.
    renderer_.debug_log_backbuffer_readback();
    ++frame_index_;
    release_lists_before(entry);
}


// The first start's calibration, once a real list: in its first 3D scene, once the start's pipelines are made, each of two
// internal heights is shown kCalibrationSettle frames (the resize and the pass timers' 0.1 average settle) and timed
// kCalibrationSamples frames (the pass timers' sum); the fit's largest height that fits a heavy stage is kept. What is kept:
//   no GPU timestamps, the CPU the limit, no fit      the start's height
//   a height of the INTERNAL RESOLUTION row fits      the largest that does (match display when the panel's own fits)
//   even 480 lines is over the budget                 the PERFORMANCE preset (apply_performance_preset); 480 lines alone
//                                                     when any setting changed since the start
// Only the height is written, onto the settings as they are then.
void RenderCore::calibrate_first_start(bool scene, double walk_ms) {
    constexpr unsigned kCalibrationSettle = 45u;  // 0.9^45: under 1 % of the height before left in the timers' average
    // Timings started again for changed settings before the start's height is left and nothing is measured: each costs at
    // most two heights' 75 frames.
    constexpr unsigned kCalibrationRestarts = 3u;
    constexpr unsigned kCalibrationSamples = 30u;
    FirstStartCalibration& c = calibration_;
    if (galaxy::bench::running()) [[unlikely]] {
        end_first_start_calibration(0u, nullptr);  // a benchmark's heights are its own: nothing set, nothing saved
        return;
    }
    const RenderConfig chosen = get_render_config_chosen();
    if (!(chosen == c.armed)) {
        if (chosen.internal_height != c.armed.internal_height || chosen.upscaler != c.armed.upscaler) {
            // The player chose the resolution (the menu, Nebula Settings): theirs stands, and nothing is set or saved.
            end_first_start_calibration(0u, nullptr);
            return;
        }
        // Another setting the player chose is taken in. The
        // pictures timed so far were another config's: timed again from the first height.
        c.armed = chosen;
        c.changed = true;
        if (c.stage >= 2u) {
            if (!c.timing_was_on) {
                renderer_.set_gpu_timing(false);
            }
            ++c.restarts;
            c.stage = 1u;
            c.first_height = chosen.internal_height == c.heights[0];
            c.frames = 0u;
            c.pixels.clear();
            c.ms.clear();
            c.cpu_over = 0.0;
            c.list_qpc = 0;
            if (c.restarts <= kCalibrationRestarts) {
                std::fprintf(stderr, "[settings] first start: a setting changed while measuring: measured again (%u of %u)\n",
                             c.restarts, kCalibrationRestarts);
            }
        }
    }
    // The settings menu over the scene: its pictures are not the scene's alone, and a height set while it is open would not
    // be the one its draft holds (an apply would write the draft's back as the player's). Held, then settled again.
    if (native_settings_overlay_visible()) {
        c.frames = 0u;
        return;
    }
    if (!scene) {
        return;
    }
    if (c.stage == 1u) {
        if (pipelines_.pending() != 0u) {
            return;
        }
        if (!c.first_height) {
            RenderConfig first = chosen;
            first.internal_height = c.heights[0];
            set_render_config(first);
            c.armed = get_render_config_chosen();
            c.first_height = true;
            return;
        }
        if (c.restarts > kCalibrationRestarts) {
            std::fprintf(stderr,
                         "[settings] first start: settings changed %u times while measuring: internal height %u stays, "
                         "nothing saved\n",
                         c.restarts, c.start);
            end_first_start_calibration(0u, nullptr);
            return;
        }
        c.timing_was_on = renderer_.gpu_timing_on();
        renderer_.set_gpu_timing(true);
        c.stage = 2u;
        c.frames = 0u;
        return;
    }
    // A pipeline made during the timing costs GPU time that is not the height's: the height starts over.
    if (pipelines_.pending() != 0u) {
        c.frames = 0u;
        return;
    }
    const std::int64_t now = galaxy::qpc_now();
    const std::uint64_t wait = game_wait_qpc_.load(std::memory_order_relaxed);
    const double game_ms = c.list_qpc != 0
                               ? static_cast<double>((now - c.list_qpc) - static_cast<std::int64_t>(wait - c.list_wait)) * 1000.0 /
                                     static_cast<double>(galaxy::qpc_frequency())
                               : 0.0;
    c.list_qpc = now;
    c.list_wait = wait;
    if (++c.frames <= kCalibrationSettle) {
        return;
    }
    GpuPassTime passes[32];
    const std::size_t count = renderer_.gpu_pass_times(passes, std::size(passes));
    double gpu_ms = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        gpu_ms += passes[i].ms;
    }
    if (gpu_ms <= 0.0) {
        end_first_start_calibration(c.start, "no GPU timestamps");
        return;
    }
    // One new picture a game step (the presents between show it again) with interpolation off, as a first start's defaults
    // leave it: the step is the GPU's period (resolution_fit.h's budget, the benchmark's too).
    constexpr double frame_ms = 1000.0 / galaxy::kGameStepsPerSecond;
    // The scene target as the renderer made it: a size it refused (the video memory check) shows as no spread, so no fit.
    c.pixels.push_back(static_cast<double>(renderer_.frame_map().width) * static_cast<double>(renderer_.frame_map().height));
    c.ms.push_back(gpu_ms);
    // The CPU's part: the walk near the step, or the game thread late on its own (its list period, less its waits on the
    // render thread, past 1.5 steps: FrameSchedule's cadence rule; the host's pacing sleep is in the period).
    c.cpu_over += walk_ms > kFitFrameShare * frame_ms || game_ms > 1.5 * frame_ms ? 1.0 : 0.0;
    if (c.frames < kCalibrationSettle + kCalibrationSamples) {
        return;
    }
    if (c.stage == 2u) {
        RenderConfig next = chosen;
        next.internal_height = c.heights[1];
        set_render_config(next);
        c.armed = get_render_config_chosen();
        c.stage = 3u;
        c.frames = 0u;
        c.list_qpc = 0;
        return;
    }
    // Most frames with the walk near the frame time or the game late on its own: the CPU sets the rate, not the pixels.
    if (c.cpu_over * 2.0 > static_cast<double>(c.ms.size())) {
        end_first_start_calibration(c.start, "the CPU is the limit");
        return;
    }
    const GpuTimeFit fit = fit_gpu_time(c.pixels.data(), c.ms.data(), c.ms.size());
    const double budget_ms = kFitFrameShare * frame_ms;
    const unsigned height = largest_fitting_height(fit, c.width, c.lines, kFitHeavyStage, budget_ms);
    std::fprintf(stderr, "[settings] first start measured: gpu ms = %.3f + %.3f a million pixels (%zu frames), a frame %.2f ms\n",
                 fit.a_ms, fit.b_ms_per_pixel * 1e6, c.ms.size(), frame_ms);
    if (fit.valid && c.lines > kFitLowestHeight &&
        predicted_gpu_ms(fit, scene_pixels(kFitLowestHeight, c.width, c.lines)) * kFitHeavyStage > budget_ms) {
        end_first_start_calibration(kFitLowestHeight, "even 480 lines is over the budget: the PERFORMANCE preset", true);
        return;
    }
    end_first_start_calibration(fit.valid ? height : c.start, fit.valid ? "measured" : "no fit");
}

// `why` null: nothing is set or saved (the player chose the resolution, a benchmark runs, settings changed too often).
// Otherwise `height` alone is written onto the settings as they are now, every other field the player's; `performance`:
// the PERFORMANCE preset with it, only when no setting changed since the calibration armed.
void RenderCore::end_first_start_calibration(unsigned height, const char* why, bool performance) {
    FirstStartCalibration& c = calibration_;
    if (c.stage >= 2u && !c.timing_was_on) {
        renderer_.set_gpu_timing(false);
    }
    c.stage = 0u;
    c.pixels = {};
    c.ms = {};
    if (why == nullptr) {
        return;
    }
    RenderConfig chosen = get_render_config_chosen();
    if (chosen.internal_height != c.armed.internal_height || chosen.upscaler != c.armed.upscaler) {
        std::fprintf(stderr, "[settings] first start: the player chose the resolution, nothing kept\n");
        return;
    }
    const bool preset = performance && !c.changed && chosen == c.armed;
    if (preset) {
        apply_performance_preset(chosen);
    }
    chosen.internal_height = height;
    set_render_config(chosen);
    (void)save_render_config_to_file();
    std::fprintf(stderr, "[settings] first start: internal height %u kept (%s%s)\n", height, why,
                 performance && !preset ? "; a setting changed since the start: the height alone" : "");
}

void boot_step(const char* step) noexcept {
    static std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    std::fprintf(stderr, "[boot] renderer %s %.0f ms\n", step,
                 std::chrono::duration<double, std::milli>(now - last).count());
    last = now;
}

void note_layer_2d_lines(unsigned lines) noexcept {
    s_layer_2d_lines.store(lines, std::memory_order_relaxed);
}

unsigned layer_2d_lines() noexcept {
    return s_layer_2d_lines.load(std::memory_order_relaxed);
}


// With the frame's list, so that a picture's number is its frame's whatever the render thread is doing.
}  // namespace galaxy::render
