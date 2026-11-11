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
#include "galaxy/loss_report.h"
#include "galaxy/crash_report.h"
#include "galaxy/host_extension.h"
#include "galaxy/platform_time.h"
#include "galaxy/platform_paths.h"
#include "galaxy/perf_hint.h"

#include "galaxy/render/present_clock.h"
#include "galaxy/portable.h"
#include "galaxy/render/render_core.h"
#include "galaxy/render/host_window.h"

#include "galaxy/experimental_ultrawide_aspect.h"
#include "galaxy/content_viewport.h"
#include "galaxy/render/render_config.h"
#include "galaxy/windows_power.h"
#include "galaxy/frame_timeline.h"

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
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>

namespace galaxy::render {

SamplerKey make_sampler_key(const TextureSampler& mode, bool generated_mips, std::uint32_t mip_levels,
                            float lod_shift, bool arbitrary, float mip_lines_log2, float scene_bias) noexcept {
    std::uint32_t min_filter = min_filter_code(mode);
    float min_lod = mode.min_lod, max_lod = mode.max_lod;
    const bool source_mips = (min_filter & 3u) != 0u;
    bool full_chain = false;
    if (generated_mips && mip_levels > 1u && min_filter == 4u && mode.mag_linear) {
        min_filter = 6u;
        max_lod = static_cast<float>(mip_levels - 1u);
        full_chain = true;
    }
    float bias = mode.lod_bias + (source_mips || full_chain ? scene_bias : 0.0f);
    if (arbitrary && source_mips) {
        bias += mip_lines_log2 - lod_shift;
        max_lod = static_cast<float>(mip_levels != 0u ? mip_levels - 1u : 0u);
        min_filter = (min_filter & 4u) | 3u;
    } else if (lod_shift != 0.0f) {
        if (source_mips) {
            min_lod = min_lod == 0.0f ? 0.0f : min_lod + lod_shift;
            max_lod += lod_shift;
        } else if (!full_chain) {
            min_filter = min_filter >= 4u ? 6u : 1u;
            min_lod = 0.0f; max_lod = lod_shift;
        }
    }
    const std::uint32_t filter = static_cast<std::uint32_t>(mode.wrap_s)
        | static_cast<std::uint32_t>(mode.wrap_t) << 2u | (mode.mag_linear ? 1u << 4u : 0u) | min_filter << 5u;
    return {filter, min_lod, max_lod, bias};
}

void boot_step(const char* step) noexcept;

namespace {


std::atomic<unsigned> s_layer_2d_lines{0u};

// lose_gpu_device's request, taken at the renderer's next frame.
std::atomic<bool> g_lose_device{false};

// Render thread: the game frame of the newest list drawn (NebulaFrameList.game_frame).
std::uint32_t g_drawn_game_frame = 0u;

// The file an extension keeps this present of the game's picture in (host_extension.h), or none. `kind` is a
// NEBULA_EXT_PICTURE_*: the picture the game made shown for the first time, shown again, or a frame generated between
// two of the game's, so a capture with interpolation on compares with one with it off by its game frames alone
// (tools/verify/interp_real.sh).
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

// The vblanks per present slot for a rate on a display of `refresh` hertz: a whole number, the rate kept above 60.
std::int64_t vblanks_per_slot(double refresh, double rate) {
    std::int64_t slot = std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil(refresh / rate - 0.02)));
    if (refresh / static_cast<double>(slot) < kGameStepRate + 1.0) {
        slot = std::max<std::int64_t>(1, static_cast<std::int64_t>(refresh / (kGameStepRate + 1.0)));
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

}  // anonymous namespace

void lose_gpu_device() noexcept {
    g_lose_device.store(true, std::memory_order_release);
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

    const RenderConfig cfg = get_render_config();

    boot_step("begins");
    // The scene at the window's picture: its exact size, square pixels.
    const auto start_aspect = galaxy::experimental_effective_host_aspect();
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
    rt_start();

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

// --diagnostics memory: every 600 presented frames, what the renderer holds, in MB.
void RenderCore::trace_memory() {
    const bool enabled = ::galaxy::diagnosing(::galaxy::kDiagnoseMemory);
    static std::uint64_t frames = 0u;
    if (!enabled || ++frames % 600u != 0u) {
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
    g_last_present_ns.store(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count(), std::memory_order_relaxed);
    record_pointer_response(texture);
    last_presented_display_stamp_.store(
        texture.frame_stamp, std::memory_order_relaxed);
    last_presented_picture_.store(
        texture.picture_number, std::memory_order_relaxed);
    ++stat_presents_;
    if (texture.last_shown_number == texture.picture_number) {
        ++stat_pictures_shown_again_;
        return;
    }
    texture.last_shown_number = texture.picture_number;
    ++stat_new_pictures_shown_;
    record_monotonic_cadence_sample(new_picture_shown_cadence_);
    report_frame_tail_window();
    trace_memory();
}

// Producer (simulation thread): copies the frame and hands it to the render thread. Blocks only
// while the queue is full.
void RenderCore::render_frame(bool present_swap_chain, bool black) {
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
        const auto hold = device_gate_.hold();
        chunk.device_epoch = device_gate_.epoch();
        // Registries first: a table taken here names textures whose work is taken too.
        chunk.registry_ops = registries_.take_ops();
        chunk.texture_ops = texture_store_.take_ops();
    }
    chunk.prerecorded_movie=producer_movie_content_;
    chunk.movie_frame=producer_movie_frame_;
    chunk.present_swap_chain = present_swap_chain;
    chunk.black = black;
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
RtView RenderCore::scene_depth_view(const float terms[6], const ScaledViewport& viewport) const {
    const auto projection = [terms](unsigned i) {
        return terms[i];
    };
    // fill_vs_projection: D3D clip z = -(z E + F) (1 - 1e-7).
    constexpr float kFarEps = 1.0f - 1e-7f;
    RtView view{};
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

void RenderCore::run_pending_interp_start() {
    if (!interp_start_pending_) {
        return;
    }
    interp_start_pending_ = false;
    supersede_interp_sequence();
    if (interp_list_waiting_) {
        interp_list_waiting_ = false;
        start_interp_sequence();
        advance_interp_sequence(false);
    }
}

void RenderCore::start_interp_sequence() {
    static const std::int64_t frequency = galaxy::qpc_frequency();
    const std::int64_t now = galaxy::qpc_now();
    const RenderConfig cfg = get_render_config();
    double rate = std::max(kGameStepRate + 1.0, present_frame_rate(
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
    note_game_frame_cadence(now);
    interp_sequence_.active = true;
    interp_sequence_.flush_requested = false;
    interp_sequence_.entry = interp_waiting_entry_;
    interp_sequence_.generated = interp_gpu_governor(rate);
    if (on_vblanks) {
        fit_rate_to_vblanks();  // a rate the governors lowered
    }
    interp_sequence_.next = 0u;
    // Present slots run on one clock at the present rate. A slot shows the
    // game at t = (slot - arrival) * 60 + lead between the previous frame (0)
    // and this one (1); the first slot with t >= 1 presents this frame
    // itself. Integer multiples of 60 lock the clock to each arrival with
    // lead 60 / rate: k generated frames at i / (k + 1), then the frame, on time.
    // Other rates keep a free-running clock with lead 0: content time is
    // then linear in present time (the next arrival ends the sequence at
    // t -> 1), where a lead would show the frame up to one slot early and
    // step the content unevenly around it. A clock left behind by a pause
    // restarts.
    interp_slot_period_qpc_ = on_vblanks ? vblanks_per_slot * vblank_period
                                         : static_cast<std::int64_t>(static_cast<double>(frequency) / rate);
    const bool locked = !on_vblanks && std::abs(rate / kGameStepRate - std::round(rate / kGameStepRate)) < 0.02;
    // The arrival clock: the render thread sees a frame's present when it
    // reaches the queue (a chunk in progress delays it by milliseconds), so
    // arrivals are the game's frame cadence, smoothed: the prediction one or
    // more frame periods on, pulled 1/8 toward the measured arrival, with the
    // period tracking the game's rate. A miss by a third of a period or more
    // (pause, hitch) restarts it at the measured time.
    if (interp_frame_period_qpc_ == 0) {
        interp_frame_period_qpc_ = frequency / kGameStepsPerSecond;
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
                interp_frame_period_qpc_ + error / (64 * periods), frequency / (kGameStepsPerSecond + 2), frequency / (kGameStepsPerSecond - 2));
        }
    }
    interp_arrival_qpc_ = arrival;
    interp_sequence_.frame_qpc = arrival;
    interp_sequence_.lead = locked ? kGameStepRate / rate : 0.0;
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
    // With no generated pictures there is no interval to interpolate. A smoothed arrival slightly in the
    // future must not hold the only picture back while a busy queue replaces it with another sequence.
    if (interp_sequence_.generated == 0u) {
        interp_slot_due_qpc_ = std::min(interp_slot_due_qpc_, now);
    }
}

// As many generated frames per game frame as the GPU fits in nine tenths of a game step. A generated frame
// walks the whole frame list, so a game frame costs about (k + 1) generated frame times. k drops at once
// when over budget and rises after 2 s with current native scene timings (10 s for temporal reconstruction).
// Only temporal reconstruction needs a generated-frame retry; native scene frames keep its cost estimate live.
// A capped sequence runs the slot clock at 60 * (k + 1).
unsigned RenderCore::interp_gpu_governor(double& rate) {
    constexpr double budget_ms = 0.9 * 1000.0 / kGameStepRate;
    const unsigned configured = interp_stopping_ ? 0u : interp_frames_;
    if (configured == 0u) {
        return configured;
    }
    const unsigned allowed = std::min(configured, interp_cadence_cap_);
    const std::uint64_t serial = interp_sequence_serial_;
    if (serial - interp_gpu_checked_ >= 30u && galaxy::qpc_now() >= interp_gpu_warm_until_qpc_) {
        interp_gpu_checked_ = serial;
        const double generated_ms = renderer_.interpolation_gpu_cost_ms();
        if (generated_ms > 0.0) {
            // With the temporal upscaler on generated frames the frame at t = 1
            // is one too; when no generated frame fits beside it, the game frames take
            // the upscaler instead (the same frames for less GPU time).
            const bool upscaled = renderer_.temporal_interpolation();
            const double fit = std::floor(budget_ms / generated_ms) - (upscaled ? 2.0 : 1.0);
            if (upscaled && fit < 1.0) {
                renderer_.suspend_temporal_interpolation(generated_ms);
            }
            const unsigned fits = fit < 1.0 ? 0u : static_cast<unsigned>(std::min(fit, 7.0));
            if (fits < interp_gpu_cap_) {
                interp_gpu_cap_ = fits;
                interp_gpu_cap_serial_ = serial;
            } else if (fits > interp_gpu_cap_ && serial - interp_gpu_cap_serial_ >=
                       (renderer_.temporal_interpolation() ? 600u : 120u)) {
                interp_gpu_cap_ = std::min(fits, interp_step_up(interp_gpu_cap_));
                interp_gpu_cap_serial_ = serial;
            }
        }
        // Temporal reconstruction needs its own generated-frame measurement. Native replay has a fresh
        // measurement from every completed scene frame, so never inject a known over-budget probe into play.
        if (renderer_.temporal_interpolation() && interp_gpu_cap_ == 0u && serial - interp_gpu_cap_serial_ >= 1200u) {
            interp_gpu_cap_ = 1u;  // measure one generated frame again
            interp_gpu_cap_serial_ = serial;
            renderer_.reset_generated_gpu_ema();
        }
    }
    const unsigned generated = std::min(allowed, interp_gpu_cap_);
    if (generated >= configured) {
        return configured;
    }
    ++interp_gpu_capped_;
    rate = kGameStepRate * static_cast<double>(generated + 1u);
    return generated;
}

// Generated frames must never cost the game its own frames. Each game frame shown is timed against the frame period: an
// interval above 1.5 periods missed VIs. A miss is the generated frames' when the interval would not have missed without
// their render-thread time: the game waited on the render thread, and would have waited that much less (at most what
// it waited). The rest are the game's own, as a scene's first frames, whose decoding, uploads and pipelines keep the
// render thread far longer than generated frames do. Two or more of the generated frames' misses a second, and no fewer than the
// game's own, take generated frames away to the next lower present rate (to half of them at eight). The game's own misses
// three seconds in a row above their lowest rate are probed, for threads that share a CPU's cores: the next lower
// rate for a second, kept only if the misses halve. A try of the next higher rate is undone at its first miss of the
// generated frames'. Tries and probes follow a clean interval that doubles after each failed one (2 s to 64 s: a scene's
// first frames back the render thread up for a second or two) and halves after a try that held.
void RenderCore::restart_interp_governors() {
    const std::int64_t now = galaxy::qpc_now();
    const std::int64_t frequency = galaxy::qpc_frequency();
    interp_gpu_cap_ = 7u;
    interp_gpu_cap_serial_ = interp_sequence_serial_;
    interp_gpu_checked_ = interp_sequence_serial_;
    interp_gpu_warm_until_qpc_ = now + frequency;  // one second of generated-frame times left out
    renderer_.reset_generated_gpu_ema();
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
    const bool trace = ::galaxy::diagnosing(::galaxy::kDiagnosePresentStats);
    // The game frame period the arrivals follow (start_interp_sequence's smoothed estimate), else the game step.
    const std::int64_t period = interp_frame_period_qpc_ != 0 ? interp_frame_period_qpc_ : frequency / galaxy::kGameStepsPerSecond;
    // Since the frame before: the game's waits on the render thread, and the render thread's generated frame time.
    const std::uint64_t waited_total = game_wait_qpc_.load(std::memory_order_relaxed);
    const auto waited = static_cast<std::int64_t>(waited_total - interp_cadence_wait_seen_);
    const auto generated_time = static_cast<std::int64_t>(interp_generated_qpc_ - interp_cadence_generated_seen_);
    interp_cadence_wait_seen_ = waited_total;
    interp_cadence_generated_seen_ = interp_generated_qpc_;
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
    const std::uint32_t own = missed_in(interval - std::min(waited, generated_time));
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
        // The walk at t = 1 alone costs the game its frames when it takes a
        // quarter of a frame or more.
        const double generated_ms = renderer_.interpolation_gpu_cost_ms();
        if (cap == 0u && renderer_.temporal_interpolation() && generated_ms * 4.0 >= 1000.0 / kGameStepRate) {
            renderer_.suspend_temporal_interpolation(generated_ms);
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
        std::cerr << "[interp-cadence] window generated-misses=" << interp_cadence_misses_
                  << " own-misses=" << interp_cadence_own_ << " running=" << running << " game-wait-ms="
                  << static_cast<double>(interp_cadence_wait_qpc_) * 1000.0 / static_cast<double>(frequency)
                  << " seconds=" << seconds << '\n';
    }
    restart_window();
    if (interp_cadence_probe_) {
        // The probe's second: misses that halved with fewer generated frames were theirs, and the lower rate stays;
        // otherwise they are the game's own at that level, and the generated frames come back.
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

// The present rate with `generated` generated frames per game frame (start_interp_sequence): with all of them the target,
// else 60 a second for the frame and each generated frame; with VSync on timed vblanks, a whole number of vblanks a slot.
double RenderCore::interp_rate_with(unsigned generated) const {
    static const std::int64_t frequency = galaxy::qpc_frequency();
    if (generated == 0u) {
        return kGameStepRate;
    }
    const RenderConfig cfg = get_render_config();
    double rate = generated < interp_frames_
        ? kGameStepRate * static_cast<double>(generated + 1u)
        : std::max(kGameStepRate + 1.0, present_frame_rate(presented_max_fps(cfg), cfg.vsync_interval, renderer_.display_refresh_hz()));
    std::int64_t vblank = 0;
    std::int64_t vblank_period = 0;
    if (cfg.vsync_interval != 0u && renderer_.vblank_grid(vblank, vblank_period) && vblank_period > 0) {
        const double refresh = static_cast<double>(frequency) / static_cast<double>(vblank_period);
        rate = refresh / static_cast<double>(vblanks_per_slot(refresh, rate));
    }
    return rate;
}

// The most generated frames below `generated` that present at a lower rate (on a 240 Hz display two show 120 Hz, as one
// does): 0 when none.
unsigned RenderCore::interp_step_down(unsigned generated) const {
    const unsigned from = std::min(generated, interp_frames_);
    const double rate = interp_rate_with(from);
    for (unsigned k = from; k > 0u; --k) {
        if (interp_rate_with(k - 1u) < rate - 0.5) {
            return k - 1u;
        }
    }
    return 0u;
}

// The fewest generated frames above `generated` that present at a higher rate: all of them when none.
unsigned RenderCore::interp_step_up(unsigned generated) const {
    const double rate = interp_rate_with(std::min(generated, interp_frames_));
    for (unsigned k = generated + 1u; k < interp_frames_; ++k) {
        if (interp_rate_with(k) > rate + 0.5) {
            return k;
        }
    }
    return interp_frames_;
}

// The next frame's sequence replaces this one before its real frame (at
// rates that are not multiples of 60 that is the normal end: the next
// frame's generated frames continue from t = 0, its predecessor's image).
void RenderCore::supersede_interp_sequence() {
    if (!interp_sequence_.active) {
        return;
    }
    interp_sequence_.active = false;
    ++interp_superseded_;
    // What it writes, later frames read: its own time is walked, unseen.
    walk_frame(*interp_sequence_.entry, core::WalkTime{}, false);
    interp_shown_list_ = nullptr;
}

// t of the next slot in the playing sequence (see advance_interp_sequence).
double RenderCore::interp_slot_t() const {
    static const std::int64_t frequency = galaxy::qpc_frequency();
    return static_cast<double>(interp_slot_due_qpc_ - interp_sequence_.frame_qpc) * kGameStepRate /
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
        const double t = std::max(0.0, static_cast<double>(slot - sequence.frame_qpc) * kGameStepRate /
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
        if (!flush && t < 1.0 - 1e-3 && sequence.next < 7u && sequence.generated != 0u) {
            const auto start = std::chrono::steady_clock::now();
            const std::int64_t generated_start = galaxy::qpc_now();
            const bool presented = render_generated_frame(static_cast<float>(t));
            interp_generated_qpc_ += static_cast<std::uint64_t>(galaxy::qpc_now() - generated_start);
            interp_cpu_ms_ += std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
            if (presented) {
                ++interp_presented_;
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
        render_generated_frame(1.0f);
        note_present_vblank(slot);
        interp_presenting_real_ = false;
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
    const std::int64_t now = galaxy::qpc_now();
    note_game_frame_cadence(now);
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
    message << "[interp-stats] generated-per-frame=" << interp_frames_
            << " sequences=" << interp_sequences_
            << " presented-hz=" << presented / seconds
            << " dropped=" << interp_dropped_
            << " late-slots=" << interp_late_slots_
            << " vblank-steps=" << interp_vblank_steps_[0] << '/' << interp_vblank_steps_[1] << '/'
            << interp_vblank_steps_[2] << '/' << interp_vblank_steps_[3]
            << " slot-to-vblank-ms=" << interp_slot_to_vblank_min_ms_ << '/' << interp_slot_to_vblank_max_ms_
            << " superseded=" << interp_superseded_
            << " started=" << interp_started_
            << " lists-skipped=" << interp_lists_skipped_
            << " presents-during-sequence=" << interp_present_skipped_
            << " failed=" << interp_failed_
            << " history-resets=" << interp_history_resets_
            << " history-carried=" << interp_history_carried_
            << " real-only=" << real_only
            << " (overlay=" << interp_real_only_[0] << " other=" << interp_real_only_[1] << ")"
            << " motion(camera-cuts/game-cuts/teleports/flips)=" << pairing_.camera_cuts << '/' << pairing_.game_cuts << '/'
            << pairing_.teleports << '/' << pairing_.flips
            << " wall-ms-per-generated=" << (interp_presented_ != 0u ? interp_cpu_ms_ / presented : 0.0)
            << " gpu-ms-per-generated=" << (gpu_samples != 0u ? gpu_ms / static_cast<double>(gpu_samples) : 0.0)
            << " gpu-generated-cap=" << interp_gpu_cap_
            << " cadence-generated-cap=" << interp_cadence_cap_
            << " cadence-drops=" << std::exchange(interp_cadence_drops_, 0u)
            << " game-fps=" << interp_cadence_fps_
            << " capped-sequences=" << std::exchange(interp_gpu_capped_, 0u)
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
    std::cerr << message.str();
    interp_presented_ = 0u;
    interp_dropped_ = 0u;
    interp_late_slots_ = 0u;
    interp_vblank_steps_ = {};
    interp_slot_to_vblank_samples_ = 0u;
    interp_superseded_ = 0u;
    interp_started_ = 0u;
    interp_lists_skipped_ = 0u;
    interp_present_skipped_ = 0u;
    interp_failed_ = 0u;
    interp_history_resets_ = 0u;
    interp_history_carried_ = 0u;
    interp_sequences_ = 0u;
    interp_real_only_ = {};
    interp_cpu_ms_ = 0.0;
    pairing_.camera_cuts = pairing_.game_cuts = pairing_.teleports = pairing_.flips = 0u;
    interp_stats_qpc_ = now;
}

bool RenderCore::render_generated_frame(float t) {
    InterpSequence& sequence = interp_sequence_;
    ListEntry& entry = *sequence.entry;
    core::WalkTime time;
    time.t = t;
    time.t_prev = t;
    time.generated = t < 1.0f;
    // The frame shown before, on this pair's clock: motion to it is what the upscaler and the motion targets see. Anything
    // else (a frame presented alone, a cut, skipped lists) starts the upscaler's history again.
    const bool cut = (entry.data.list->flags & NEBULA_FRAME_CAMERA_CUT) != 0u;
    const bool shown_before = interp_shown_list_ != nullptr && interp_shown_list_ == entry.before && !cut;
    if (interp_shown_serial_ == interp_sequence_serial_ && interp_shown_t_ < t) {
        time.t_prev = interp_shown_t_;
    } else if (interp_shown_t_ >= 1.0f && shown_before) {
        time.t_prev = 0.0f;
    } else if (shown_before && interp_shown_serial_ + 1u == interp_sequence_serial_) {
        time.t_prev = interp_shown_t_ - 1.0f;
        ++interp_history_carried_;
    } else {
        time.reset = true;
        ++interp_history_resets_;
    }
    walk_frame(entry, time, true);
    interp_shown_serial_ = interp_sequence_serial_;
    interp_shown_t_ = t;
    interp_shown_list_ = &entry;
    return true;
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
        if (!galaxy::crash::is_gpu_lost(lost.what())) {
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
                queued.list->released = 1u;
            }
        }
        frame_queue_.clear();
        render_chunk_started_ns_ = 0u;
        queue_space_cv_.notify_all();
    }
    if (in_hand_list_ != nullptr) {
        in_hand_list_->released = 1u;
        in_hand_list_ = nullptr;
    }
    for (const std::unique_ptr<ListEntry>& entry : lists_) {
        entry->data.list->released = 1u;
    }
    lists_.clear();
    free_lists_.clear();
    freed_textures_.clear();
    retired_tables_.clear();

    interp_sequence_ = {};
    interp_list_waiting_ = false;
    interp_waiting_entry_ = nullptr;
    interp_start_pending_ = false;
    interp_shown_list_ = nullptr;
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
    probe_flights_ = {};
    free_probe_slots_.clear();
    next_probe_slot_ = 0u;
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

bool RenderCore::restart_renderer(const FrameMap& frame, bool same_card) {
    Renderer::Kept kept = renderer_.take_apart_lost_device();
    std::destroy_at(&renderer_);
#if !defined(NEBULA_GPU_VULKAN) && !defined(NEBULA_GPU_METAL)
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
    try {
        std::unique_lock<std::shared_mutex> rebuilding = device_gate_.begin_rebuild();
        const FrameMap frame = renderer_.frame_map();
        pipelines_.shutdown();
        movie_playback_.shutdown();
        texture_store_.shutdown();
        rt_stop();
        registries_.lose_device();
        display_pictures_.shutdown();
        drop_device_state();

        // The driver takes a moment to come back.
        constexpr unsigned kTries = 5u;
        bool back = false;
        for (unsigned attempt = 0u; attempt < kTries && !back; ++attempt) {
            if (attempt != 0u) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
            }
            back = restart_renderer(frame, attempt + 1u != kTries);
        }
        if (!back || !display_pictures_.initialize(renderer_.device(), frame)) {
            throw std::runtime_error("[render] no device came back: DEVICE_LOST");
        }
        device_gate_.set_device(renderer_.device());
        texture_store_.restart(renderer_.device());
        registries_.restart(renderer_.device());
        pipelines_.initialize(renderer_.device(), renderer_.pipeline_root(), renderer_.pipeline_backend());
        rt_start();
        device_gate_.end_rebuild(rebuilding);
    } catch (...) {
        device_gate_.give_up();
        const std::lock_guard<std::mutex> lock(queue_mutex_);
        render_thread_error_ = std::current_exception();
        stop_render_thread_ = true;
        queue_space_cv_.notify_all();
        return false;
    }
    std::cerr << "[render] a new device in " << (galaxy::qpc_now() - started) * 1000 / galaxy::qpc_frequency() << " ms\n";
    if (asked_loss_frame_ != 0u) {
        std::cerr << "[gpu] recovered from the device loss asked at frame " << asked_loss_frame_ << " (rendering goes on)\n";
        asked_loss_frame_ = 0u;
    }
    return true;
}

void RenderCore::render_loop() {
    // Called with queue_mutex_ held.
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
            poll_screen_probes();
        } catch (...) {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            fail_queued_utilities(std::current_exception());
            break;
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
                if (interp_sequence_.active) {
                    supersede_interp_sequence();
                }
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
        bool idle_present = false;
        bool overlay_idle = false;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            const RenderConfig cfg = get_render_config();
            idle_present = !hidden && !background && cfg.frame_rate_decouple &&
                (cfg.vsync_interval != 0u || presented_max_fps(cfg) > 0.0f);
            const bool poll_probes = !probe_flights_.empty();
            // Draining for a stop, out of sight or behind another window: queued frames present without generated frames.
            interp_stopping_ = stop_render_thread_ || hidden || background;
            // A native overlay over a game showing no interpolated frames (the HOME Menu's paused game draws no 3D scene;
            // interpolation waits while the settings are open): the latest frame again at each display slot, so the
            // overlay keeps the rate play had instead of the game's 60 Hz frame rate.
            overlay_idle = !hidden && !background && !stop_render_thread_ && overlay_idle_presents();
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
                // A service waiting for a new card can't reach this thread any other way (DeviceGate).
                queue_cv_.wait_for(lock, std::chrono::milliseconds(hidden ? 100 : 250), [this] {
                    return stop_render_thread_ || !frame_queue_.empty();
                });
            }
            // A due slot goes before queued chunks, so presents keep their
            // cadence while a game frame's chunks stream in; except a slot
            // that would show the frame itself (t >= 1) while the next
            // frame's present is queued: that present starts its sequence.
            const auto due_slot_first = [this] {
                if (interp_remaining_us() > 0) return false;
                // Skipping the real endpoint is valid only after this sequence has shown an intermediate
                // picture. Otherwise a saturated GPU can render every endpoint unseen and starve presentation.
                if (interp_sequence_.next == 0u) return true;
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
            } else if (frame_queue_.front().list != nullptr && frame_queue_.front().device_epoch != device_gate_.epoch()) {
                // Made for the card that was lost.
                frame_queue_.front().list->released = 1u;
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
                        interp_list_waiting_ = false;
                        present_cached_display_on_thread(true, false);
                    } else if (interp_list_waiting_) {
                        supersede_interp_sequence();
                        interp_list_waiting_ = false;
                        start_interp_sequence();
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
    const std::lock_guard guard(probe_answers_lock_);
    const auto found = probe_answers_.find(channel);
    if (found == probe_answers_.end()) return 0u;
    const ScreenProbeAnswers& answers = found->second;
    const std::uint32_t count = std::min(answers.count, capacity);
    std::memcpy(probes, answers.probes.data(), count * sizeof(ScreenProbe));
    return count;
}

void RenderCore::record_screen_probes(std::uint32_t channel, const ScreenProbe* probes, std::uint32_t count, bool compared) {
    if (count == 0u) return;
    ScreenProbeFlight flight;
    flight.channel = channel;
    flight.count = count;
    flight.slot = free_probe_slots_.empty() ? next_probe_slot_++ : free_probe_slots_.back();
    if (!free_probe_slots_.empty()) free_probe_slots_.pop_back();
    flight.probes.assign(probes, probes + count);
    if (renderer_.record_screen_probes(flight.slot, flight.probes, compared)) {
        probe_flights_.push_back(std::move(flight));
    } else {
        free_probe_slots_.push_back(flight.slot);
    }
}

void RenderCore::poll_screen_probes() {
    while (!probe_flights_.empty()) {
        ScreenProbeFlight& flight = probe_flights_.front();
        const float* const values = renderer_.screen_probes_finished(flight.slot);
        if (values == nullptr) {
            return;
        }
        for (std::uint32_t i = 0; i < flight.count; ++i) {
            flight.probes[i].value = values[i];
        }
        {
            const std::lock_guard guard(probe_answers_lock_);
            ScreenProbeAnswers& answers = probe_answers_[flight.channel];
            answers.count = flight.count;
            answers.probes = flight.probes;
        }
        free_probe_slots_.push_back(flight.slot);
        probe_flights_.pop_front();
    }
}

void RenderCore::present_black_picture(bool in_frame) {
    const DisplayTexture* layout = display_pictures_.latest();
    if (layout == nullptr || (black_shown_ && !native_settings_overlay_visible())) {
        return;
    }
    const std::string snapshot_path = present_snapshot_path(NEBULA_EXT_PICTURE_GAME);
    if (!snapshot_path.empty()) {
        renderer_.set_debug_capture_paths({}, snapshot_path);
    }
    if (!in_frame) {
        renderer_.begin_frame();
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
    last_presented_picture_.store(~std::uint64_t{0}, std::memory_order_relaxed);
}

void RenderCore::present_latest_frame_on_thread() {
    present_cached_display_on_thread(false, true, false);
}

void RenderCore::present_cached_display_on_thread(
    bool black,
    bool sleep_if_missing,
    bool count_game_display_present,
    std::optional<std::uint32_t> slot) {
    if (black) {
        if (count_game_display_present) {
            present_black_picture(false);
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
        renderer_.set_debug_capture_paths({}, snapshot_path);
    }
    const bool game_frame_only =
        interp_on_ && count_game_display_present && !interp_presenting_real_ && !duplicate_stamp;
    if (game_frame_only) {
        note_interp_real_present();
    }
    renderer_.set_present_game_frame(game_frame_only);
    renderer_.begin_frame();
    // Present-only frames acquire no display picture textures, so DisplayPictures retirement advances with
    // render frames only; otherwise render-then-present would age pictures twice per frame.
    (void)renderer_.present(selected, !snapshot_path.empty());
    if (!snapshot_path.empty()) {
        renderer_.set_debug_capture_paths({}, {});
    }
    renderer_.end_frame();
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
    } else {
        entry = std::make_unique<ListEntry>();
    }
    entry->data.list = chunk.list;
    entry->data.serial = ++list_serial_;
    entry->before = lists_.empty() ? nullptr : lists_.back().get();
    if (::galaxy::diagnosing(::galaxy::kDiagnoseRtInputs)) {
        world_light_stats_.add(*chunk.list, entry->before != nullptr ? entry->before->data.list : nullptr);
        if (world_light_stats_.lists >= 300u) {
            world_light_stats_.report();
        }
    }
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
        lists_.front()->data.list->released = 1u;
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
    if (frame_index_ >= vram_log_at_) {
        vram_log_at_ = frame_index_ + 60u;
        const TextureStore::BudgetStats stats = texture_store_.budget_stats();
        const std::uint64_t budget_mb = budget >> 20u;
        const std::uint64_t left_out_mb = stats.reduced_bytes >> 20u;
        if (budget_mb != vram_logged_budget_mb_ || left_out_mb != vram_logged_left_out_mb_ || frame_index_ >= vram_report_at_) {
            vram_logged_budget_mb_ = budget_mb;
            vram_logged_left_out_mb_ = left_out_mb;
            vram_report_at_ = frame_index_ + 216000u;
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
    rt_follow_setting(render);
    // Ray tracing's sky and bounce light shades what screen-space occlusion would darken a second time; with that part
    // off nothing traced darkens it (the light pass's O is 1: 22.10), so the occlusion runs as without ray tracing.
    ambient_occlusion_ = render.ambient_occlusion && renderer_.ambient_occlusion_ready() &&
                         !(render.ray_tracing_enabled && rt_backend_.started() && rt_part(render, RtPart::Indirect) != 0u);
    rt_quality_ = render.rt_quality;
    for (unsigned part = 0; part < 6u; ++part) {
        rt_levels_[part] = static_cast<std::uint8_t>(rt_part(render, static_cast<RtPart>(part)));
    }
    // The complete native list is available before begin_frame can resize a
    // ring. Include all eye/world rows, even rows a particular program omits.
    const NebulaFrameList& list = *entry.data.list;
    std::size_t matrix_bytes = std::size_t{list.item_count} * 16u + 32u;
    for (std::uint32_t b = 0; b < list.block_count; ++b) {
        const auto& block = list.blocks[b];
        std::size_t matrices = block.joint_count;
        if (block.model != 0u) {
            const auto incoming = std::find_if(entry.registry_ops.models.begin(), entry.registry_ops.models.end(),
                [&](const auto& m) { return m.first == block.model; });
            matrices = incoming != entry.registry_ops.models.end() ? incoming->second.draw_matrices.size()
                : registries_.model(block.model).draw_matrices.size();
        }
        matrix_bytes += matrices * core::kRowsPerMatrix * 16u * 2u;
    }
    matrix_bytes += std::size_t{core::light_set_rows(0u)} * 16u;
    for (std::uint32_t s = 0; s < list.light_set_count; ++s) {
        matrix_bytes += std::size_t{core::light_set_rows(list.light_sets[s].count)} * 16u;
    }
    matrix_bytes += std::size_t{list.instance_row_count} * 16u;
    if (rt_backend_.started()) {
        // The scene's instances, and the ring draws' triangles as the tracer takes them.
        matrix_bytes += std::size_t{list.item_count} * sizeof(rt::Instance) + 64u;
        matrix_bytes += std::size_t{list.ring_index_count} * 12u + static_cast<std::size_t>(list.ring_vertex_bytes);
        // The drop shadows' pass: its drops, each item's slot, and the screen's tiles with their entries (a drop that
        // covers the screen fills every tile).
        const std::size_t tiles = (std::size_t{renderer_.frame_map().width} / 16u + 2u) * (std::size_t{renderer_.frame_map().height} / 16u + 2u);
        matrix_bytes += std::size_t{list.shadow_drop_count} * (48u + tiles * 4u) + tiles * 8u + std::size_t{list.item_count} * 4u + 2048u;
    }
    std::size_t constant_bytes = std::size_t{list.view_count} * (sizeof(core::ViewRows) + 255u);
    for (std::uint32_t c = 0; c < list.constants_count; ++c) {
        const auto& constants = list.constants[c];
        constant_bytes += (std::size_t{constants.vertex_rows} + constants.pixel_rows) * 16u + 512u;
    }
    renderer_.matrix_ring().require_frame_bytes(matrix_bytes);
    renderer_.vertex_ring().require_frame_bytes(list.ring_vertex_bytes);
    renderer_.index_ring().require_frame_bytes(std::size_t{list.ring_index_count} * sizeof(std::uint32_t));
    if (rt_backend_.started()) {
        constant_bytes += 1024u + std::size_t{list.item_count} * 256u;  // the tracer's passes' constants (a skinned draw's)
    }
    renderer_.constant_ring().require_frame_bytes(constant_bytes);
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
        settings.packs.insert(settings.packs.end(), texture_pack_mod_folders_.begin(), texture_pack_mod_folders_.end());
        settings.pack_categories_off.insert(settings.pack_categories_off.end(), texture_pack_mod_categories_off_.begin(),
                                            texture_pack_mod_categories_off_.end());
        settings.dumps = dumps;
        settings.budget_bytes = static_cast<std::uint64_t>(render.texture_pack_budget_mb) << 20u;
        if (!texture_pack_settings_applied_ || !(settings == texture_pack_settings_)) {
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
        g_drawn_game_frame = entry.data.list->game_frame;
        if (rt_backend_.started()) {
            for (const auto& [id, mesh] : entry.registry_ops.meshes) {
                rt_want(id, mesh);
            }
        }
        registries_.apply(entry.registry_ops, renderer_.submitted_fence_value());
        if (rt_backend_.started()) {
            // A retired mesh's vertices go once the GPU is past the work submitted so far: nothing is built from them now.
            for (const std::uint32_t id : registries_.take_retired_meshes()) {
                rt_geometry_.forget(std::uint64_t{id} * 2u);
                rt_geometry_.forget(std::uint64_t{id} * 2u + 1u);
            }
        }
        freed_textures_.clear();
        if (entry.texture_ops != nullptr) {
            texture_store_.apply_ops(*entry.texture_ops, freed_textures_);
            entry.texture_ops.reset();
        } else {
            texture_store_.release_held(freed_textures_);
        }
        retired_tables_.clear();
        registries_.forget_textures(freed_textures_, retired_tables_);
#if defined(NEBULA_GPU_VULKAN) || defined(NEBULA_GPU_METAL)
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
            }
        }
        // Tables no walk has named for a long while give up what was built for them (core::Registries::retire_idle_tables).
        if (frame_index_ >= table_sweep_at_) {
            // The idle time and the sweep interval are counted in frames at 60 a second; each game frame shown in 1 + interp_frames_
            // of them makes the same time that many frames more.
            const std::uint64_t per_game_frame = 1u + interp_frames_;
            table_sweep_at_ = frame_index_ + core::Registries::kTableSweepFrames * per_game_frame;
            retired_tables_.clear();
            registries_.retire_idle_tables(frame_index_, retired_tables_, core::Registries::kTableIdleFrames * per_game_frame);
#if defined(NEBULA_GPU_VULKAN) || defined(NEBULA_GPU_METAL)
            if (!renderer_.bindless()) {
                for (const gpu::Table table : retired_tables_) {
                    renderer_.retire_texture_table(table);
                }
            }
#endif
        }
        // --diagnostics memory: every 600 frames, the texture tables and where the video memory is.
        if (frame_index_ >= table_log_at_ && ::galaxy::diagnosing(::galaxy::kDiagnoseMemory)) {
            table_log_at_ = frame_index_ + 600u;
            const core::Registries::TableCounts tables = registries_.table_counts();
            std::fprintf(stderr, "[render] texture tables: %u live, %u retired (%llu released since the start)\n", tables.live,
                         tables.retired, static_cast<unsigned long long>(tables.retirements));
#if !defined(NEBULA_GPU_VULKAN) && !defined(NEBULA_GPU_METAL)
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
    }
    registries_.reclaim(renderer_.completed_fence_value());
    if (rt_backend_.started()) {
        rt_frame(list);
    }
    // Finished replacements upload here and take the place of their originals.
    texture_store_.apply_texture_packs();
    // The movie frame this walk draws, as the movie quad's planes.
    if (render_movie_frame_.frame != MovieFrameMark::kNone || texture_store_.movie_override_active()) {
        if (++movie_stats_tick_ % 240u == 0u) {
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
        const bool wide = render.enhanced_lighting && render.wide_scene;
        // Native interpolation replays geometry and does not write temporal reconstruction targets.
        const bool motion = renderer_.temporal_mode();
        const bool one_pass = motion && renderer_.motion_pass_only();
        const bool layer = motion && renderer_.has_layer_targets();
        const unsigned samples = renderer_.scene_samples_log2();
        const std::uint32_t settings =
            samples | (wide ? 4u : 0u) | (motion ? 8u : 0u) | (pixel_lit_ ? 16u : 0u) | (one_pass ? 32u : 0u) |
            (rt_backend_.started() ? 64u : 0u);  // tracing turned on: every pipeline again, with its traced forms
        if (settings != pipelines_settings_) {
            pipelines_settings_ = settings;
            pipelines_queued_.store(0u, std::memory_order_relaxed);
        }
        const std::uint32_t count = registries_.pipeline_count();
        if (pipelines_queued_.load(std::memory_order_relaxed) != count) {
            std::vector<unsigned> variants{samples, 8u};
            if (samples != 0u) variants.push_back(0u);
            if (wide) variants.push_back(samples | kSceneVariantHdr);
            std::vector<std::uint32_t> order;
            for (std::uint32_t id = pipelines_queued_.load(std::memory_order_relaxed); id < count; ++id) {
                order.push_back(id);
            }
            for (const std::uint32_t id : order) {
                const core::PipelineEntry& pipeline = registries_.pipeline(id);
                if (pipeline.lit_form && !pixel_lit_) {
                    continue;
                }
                pipelines_.prepare(id, pipeline, variants, motion, layer, one_pass, rt_backend_.started());
            }
            pipelines_queued_.store(count, std::memory_order_relaxed);
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
    walk_list(entry.data, entry.before != nullptr ? &entry.before->data : nullptr, time);
    const bool real = time.t >= 1.0f;
    const DisplayTexture* selected = present ? display_pictures_.latest() : nullptr;
    const std::string snapshot_path = selected != nullptr ? present_snapshot_path(real ? NEBULA_EXT_PICTURE_GAME : NEBULA_EXT_PICTURE_GENERATED) : std::string{};
    if (!snapshot_path.empty() && real && rt_holds()) {
        rt_hold(entry, time, snapshot_path);
        selected = display_pictures_.latest();
    }
    if (selected != nullptr) {
        if (!snapshot_path.empty()) {
            renderer_.set_debug_capture_paths({}, snapshot_path);
        }
        renderer_.present(selected, !snapshot_path.empty());
        if (!snapshot_path.empty()) {
            renderer_.set_debug_capture_paths({}, {});
        }
    }
    (void)renderer_.end_frame(
        selected != nullptr,
        [](void*, std::uint64_t fence_value) {
            galaxy::timeline::record(galaxy::timeline::Event::RenderSubmitted, 0u, fence_value);
        },
        nullptr, time.generated);
    if (selected != nullptr && real) {
        note_picture_shown(*selected);
        last_presented_slot_.store(selected->slot, std::memory_order_relaxed);
    }
    // Nonblocking: a later frame writes a finished present snapshot.
    renderer_.debug_log_backbuffer_readback();
    ++frame_index_;
    if (real) {
        release_lists_before(entry);
    }
}

// A list that waited for a present that never came.
void RenderCore::flush_waiting_list() {
    if (!interp_list_waiting_) {
        return;
    }
    interp_list_waiting_ = false;
    supersede_interp_sequence();
    walk_frame(*interp_waiting_entry_, core::WalkTime{}, false);
    interp_shown_list_ = nullptr;
}

void RenderCore::render_frame_on_thread(FrameChunk& chunk) {
    // An extension's lose_gpu_device: Direct3D 12 removes its device; Vulkan has no call for that, so its next submission
    // reports VK_ERROR_DEVICE_LOST. Both go on through the loss recovery below.
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
        if (frames != interp_frames_) {
            interp_frames_ = frames;
            restart_interp_governors();
        }
        if (!interp_on_) {
            interp_shown_list_ = nullptr;
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
    if (interp_on_ && shows && !interp_stopping_ && !native_settings_overlay_visible()) {
        // It walks on the display's clock, after the frames between it and the list before.
        flush_waiting_list();
        interp_list_waiting_ = true;
        interp_waiting_entry_ = &entry;
        if (chunk.present_swap_chain) {
            if (chunk.black) {
                flush_waiting_list();
                present_black_picture(false);
            } else {
                interp_start_pending_ = true;
            }
        }
        return;
    }
    flush_waiting_list();
    supersede_interp_sequence();

    // Presents with this frame only when asked or when a frame's end presents (not while the output is black, the settings
    // overlay draws over every present, or a temporal upscaler or frame generation keeps its own time).
    render_pointer_response_ = entry.pointer_response;
    render_movie_content_ = entry.prerecorded_movie;
    render_movie_frame_ = entry.movie_frame;
    begin_walk_frame(entry);
    walk_list(entry.data, entry.before != nullptr ? &entry.before->data : nullptr, core::WalkTime{});

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
        if (!snapshot_path.empty() && !duplicate_display_present && rt_holds()) {
            rt_hold(entry, core::WalkTime{}, snapshot_path);
            selected_display = display_pictures_.latest();
        }
        if (!snapshot_path.empty()) {
            renderer_.set_debug_capture_paths({}, snapshot_path);
        }
        renderer_.present(selected_display, !snapshot_path.empty());
        if (!snapshot_path.empty()) {
            renderer_.set_debug_capture_paths({}, {});
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
    if (presents && present_swap_chain && selected_display != nullptr) {
        note_picture_shown(*selected_display);
        last_presented_slot_.store(selected_display->slot, std::memory_order_relaxed);
    }
    // Nonblocking: a later frame writes a finished present snapshot.
    renderer_.debug_log_backbuffer_readback();
    ++frame_index_;
    release_lists_before(entry);
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
