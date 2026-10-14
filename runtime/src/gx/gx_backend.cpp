// gx_backend.cpp: GxBackend, the top-level orchestrator for the live GX pipeline.
//
// gx_shim.cpp implements the frozen galaxy::gx public API in gx_d3d12.h as a thin wrapper
// around this backend.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#pragma warning(push, 0)
#include <Windows.h>
#include <d3d12.h>
#pragma warning(pop)

#include "galaxy/gx/gx_backend.h"
#include "galaxy/gx/dependency_draw_memo.h"
#include "galaxy/gx/dependency_range_memo.h"
#include "galaxy/gx/dependency_page_mask.h"
#include "galaxy/gx/fifo_cache_fingerprint.h"
#include "galaxy/gx/dependency_event_capture.h"
#include "galaxy/gx/owned_fifo_event_replay.h"

#include "galaxy/frame_cadence_diagnostics.h"
#include "galaxy/gx/line_point_raster.h"
#include "galaxy/gx/rt_surfaces.h"
#include "galaxy/gx/texture_sampling.h"
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
#include <intrin.h>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <cmath>
#include <limits>
#include <mutex>
#include <optional>
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
    std::uint8_t mip_levels) noexcept {
    std::uint32_t min_filter =
        static_cast<std::uint32_t>(mode.min_filter) & 0x7u;
    std::uint32_t max_lod =
        static_cast<std::uint32_t>(mode.max_lod_x16) & 0xFFu;
    if (generated_mips && mip_levels > 1u &&
        min_filter == static_cast<std::uint32_t>(TexMinFilter::Linear) &&
        mode.mag_filter == TexMagFilter::Linear) {
        min_filter =
            static_cast<std::uint32_t>(TexMinFilter::LinearMipLinear);
        max_lod = std::min<std::uint32_t>(
            0xFFu,
            static_cast<std::uint32_t>(mip_levels - 1u) * 16u);
    }
    return
          (static_cast<std::uint32_t>(mode.wrap_s) & 0x3u)
        | ((static_cast<std::uint32_t>(mode.wrap_t) & 0x3u) << 2)
        | ((static_cast<std::uint32_t>(mode.mag_filter) & 0x1u) << 4)
        | (min_filter << 5)
        | ((static_cast<std::uint32_t>(
               static_cast<std::uint8_t>(mode.lod_bias_x32)) & 0xFFu) << 8)
        | ((static_cast<std::uint32_t>(mode.min_lod_x16) & 0xFFu) << 16)
        | (max_lod << 24);
}

// Helpers: build GxVsConstants from current GxState and the render ring.

namespace {

static_assert(kEfbWidth == 640u && kEfbHeight == 528u);

// The alpha test keeps every fragment (both compares against the references, combined).
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

std::vector<std::uint64_t> read_capture_frame_list() {
    std::vector<std::uint64_t> frames;
    char list[1024]{};
    std::size_t list_length = 0;
    if (getenv_s(
            &list_length, list, sizeof(list),
            "GALAXY_GX_CAPTURE_FRAMES") == 0 &&
        list_length > 1u) {
        char* cursor = list;
        while (*cursor != '\0') {
            char* end = nullptr;
            const auto frame = std::strtoull(cursor, &end, 10);
            if (end == cursor) {
                ++cursor;
                continue;
            }
            if (frame != 0u) {
                frames.push_back(static_cast<std::uint64_t>(frame));
            }
            cursor = end;
        }
    }

    if (frames.empty()) {
        char value[32]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, value, sizeof(value),
                "GALAXY_GX_CAPTURE_FRAME") == 0 &&
            length > 1u) {
            const auto frame = std::strtoull(value, nullptr, 10);
            if (frame != 0u) {
                frames.push_back(static_cast<std::uint64_t>(frame));
            }
        }
    }

    std::sort(frames.begin(), frames.end());
    frames.erase(std::unique(frames.begin(), frames.end()), frames.end());
    return frames;
}

std::vector<std::uint64_t> read_u64_list_env(const char* env_name) {
    std::vector<std::uint64_t> values;
    char list[1024]{};
    std::size_t list_length = 0;
    if (getenv_s(&list_length, list, sizeof(list), env_name) != 0 ||
        list_length <= 1u) {
        return values;
    }

    char* cursor = list;
    while (*cursor != '\0') {
        char* end = nullptr;
        const auto value = std::strtoull(cursor, &end, 10);
        if (end == cursor) {
            ++cursor;
            continue;
        }
        if (value != 0u) {
            values.push_back(static_cast<std::uint64_t>(value));
        }
        cursor = end;
    }

    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
    return values;
}

bool diagnostic_texture_invalidate_requested(std::uint64_t frame) {
    static const std::vector<std::uint64_t> frames =
        read_u64_list_env("GALAXY_GX_INVALIDATE_FRAMES");
    return std::binary_search(frames.begin(), frames.end(), frame);
}

const std::string& capture_request_file() {
    static const std::string path = [] {
        char value[MAX_PATH]{};
        std::size_t length = 0u;
        return getenv_s(&length, value, sizeof(value),
                        "GALAXY_GX_CAPTURE_REQUEST_FILE") == 0 && length > 1u
            ? std::string(value) : std::string{};
    }();
    return path;
}

// Render-worker capture of substantial FIFO chunks. Disabled runs never touch a file. Each
// decimal request selects one substantial FIFO chunk, not a whole game frame. Empty VI and
// presentation submissions, and the 32-byte control chunks on the Gateway route, do not
// consume a request. Files are polled at most 10 times per second; a pending request expires
// after 10 seconds.
std::uint64_t live_capture_frame = 0u;

void poll_live_capture_request(std::uint64_t frame, std::size_t fifo_size) {
    constexpr std::size_t minimum_capture_fifo_bytes = 4096u;
    const auto& path = capture_request_file();
    if (path.empty()) return;
    static std::uint64_t examined_frame = 0u;
    static std::uint64_t last_request = 0u;
    static std::uint64_t pending_request = 0u;
    static ULONGLONG pending_since = 0u;
    static ULONGLONG last_poll = 0u;
    if (examined_frame == frame) return;
    examined_frame = frame;
    const auto now = GetTickCount64();
    if (pending_request != 0u) {
        if (now - pending_since > 10000u) {
            std::cerr << "[capture-request] request=" << pending_request
                      << " expired=1 scope=intrusive-substantial-chunk-not-performance\n";
            pending_request = 0u;
        } else if (fifo_size >= minimum_capture_fifo_bytes) {
            live_capture_frame = frame;
            std::cerr << "[capture-request] request=" << pending_request
                      << " frame=" << frame << " fifo-bytes=" << fifo_size
                      << " scope=intrusive-substantial-chunk-not-performance\n";
            pending_request = 0u;
            return;
        }
    }
    if (last_poll != 0u && now - last_poll < 100u) return;
    last_poll = now;
    const HANDLE file = CreateFileA(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    std::array<char, 32> text{};
    DWORD count = 0u;
    const BOOL read = ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &count, nullptr);
    CloseHandle(file);
    if (!read || count == 0u || count == text.size()) return;
    std::string_view token(text.data(), count);
    if (token.back() == '\n') {
        token.remove_suffix(1u);
        if (!token.empty() && token.back() == '\r') token.remove_suffix(1u);
    }
    if (token.empty() || token.front() < '0' || token.front() > '9') return;
    std::uint64_t request = 0u;
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), request);
    if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size() ||
        request <= last_request) return;
    last_request = request;
    pending_request = request;
    pending_since = now;
    if (fifo_size >= minimum_capture_fifo_bytes) {
        live_capture_frame = frame;
        pending_request = 0u;
        std::cerr << "[capture-request] request=" << request << " frame=" << frame
                  << " fifo-bytes=" << fifo_size
                  << " scope=intrusive-substantial-chunk-not-performance\n";
    }
}

bool capture_frame_requested(std::uint64_t frame) {
    static const std::vector<std::uint64_t> frames =
        read_capture_frame_list();
    // Opt-in extension of the same bounded capture: small effect/copy chunks
    // following the substantial scene chunk otherwise escape its observation.
    // At most eight adjacent renderer chunks, never continuous tracing.
    static const unsigned live_capture_chunks = [] {
        char value[16]{};
        std::size_t length = 0u;
        if (getenv_s(&length, value, sizeof(value),
                "GALAXY_GX_CAPTURE_REQUEST_CHUNKS") != 0 || length <= 1u)
            return 1u;
        unsigned chunks = 0u;
        const auto parsed = std::from_chars(value, value + length - 1u, chunks);
        return parsed.ec == std::errc{} && parsed.ptr == value + length - 1u &&
            chunks >= 1u && chunks <= 8u ? chunks : 1u;
    }();
    return (live_capture_frame != 0u && frame >= live_capture_frame &&
            frame - live_capture_frame < live_capture_chunks) ||
           std::binary_search(frames.begin(), frames.end(), frame);
}

// The adjacent pass-record range shares a single readback slot. Capture
// pixels only at its requested boundary; later chunks still record copy,
// resource and composite identities without reusing a pending GPU buffer.
bool capture_owned_xfb_enabled() {
    static const bool enabled = [] {
        char value[8]{}; std::size_t length = 0u;
        return getenv_s(&length, value, sizeof(value),
            "GALAXY_GX_CAPTURE_OWNED_XFB") == 0 && length > 1u && value[0] == '1';
    }();
    return enabled;
}

// One source/destination pair from the same real XFB copy, within the existing eight-chunk
// request bound. The substantial-chunk snapshot can belong to a newer EFB than the selected
// display XFB; never call those a matched pair.
bool capture_owned_xfb_requested(std::uint64_t frame) {
    static std::uint64_t captured_request_frame = 0u; // Render-worker owned.
    if (!capture_owned_xfb_enabled() || live_capture_frame == 0u ||
        live_capture_frame == captured_request_frame || frame < live_capture_frame ||
        frame - live_capture_frame >= 8u) return false;
    captured_request_frame = live_capture_frame;
    return true;
}

bool capture_pixel_frame_requested(std::uint64_t frame) {
    static const std::vector<std::uint64_t> frames = read_capture_frame_list();
    return (!capture_owned_xfb_enabled() && live_capture_frame != 0u &&
            frame == live_capture_frame) ||
           std::binary_search(frames.begin(), frames.end(), frame);
}

// Test aid for runs without a display: every Nth swap-chain present is read
// back and written to GALAXY_PRESENT_SNAPSHOT_DIR\present_<n>.ppm, limited to
// presents GALAXY_PRESENT_SNAPSHOT_FIRST..GALAXY_PRESENT_SNAPSHOT_LAST.
// GALAXY_PRESENT_SNAPSHOT_PAIR=1 also writes the present after each.
std::string present_snapshot_path() {
    const auto read_u64 = [](const char* name, std::uint64_t fallback) {
        char value[32]{};
        std::size_t length = 0;
        return getenv_s(&length, value, sizeof(value), name) == 0 && length > 1u
            ? std::strtoull(value, nullptr, 10) : fallback;
    };
    static const std::uint64_t every = read_u64("GALAXY_PRESENT_SNAPSHOT_EVERY", 0u);
    static const bool pair = read_u64("GALAXY_PRESENT_SNAPSHOT_PAIR", 0u) != 0u;
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
    static std::uint64_t presents = 0u;
    ++presents;
    if (every == 0u || directory.empty() || presents < first || presents > last ||
        (presents % every != 0u && !(pair && presents % every == 1u % every))) {
        return {};
    }
    return directory + "\\present_" + std::to_string(presents) + ".ppm";
}

bool capture_present_requested(std::uint64_t request) {
    static const std::vector<std::uint64_t> requests =
        read_u64_list_env("GALAXY_GX_CAPTURE_PRESENT_REQUESTS");
    return std::binary_search(requests.begin(), requests.end(), request);
}

bool capture_frame_list_enabled() {
    if (!capture_request_file().empty()) return true;
    static const bool enabled = [] {
        char value[1024]{};
        std::size_t length = 0;
        return getenv_s(
                   &length, value, sizeof(value),
                   "GALAXY_GX_CAPTURE_FRAMES") == 0 &&
               length > 1u;
    }();
    return enabled;
}

bool capture_draw_log_enabled() {
    static const bool enabled = [] {
        char value[8]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, value, sizeof(value),
                "GALAXY_GX_CAPTURE_DRAW_LOG") != 0 ||
            length <= 1u) {
            return false;
        }
        return std::strtoull(value, nullptr, 10) != 0u;
    }();
    return enabled;
}

bool capture_composites_only_enabled() {
    static const bool enabled = [] {
        char value[8]{};
        std::size_t length = 0;
        return getenv_s(&length, value, sizeof(value),
            "GALAXY_GX_CAPTURE_COMPOSITES_ONLY") == 0 &&
            length > 1u && value[0] == '1';
    }();
    return enabled;
}

std::string numbered_capture_path(
    const char* env_name,
    std::uint64_t frame) {
    char value[MAX_PATH]{};
    std::size_t length = 0;
    if (getenv_s(&length, value, sizeof(value), env_name) != 0 ||
        length <= 1u) {
        return {};
    }

    std::filesystem::path path(value);
    const std::string suffix = "_" + std::to_string(frame);
    const std::string stem = path.stem().string();
    const std::string ext = path.extension().string();
    path.replace_filename(stem + suffix + ext);
    return path.string();
}

std::string capture_output_path(const char* env_name, std::uint64_t frame) {
    if (capture_frame_list_enabled()) {
        return numbered_capture_path(env_name, frame);
    }

    char value[MAX_PATH]{};
    std::size_t length = 0;
    if (getenv_s(&length, value, sizeof(value), env_name) != 0 ||
        length <= 1u) {
        return {};
    }
    return value;
}

class PeEventSink final : public FifoSink {
public:
    PeEventSink(GxState& state, const NativeServicesV1* services)
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

    // Initial parsing validates every draw's extent. Cached replay verifies
    // unchanged list bytes and VCD/VAT dependencies before offering these
    // draw-only runs. This sink has no draw side effects; PE state commands
    // remain separate and replay in their original order.
    [[nodiscard]] bool on_cached_prepared_draw_run(
        PrimitiveClass,
        std::uint8_t,
        std::span<const std::byte>,
        std::size_t,
        std::size_t,
        std::uint8_t,
        std::size_t) override {
        return true;
    }

    [[nodiscard]] bool on_cached_packet_draw_run(
        std::uint64_t,
        std::size_t,
        PrimitiveClass,
        std::uint8_t,
        std::span<const std::byte>,
        std::size_t,
        std::span<const CachedDrawPacket>,
        std::uint32_t,
        std::uint32_t,
        std::span<const std::uint16_t>) override {
        return true;
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
    const NativeServicesV1* services_;
};

bool trace_efb_copy_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length, value, sizeof(value),
                   "GALAXY_TRACE_EFB_COPY") == 0 &&
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

// GALAXY_TRACE_LIGHTS=1: the actor-light aim every 600 scenes, the first moved draws and the CPU
// cycles ([rt-actor-light]); =2: the aim of every scene.
std::uint64_t actor_light_trace_level() {
    static const std::uint64_t level = read_env_u64("GALAXY_TRACE_LIGHTS", 0u);
    return level;
}

bool trace_actor_lights_enabled() {
    return actor_light_trace_level() != 0u;
}

bool trace_xfb_causal_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_XFB_CAUSAL") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

bool trace_xfb_causal_frame(std::uint64_t frame) {
    if (!trace_xfb_causal_enabled()) {
        return false;
    }
    static const std::uint64_t start_frame =
        read_env_u64("GALAXY_TRACE_XFB_CAUSAL_START_FRAME", 1u);
    // The trace is bounded even when the caller sets no limit. It is a causal diagnostic, not a
    // shipping telemetry stream.
    static const std::uint64_t max_frames = std::min<std::uint64_t>(
        read_env_u64("GALAXY_TRACE_XFB_CAUSAL_MAX_FRAMES", 600u),
        10'000u);
    return frame >= start_frame && frame - start_frame < max_frames;
}

// Unlike the streaming causal trace above, this mode collects a small, fixed record set on the
// render thread and formats it only after that thread has stopped. It remains usable while AI
// DMA is active: stderr writes from a render callback are not real-time safe.
bool trace_xfb_causal_deferred_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_XFB_CAUSAL_DEFERRED") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

bool trace_xfb_causal_deferred_frame(std::uint64_t frame) {
    if (!trace_xfb_causal_deferred_enabled()) {
        return false;
    }
    static const std::uint64_t start_frame =
        read_env_u64("GALAXY_TRACE_XFB_CAUSAL_DEFERRED_START_FRAME", 1u);
    static const std::uint64_t max_frames = std::min<std::uint64_t>(
        read_env_u64("GALAXY_TRACE_XFB_CAUSAL_DEFERRED_MAX_FRAMES", 64u),
        128u);
    return frame >= start_frame && frame - start_frame < max_frames;
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

bool trace_gx_microprofile_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_GX_MICROPROFILE") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

bool trace_gx_stats_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_TRACE_GX_STATS") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

bool shader_cache_periodic_flush_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length,
                   value,
                   sizeof(value),
                   "GALAXY_SHADER_CACHE_PERIODIC_FLUSH") == 0 &&
               length > 1 && value[0] != '0' && value[0] != 'n' &&
               value[0] != 'N';
    }();
    return enabled;
}

bool trace_xfb_present_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length, value, sizeof(value),
                   "GALAXY_TRACE_XFB_PRESENT") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

std::uint64_t trace_xfb_present_interval() {
    static const std::uint64_t interval =
        std::max<std::uint64_t>(
            1u,
            read_env_u64("GALAXY_TRACE_XFB_PRESENT_INTERVAL", 120u));
    return interval;
}

std::uint64_t trace_gx_microprofile_interval() {
    static const std::uint64_t interval =
        std::max<std::uint64_t>(
            1u,
            read_env_u64("GALAXY_TRACE_GX_MICROPROFILE_INTERVAL", 1u));
    return interval;
}

std::uint64_t max_xfb_preserve_age() {
    // select_xfb_for_present holds the last good frame when the VI-selected XFB is transiently not
    // presentable, which happens while that buffer is re-acquired as the next copy target during a
    // double or triple buffer flip. The fall-forward to latest() on "missing_expired" applies only
    // to a sustained miss, a buffer that never receives a copy. It must not fire on the 1-3 frame
    // jitter of a normal flip or of the async render pipeline, or every flip would show a different
    // buffer for one frame. 30 presents (about 0.5 s) clears pipeline jitter and still breaks a true
    // multi-frame freeze.
    static const std::uint64_t age =
        std::clamp<std::uint64_t>(
            read_env_u64("GALAXY_XFB_MAX_PRESERVE_AGE", 30u),
            0u,
            600u);
    return age;
}

// Per-draw instrumentation sampling. The per-draw clocks sit on the render thread's critical
// path, so they cannot be always on. Gating them behind an intrusive flag would leave a routine
// recording unable to attribute time. GALAXY_GX_FRAME_TIMING_SAMPLE=N enables per-draw
// instrumentation for one frame in every N, so one recording holds attributed sample frames and
// frames at production speed. N <= 1 disables sampling (the default).
bool frame_gx_timing_sample_enabled(std::uint64_t frame_index) {
    static const std::uint64_t period = read_env_u64(
        "GALAXY_GX_FRAME_TIMING_SAMPLE", 0u);
    if (period < 2u) {
        return false;
    }
    // Caller passes the same pre-increment index trace_gx_microprofile uses.
    return (frame_index + 1u) % period == 0u;
}

// There is no accessor for the sampling period. `frame_time_sample_` is true once per period,
// not on every frame, so a store stride derived from the period would suppress the samples.
// `frame_time_detail_` is the flag that is true every frame. The ring bounds its occupancy by
// reservoir sampling.

// GALAXY_PE_PRECLASSIFY=0 skips the simulation thread's second FIFO parse;
// every capture then takes the render thread's resolution path.
bool pe_preclassify_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(&length, value, sizeof(value), "GALAXY_PE_PRECLASSIFY") != 0 ||
            length <= 1u) {
            return true;
        }
        return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
}

bool display_list_dirty_invalidate_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length,
                value,
                sizeof(value),
                "GALAXY_DISPLAY_LIST_DIRTY_INVALIDATE") != 0 ||
            length <= 1u) {
            return false;
        }
        return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
}

// The decoded-packet-run vertex cache reuses CPU-decoded vertices across identical display-list
// replays. A busy SMG frame replays tens of thousands of draws from cached display lists. With
// the cache off, every packet run re-decodes its source vertices and copies them into the frame
// ring. That re-decode is on the render thread's critical path, so the cache is on by default.
// Set GALAXY_GX_DECODED_VERTEX_CACHE=0 to restore the uncached path when bisecting geometry
// regressions.
//
// A hit is taken only when the whole decode input is identical:
//   - the key holds the display-list token and packet-run index (so the source bytes and packet
//     boundaries match), the VCD/VAT words and the indexed array base and stride registers;
//   - guest-write notifications invalidate every overlapping entry before the frame's first
//     replay. The retained snapshot bytes are compared against live guest memory once per
//     generation, as a backstop against a coalesced or missed notification.
// A miss, a stale reject, or an entry that could not capture a complete dependency snapshot falls
// through to the uncached decode path below.
bool decoded_vertex_cache_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length,
                value,
                sizeof(value),
                "GALAXY_GX_DECODED_VERTEX_CACHE") != 0 ||
            length <= 1u) {
            return true;
        }
        return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
}

// A cache entry whose guest input did not change describes the same vertices on consecutive
// frames. When enabled, the entry's ImmutableUploadToken lets UploadRing::upload_immutable() keep
// the bytes it already copied into the per-frame segment, so each replayed packet run skips one
// full vertex-buffer copy per frame.
//
// Reuse is taken only when the ring can prove the bytes are intact. Allocations are monotonic
// within a frame segment. The token records the segment generation of its own upload, and reuse
// requires that generation to be exactly one behind the current one. Any intervening allocation
// in that slot changes the generation, and the call falls back to a plain copy. The failure mode
// is therefore an extra copy, never a reused overwritten buffer.
bool immutable_vertex_upload_reuse_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(&length, value, sizeof(value),
                     "GALAXY_GX_IMMUTABLE_VERTEX_UPLOAD_REUSE") != 0 ||
            length <= 1u) {
            return true;
        }
        return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
}

// Validated decoded packet-run entries draw from one persistent buffer instead
// of being copied into the frame vertex ring on every replay. Default on;
// GALAXY_GX_RESIDENT_DECODED_VERTICES=0 restores the ring copy for A/B runs.
bool resident_decoded_vertices_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(&length, value, sizeof(value),
                     "GALAXY_GX_RESIDENT_DECODED_VERTICES") != 0 ||
            length <= 1u) {
            return true;
        }
        return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
}

// First-use texture decodes run on TextureDecodePool workers when the chunk
// carries a sealed memory snapshot. GALAXY_GX_ASYNC_TEXTURE_DECODE=0 keeps
// every decode on the render thread.
bool async_texture_decode_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(&length, value, sizeof(value),
                     "GALAXY_GX_ASYNC_TEXTURE_DECODE") != 0 ||
            length <= 1u) {
            return true;
        }
        return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
}

bool sorted_vertex_invalidation_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0u;
        return getenv_s(&length, value, sizeof(value),
                   "GALAXY_GX_SORTED_VERTEX_INVALIDATION") == 0 && length > 1u &&
               value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
}

bool vertex_dependency_page_filter_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0u;
        return getenv_s(&length, value, sizeof(value),
                   "GALAXY_GX_VERTEX_DEPENDENCY_PAGE_FILTER") == 0 && length > 1u &&
               value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
}

bool lazy_vertex_invalidation_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0u;
        return getenv_s(&length, value, sizeof(value),
                   "GALAXY_GX_LAZY_VERTEX_INVALIDATION") == 0 && length > 1u &&
               value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
}

bool async_render_thread_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        if (getenv_s(
                &length,
                value,
                sizeof(value),
                "GALAXY_ASYNC_RENDER_THREAD") != 0 ||
            length <= 1u) {
            return true;
        }
        return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
}

// Experimental two-stage ownership handoff: the render worker seals a strict
// dependency snapshot before recording GPU commands, then releases the CPU
// owner. The ordinary live-read barrier remains the default.
bool worker_memory_snapshot_enabled() {
    static const bool enabled = [] {
        char value[8]{};
        std::size_t length = 0;
        return getenv_s(
                   &length, value, sizeof(value),
                   "GALAXY_RENDER_WORKER_MEMORY_SNAPSHOT") == 0 &&
               length > 1u && value[0] == '1';
    }();
    return enabled;
}

bool ordered_dependency_capture_enabled() {
    static const bool enabled = [] {
        char value[8]{};
        std::size_t length = 0;
        return getenv_s(
                   &length, value, sizeof(value),
                   "GALAXY_GX_ORDERED_DEPENDENCY_CAPTURE") == 0 &&
               length > 1u && value[0] != '0';
    }();
    return enabled;
}

bool ordered_packet_replay_enabled() {
    static const bool enabled = [] {
        char value[8]{};
        std::size_t length = 0;
        return getenv_s(
                   &length, value, sizeof(value),
                   "GALAXY_GX_ORDERED_PACKET_REPLAY") == 0 &&
               length > 1u && value[0] != '0';
    }();
    return enabled;
}

std::vector<GuestMemoryRange> render_memory_snapshot_ranges() {
    std::vector<GuestMemoryRange> ranges;
    char value[1024]{};
    std::size_t length = 0;
    if (getenv_s(
            &length,
            value,
            sizeof(value),
            "GALAXY_RENDER_MEMORY_SNAPSHOT_RANGES") != 0 ||
        length <= 1u) {
        return ranges;
    }

    const std::string text(value);
    std::size_t offset = 0;
    while (offset <= text.size()) {
        const std::size_t sep = text.find(';', offset);
        const std::string entry = text.substr(
            offset,
            (sep == std::string::npos ? text.size() : sep) - offset);
        const std::size_t colon = entry.find(':');
        if (colon != std::string::npos) {
            const std::uint64_t base = std::strtoull(
                entry.substr(0, colon).c_str(), nullptr, 0);
            const std::uint64_t size = std::strtoull(
                entry.substr(colon + 1u).c_str(), nullptr, 0);
            if (base <= std::numeric_limits<std::uint32_t>::max() &&
                size != 0u &&
                size <= std::numeric_limits<std::uint32_t>::max() &&
                base + size <=
                    static_cast<std::uint64_t>(
                        std::numeric_limits<std::uint32_t>::max()) +
                        1ull) {
                ranges.push_back(GuestMemoryRange{
                    static_cast<std::uint32_t>(base),
                    static_cast<std::uint32_t>(size),
                });
            }
        }
        if (sep == std::string::npos) {
            break;
        }
        offset = sep + 1u;
    }
    return ranges;
}

bool compact_memory_snapshot_enabled() {
    static const bool enabled = [] {
        char value[8]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, value, sizeof(value),
                "GALAXY_RENDER_MEMORY_SNAPSHOT_COMPACT") != 0 ||
            length <= 1u) {
            return !get_gx_timing_config().render_live_memory_wait;
        }
        return value[0] != '0';
    }();
    return enabled;
}

bool immutable_range_ownership_enabled() {
    static const bool enabled = [] {
        char value[8]{};
        std::size_t length = 0;
        if (getenv_s(
                &length,
                value,
                sizeof(value),
                "GALAXY_RENDER_IMMUTABLE_RANGE_OWNERSHIP") != 0 ||
            length <= 1u) {
            return false;
        }
        return value[0] != '0';
    }();
    return enabled;
}

bool parallel_memory_snapshot_copy_enabled() {
    static const bool enabled = [] {
        char value[8]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, value, sizeof(value),
                "GALAXY_RENDER_MEMORY_SNAPSHOT_PARALLEL") != 0 ||
            length <= 1u) {
            return false;
        }
        return value[0] != '0';
    }();
    return enabled;
}

bool broad_dependency_index_ranges_enabled() {
    static const bool enabled = [] {
        char value[8]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, value, sizeof(value),
                "GALAXY_DEPENDENCY_SNAPSHOT_BROAD_INDEX_RANGES") != 0 ||
            length <= 1u) {
            return true;
        }
        return value[0] != '0';
    }();
    return enabled;
}

std::uint64_t exact_cached_dependency_vertex_budget() {
    static const std::uint64_t budget =
        std::clamp<std::uint64_t>(
            read_env_u64(
                "GALAXY_DEPENDENCY_EXACT_CACHED_VERTEX_BUDGET",
                4096u),
            0u,
            1u << 20u);
    return budget;
}

// A video frame the game did not redraw is not presented again, so the swap
// chain's present rate (what frame-rate overlays count) is the game's frame
// rate. GALAXY_SKIP_DUPLICATE_XFB_PRESENTS=0 presents every video frame.
bool skip_duplicate_xfb_presents_enabled() {
    static const bool enabled = [] {
        char value[8]{};
        std::size_t length = 0;
        if (getenv_s(
                &length,
                value,
                sizeof(value),
                "GALAXY_SKIP_DUPLICATE_XFB_PRESENTS") != 0 ||
            length <= 1u) {
            return true;
        }
        return value[0] != '0';
    }();
    return enabled;
}

std::size_t snapshot_copy_worker_count() {
    static const std::size_t count = [] {
        const unsigned logical_cores = std::max(1u, std::thread::hardware_concurrency());
        const std::uint64_t default_count =
            logical_cores >= 12u ? 4u : (logical_cores >= 6u ? 3u : 2u);
        return static_cast<std::size_t>(
            std::clamp<std::uint64_t>(
                read_env_u64(
                    "GALAXY_RENDER_MEMORY_SNAPSHOT_COPY_WORKERS",
                    default_count),
                1u,
                8u));
    }();
    return count;
}

std::uint64_t snapshot_parallel_copy_threshold() {
    static const std::uint64_t threshold =
        read_env_u64(
            "GALAXY_RENDER_MEMORY_SNAPSHOT_PARALLEL_THRESHOLD",
            8ull * 1024ull * 1024ull);
    return threshold;
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
    // Compact into the already owned vector. Output never passes the current
    // input, so aliasing the read/write storage is safe and retains capacity.
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
    // Generated host mips consume only guest mip zero, regardless of the
    // enhancement setting. Dependency discovery needs no settings lookup.

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

void record_texture_dependency_ranges(
    const GxState& state,
    std::vector<GuestMemoryRange>& ranges) {
    const std::uint8_t texture_map_mask =
        sampled_texture_map_mask(state);
    for (unsigned map = 0; map < kMaxTextureMaps; ++map) {
        if ((texture_map_mask & (1u << map)) == 0u) {
            continue;
        }
        const TexImage image = state.tex_image(map);
        const TexMode mode = state.tex_mode(map);
        record_guest_memory_range(
            ranges,
            image.guest_addr,
            snapshot_texture_guest_size(image, mode));
    }
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
        GuestMemoryV1* memory,
        std::vector<GuestMemoryRange>& ranges,
        std::unordered_map<
            GxBackend::DependencyDrawRunRangeCacheKey,
            GxBackend::DependencyDrawRunRangeCacheEntry,
            GxBackend::DependencyDrawRunRangeCacheKeyHash>&
            draw_run_cache,
        std::uint64_t& draw_run_cache_tick,
        std::size_t& draw_run_cache_bytes,
        std::atomic<std::uint64_t>& stat_draw_run_cache_hits,
        std::atomic<std::uint64_t>& stat_draw_run_cache_misses,
        std::atomic<std::uint64_t>& stat_draw_run_cache_evictions)
        : state_(state),
          memory_(memory),
          ranges_(ranges),
          draw_run_cache_(draw_run_cache),
          draw_run_cache_tick_(draw_run_cache_tick),
          draw_run_cache_bytes_(draw_run_cache_bytes),
          stat_draw_run_cache_hits_(stat_draw_run_cache_hits),
          stat_draw_run_cache_misses_(stat_draw_run_cache_misses),
          stat_draw_run_cache_evictions_(stat_draw_run_cache_evictions) {}

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
        if (cached_broad_draw_run_active_) {
            return;
        }
        if (record_broad_draw_dependencies_for_vtxfmt(vtxfmt)) {
            return;
        }
        std::array<SnapshotArrayRange, 12> array_ranges{};
        scan_draw_payload_dependencies(desc, raw, vertex_count, array_ranges);
        flush_array_ranges(array_ranges);
        record_texture_dependency_ranges_once();
    }

    [[nodiscard]] bool begin_cached_draw_run(
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        std::size_t draw_count) override {
        (void)primitive;
        (void)draw_count;
        if (!record_broad_draw_dependencies_for_vtxfmt(vtxfmt)) {
            return false;
        }
        cached_broad_draw_run_active_ = true;
        return true;
    }

    void end_cached_draw_run() override {
        cached_broad_draw_run_active_ = false;
    }

    [[nodiscard]] bool on_cached_simple_draw_run(
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        std::span<const std::byte> bytes,
        std::size_t base_offset,
        std::span<const CachedDrawPacket> packets) override {
        (void)primitive;
        (void)base_offset;
        const std::uint64_t total_vertices =
            cached_packet_vertex_count(packets);
        if (record_broad_cached_draw_dependencies_if_over_budget(
                vtxfmt,
                total_vertices)) {
            return true;
        }
        const VertexDescriptor desc = state_.vertex_desc(vtxfmt);
        const std::size_t stride = VertexLoader::source_vertex_size(desc);
        std::array<SnapshotArrayRange, 12> array_ranges{};
        for (const CachedDrawPacket& packet : packets) {
            const std::size_t expected_payload =
                stride * static_cast<std::size_t>(packet.vertex_count);
            if (packet.draw_payload_size != expected_payload ||
                packet.draw_cursor_offset + 2u + packet.draw_payload_size >
                    bytes.size()) {
                return record_broad_draw_dependencies_for_vtxfmt(vtxfmt);
            }
            const std::span<const std::byte> raw(
                bytes.data() + packet.draw_cursor_offset + 2u,
                packet.draw_payload_size);
            scan_draw_payload_dependencies(
                desc,
                raw,
                packet.vertex_count,
                array_ranges);
        }
        flush_array_ranges(array_ranges);
        record_texture_dependency_ranges_once();
        return true;
    }

    [[nodiscard]] bool on_cached_prepared_draw_run(
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        std::span<const std::byte> prepared_payload,
        std::size_t base_offset,
        std::size_t local_opcode_offset,
        std::uint8_t opcode,
        std::size_t source_draw_count) override {
        (void)primitive;
        (void)vtxfmt;
        (void)prepared_payload;
        (void)base_offset;
        (void)local_opcode_offset;
        (void)opcode;
        (void)source_draw_count;
        // Dependency scans prefer cached packet draw runs because they carry a
        // stable cache token and can reuse exact guest-memory ranges across
        // identical display-list replays. If a cached display list lacks a
        // packet run, FifoParser falls back to ordinary draw replay below.
        return false;
    }

    [[nodiscard]] bool on_cached_packet_draw_run(
        std::uint64_t cache_token,
        std::size_t packet_run_index,
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        std::span<const std::byte> bytes,
        std::size_t base_offset,
        std::span<const CachedDrawPacket> packets,
        std::uint32_t total_vertices,
        std::uint32_t total_indices,
        std::span<const std::uint16_t> precomputed_indices) override {
        (void)base_offset;
        (void)total_indices;
        (void)precomputed_indices;
        const std::uint64_t exact_budget = exact_cached_dependency_vertex_budget();
        // Zero retains the configured broad-first policy, including fallthrough.
        if (exact_budget == 0u &&
            record_broad_cached_draw_dependencies_if_over_budget(
                vtxfmt, total_vertices)) {
            return true;
        }
        const GxBackend::DependencyDrawRunRangeCacheKey cache_key{
            cache_token,
            packet_run_index,
            primitive,
            vtxfmt,
            dependency_shape_state_hash(state_),
            state_.dependency_shape_words(),
        };
        if (const auto cache_it = draw_run_cache_.find(cache_key);
            cache_it != draw_run_cache_.end()) {
            cache_it->second.last_used = ++draw_run_cache_tick_;
            for (const GuestMemoryRange& range : cache_it->second.ranges) {
                record_dependency_range(range.guest_base, range.size);
            }
            stat_draw_run_cache_hits_.fetch_add(
                1u,
                std::memory_order_relaxed);
            return true;
        }
        // Hits reuse addresses proved by immutable indices and exact state;
        // current attribute bytes are still copied/validated by their owners.
        // Only an actual scan consumes the nonzero exact-discovery budget.
        if (exact_budget != 0u &&
            record_broad_cached_draw_dependencies_if_over_budget(
                vtxfmt, total_vertices)) {
            return true;
        }
        stat_draw_run_cache_misses_.fetch_add(
            1u,
            std::memory_order_relaxed);

        const VertexDescriptor desc = state_.vertex_desc(vtxfmt);
        const std::size_t stride = VertexLoader::source_vertex_size(desc);
        std::array<SnapshotArrayRange, 12> array_ranges{};
        for (const CachedDrawPacket& packet : packets) {
            const std::size_t expected_payload =
                stride * static_cast<std::size_t>(packet.vertex_count);
            if (packet.draw_payload_size != expected_payload ||
                packet.draw_cursor_offset + 2u + packet.draw_payload_size >
                    bytes.size()) {
                return record_broad_draw_dependencies_for_vtxfmt(vtxfmt);
            }
            const std::span<const std::byte> raw(
                bytes.data() + packet.draw_cursor_offset + 2u,
                packet.draw_payload_size);
            scan_draw_payload_dependencies(
                desc,
                raw,
                packet.vertex_count,
                array_ranges);
        }
        std::vector<GuestMemoryRange> cached_ranges;
        flush_array_ranges_to(array_ranges, cached_ranges);
        record_texture_dependency_ranges_to(cached_ranges);
        normalize_guest_memory_ranges(cached_ranges);
        for (const GuestMemoryRange& range : cached_ranges) {
            record_dependency_range(range.guest_base, range.size);
        }
        store_draw_run_cache_entry(cache_key, std::move(cached_ranges));
        return true;
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
    [[nodiscard]] static std::uint64_t cached_packet_vertex_count(
        std::span<const CachedDrawPacket> packets) noexcept {
        std::uint64_t total = 0;
        for (const CachedDrawPacket& packet : packets) {
            total += packet.vertex_count;
        }
        return total;
    }

    [[nodiscard]] bool record_broad_cached_draw_dependencies_if_over_budget(
        std::uint8_t vtxfmt,
        std::uint64_t vertices) {
        const std::uint64_t budget = exact_cached_dependency_vertex_budget();
        if (budget == 0u ||
            exact_cached_dependency_vertices_ + vertices > budget) {
            return record_broad_draw_dependencies_for_vtxfmt(vtxfmt);
        }
        exact_cached_dependency_vertices_ += vertices;
        return false;
    }

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

    [[nodiscard]] static std::size_t draw_run_cache_entry_bytes(
        const std::vector<GuestMemoryRange>& ranges) noexcept {
        // Count retained capacity and both copies of the key. Node overhead is
        // an allowance, so an independent entry cap also bounds metadata.
        return ranges.capacity() * sizeof(GuestMemoryRange) +
            sizeof(GxBackend::DependencyDrawRunRangeCacheKey) +
            sizeof(GxBackend::DependencyDrawRunRangeCacheEntry) +
            3u * sizeof(void*);
    }

    void prune_draw_run_cache() {
        constexpr std::size_t kMaxDrawRunCacheBytes =
            8u * 1024u * 1024u;
        constexpr std::size_t kMaxDrawRunCacheEntries = 4096u;
        if ((draw_run_cache_bytes_ <= kMaxDrawRunCacheBytes &&
             draw_run_cache_.size() <= kMaxDrawRunCacheEntries) ||
            draw_run_cache_.empty()) {
            return;
        }

        struct PruneCandidate {
            std::uint64_t last_used = 0;
            const GxBackend::DependencyDrawRunRangeCacheKey* key = nullptr;
        };
        std::vector<PruneCandidate> candidates;
        candidates.reserve(draw_run_cache_.size());
        for (const auto& [key, entry] : draw_run_cache_) {
            candidates.push_back(PruneCandidate{entry.last_used, &key});
        }
        std::sort(
            candidates.begin(),
            candidates.end(),
            [](const PruneCandidate& lhs, const PruneCandidate& rhs) noexcept {
                return lhs.last_used < rhs.last_used;
            });

        for (const PruneCandidate& candidate : candidates) {
            // Evict a batch rather than sorting the whole cache again on each
            // next insertion at its limit. Node addresses survive other erases.
            if (draw_run_cache_bytes_ <= kMaxDrawRunCacheBytes * 3u / 4u &&
                draw_run_cache_.size() <= kMaxDrawRunCacheEntries * 3u / 4u) {
                break;
            }
            const auto it = draw_run_cache_.find(*candidate.key);
            if (it == draw_run_cache_.end()) {
                continue;
            }
            draw_run_cache_bytes_ -= std::min(
                draw_run_cache_bytes_,
                it->second.byte_size);
            draw_run_cache_.erase(it);
            stat_draw_run_cache_evictions_.fetch_add(
                1u,
                std::memory_order_relaxed);
        }
    }

    void store_draw_run_cache_entry(
        const GxBackend::DependencyDrawRunRangeCacheKey& key,
        std::vector<GuestMemoryRange>&& ranges) {
        const std::size_t byte_size = draw_run_cache_entry_bytes(ranges);
        GxBackend::DependencyDrawRunRangeCacheEntry entry;
        entry.key = key;
        entry.last_used = ++draw_run_cache_tick_;
        entry.byte_size = byte_size;
        entry.ranges = std::move(ranges);

        if (auto existing = draw_run_cache_.find(key);
            existing != draw_run_cache_.end()) {
            draw_run_cache_bytes_ -= std::min(
                draw_run_cache_bytes_,
                existing->second.byte_size);
            existing->second = std::move(entry);
            draw_run_cache_bytes_ += byte_size;
        } else {
            draw_run_cache_.emplace(key, std::move(entry));
            draw_run_cache_bytes_ += byte_size;
        }
        prune_draw_run_cache();
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
        if (!broad_dependency_index_ranges_enabled()) {
            return false;
        }
        // This sink owns one parse interval over the same register instance;
        // no state assignment/restore occurs while it is active. A revision
        // identifies mutations exactly here, whereas a hash alone cannot
        // justify omitting required snapshot reads. Zero forbids reuse after
        // counter saturation. Changes conservatively start a new format mask.
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
            const GuestMemoryRegionV1& region = memory_->regions[i];
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
    GuestMemoryV1* memory_ = nullptr;
    std::vector<GuestMemoryRange>& ranges_;
    std::unordered_map<
        GxBackend::DependencyDrawRunRangeCacheKey,
        GxBackend::DependencyDrawRunRangeCacheEntry,
        GxBackend::DependencyDrawRunRangeCacheKeyHash>& draw_run_cache_;
    std::uint64_t& draw_run_cache_tick_;
    std::size_t& draw_run_cache_bytes_;
    std::atomic<std::uint64_t>& stat_draw_run_cache_hits_;
    std::atomic<std::uint64_t>& stat_draw_run_cache_misses_;
    std::atomic<std::uint64_t>& stat_draw_run_cache_evictions_;
    detail::DependencyRangeMemo emitted_range_keys_;
    detail::BroadDrawDependencyMemo broad_draw_memo_;
    std::uint64_t exact_cached_dependency_vertices_ = 0;
    bool cached_broad_draw_run_active_ = false;
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

class RenderDependencyReadRecorder final
    : public FifoGuestMemoryReadRecorder {
public:
    explicit RenderDependencyReadRecorder(DependencyEventSink& sink)
        : sink_(sink) {}

    void record_guest_memory_read(std::uint32_t,
                                  std::uint32_t) override {
        throw std::logic_error(
            "GX ordered capture requires exact parser read bytes");
    }
    void record_guest_memory_read_bytes(
        std::uint32_t guest_addr,
        std::span<const std::byte> bytes) override {
        sink_.guest_read(DependencyReadSource::Parser,
                         guest_addr, bytes);
    }

private:
    DependencyEventSink& sink_;
};

class ScopedRenderDependencyCapture final {
public:
    ScopedRenderDependencyCapture(
        FifoParser& parser,
        VertexLoader& vertex_loader,
        TextureCache& texture_cache,
        DependencyEventSink*& backend_sink,
        FifoGuestMemoryReadRecorder& parser_recorder,
        DependencyEventSink& sink)
        : parser_(parser), vertex_loader_(vertex_loader),
          texture_cache_(texture_cache), backend_sink_(backend_sink) {
        parser_.set_memory_read_recorder(&parser_recorder);
        vertex_loader_.set_dependency_event_sink(&sink);
        texture_cache_.set_dependency_event_sink(&sink);
        backend_sink_ = &sink;
    }
    ScopedRenderDependencyCapture(
        const ScopedRenderDependencyCapture&) = delete;
    ScopedRenderDependencyCapture& operator=(
        const ScopedRenderDependencyCapture&) = delete;
    ~ScopedRenderDependencyCapture() {
        backend_sink_ = nullptr;
        texture_cache_.set_dependency_event_sink(nullptr);
        vertex_loader_.set_dependency_event_sink(nullptr);
        parser_.set_memory_read_recorder(nullptr);
    }

private:
    FifoParser& parser_;
    VertexLoader& vertex_loader_;
    TextureCache& texture_cache_;
    DependencyEventSink*& backend_sink_;
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

// Optional calling-thread counters. Availability is separate from value: zero
// is a valid counter sample, and failed queries must never enter subtraction.
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
    ULONG64 cycles = 0u;
    if (QueryThreadCycleTime(GetCurrentThread(), &cycles) == FALSE) return {};
    // Raw count, never converted to time. Frequency and instrumentation affect
    // its interpretation; this counter alone cannot distinguish work/waits.
    return {static_cast<std::uint64_t>(cycles), true};
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
    // Kernel+user CPU duration in 100ns units. Granularity is host-dependent;
    // wall minus CPU also includes blocking, not solely scheduler preemption.
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
    // Kernel + user, both already 100 ns ticks. Self-consistent with the sample
    // taken at the top of render_frame_on_thread, so the delta is a real CPU-time
    // delta for this thread, independent of how often it was descheduled.
    return {kernel_ticks.QuadPart + user_ticks.QuadPart, true};
}

// Always-on flush timing, sampled one call in kFlushTimingSampleStride so the meter does not
// become the measured path.
//
// The `*_us` fields inside `flush_draw_state` are gated on
// `frame_microprofile_enabled_ || trace_gx_stalls_enabled()`, so they exist only when extra
// steady_clock reads run on every frame. Their values are upper bounds that include the
// profiler's own overhead, so they cannot size the real work.
// One steady_clock pair per draw costs render-thread time on a large frame, so the clock is read
// for only one call in kFlushTimingSampleStride.
//
// Report fields:
//   - `flush-mean-us` = frame_flush_total_us_ / frame_flush_timed_samples_ is the per-call cost,
//     the mean over the sampled calls. Compare builds with this field.
//   - `flush-total-us` is the sum over the sampled calls only, roughly 1/64 of the frame's flush
//     work. It is not scaled up, because that assumes the sample is representative. It is a
//     diagnostic for the sampler (see the note at its print site).
//   - `flush-timed-samples` is the denominator and the sampler's health signal. If it is 0, no
//     call was timed and the other two fields are not meaningful.
//
// `frame_flush_timed_calls_` is monotonic across the session; see the note where the per-frame
// counters are reset.
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
        // Every Nth call of the session. `call_count` is monotonic, so the residue walks all 64 values
        // and the samples spread across call positions regardless of how many flushes a frame makes. Do
        // not tie this to a per-frame counter: with a stride of 64 and fewer flushes per frame than that,
        // only call 0 of each frame would be sampled.
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

bool capture_all_textures_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(
                   &length, value, sizeof(value),
                   "GALAXY_GX_CAPTURE_TEXTURES") == 0 &&
               length > 1 && value[0] != '0';
    }();
    return enabled;
}

bool capture_tev_log_enabled(unsigned draw_index) {
    static const std::uint64_t start = read_env_u64(
        "GALAXY_GX_CAPTURE_TEV_DRAW_START", 2329u);
    static const std::uint64_t end = read_env_u64(
        "GALAXY_GX_CAPTURE_TEV_DRAW_END", 2334u);
    return draw_index >= start && draw_index <= end;
}

std::uint32_t capture_texaddr_target() {
    static const std::uint32_t texaddr = static_cast<std::uint32_t>(
        read_env_u64("GALAXY_GX_CAPTURE_TEV_TEXADDR", 0u));
    return texaddr;
}

bool capture_texaddr_log_enabled(std::uint32_t guest_addr) {
    const std::uint32_t texaddr = capture_texaddr_target();
    return texaddr != 0u && guest_addr == texaddr;
}

bool trace_thp_draw_enabled() {
    static const bool enabled =
        read_env_u64("GALAXY_TRACE_THP_DRAW", 0u) != 0u;
    return enabled;
}

bool trace_thp_draw_all_enabled() {
    static const bool enabled =
        read_env_u64("GALAXY_TRACE_THP_DRAW_ALL", 0u) != 0u;
    return enabled;
}

std::uint64_t trace_thp_draw_first_n() {
    static const std::uint64_t first_n =
        read_env_u64("GALAXY_TRACE_THP_DRAW_FIRST_N", 0u);
    return first_n;
}

bool trace_thp_draw_frame_enabled(std::uint64_t frame) {
    if (!trace_thp_draw_enabled()) {
        return false;
    }
    static const std::uint64_t start =
        read_env_u64("GALAXY_TRACE_THP_DRAW_START_VI", 0u);
    static const std::uint64_t end =
        read_env_u64("GALAXY_TRACE_THP_DRAW_END_VI", UINT64_MAX);
    return frame >= start && frame <= end;
}

[[nodiscard]] bool tex_image_is_i8(const TexImage& image) {
    return image.format == TexFormat::I8 && image.guest_addr != 0u &&
           image.width != 0u && image.height != 0u;
}

[[nodiscard]] bool looks_like_thp_yuv_draw(
    const GxState& state,
    PrimitiveClass primitive,
    const LoadedPrimitive& prim) {
    const GenMode gen = state.gen_mode();
    if ((primitive != PrimitiveClass::Quads &&
         primitive != PrimitiveClass::Quads2) ||
        prim.vertex_count != 4u ||
        gen.num_texgens < 2u ||
        gen.num_tev_stages != 4u) {
        return false;
    }

    const TexImage y = state.tex_image(0);
    const TexImage u = state.tex_image(1);
    const TexImage v = state.tex_image(2);
    if (!tex_image_is_i8(y) || !tex_image_is_i8(u) || !tex_image_is_i8(v)) {
        return false;
    }

    const bool half_chroma =
        u.width * 2u == y.width && v.width * 2u == y.width &&
        u.height * 2u == y.height && v.height * 2u == y.height;
    const TevOrder s0 = state.tev_order(0);
    const TevOrder s1 = state.tev_order(1);
    const TevOrder s2 = state.tev_order(2);
    const TevOrder s3 = state.tev_order(3);
    const bool thp_orders =
        s0.tex_enable && s0.texmap == 1u && s0.texcoord == 1u &&
        s1.tex_enable && s1.texmap == 2u && s1.texcoord == 1u &&
        s2.tex_enable && s2.texmap == 0u && s2.texcoord == 0u &&
        !s3.tex_enable;
    return half_chroma && thp_orders;
}

[[nodiscard]] std::uint32_t trace_tex_data_size(const TexImage& image) {
    const auto blocks = [](std::uint32_t dim, std::uint32_t block) {
        return (dim + block - 1u) / block;
    };
    switch (image.format) {
    case TexFormat::I4:
        return blocks(image.width, 8u) * blocks(image.height, 8u) * 32u;
    case TexFormat::I8:
    case TexFormat::IA4:
        return blocks(image.width, 8u) * blocks(image.height, 4u) * 32u;
    case TexFormat::IA8:
    case TexFormat::RGB565:
    case TexFormat::RGB5A3:
        return blocks(image.width, 4u) * blocks(image.height, 4u) * 32u;
    case TexFormat::RGBA8:
        return blocks(image.width, 4u) * blocks(image.height, 4u) * 64u;
    case TexFormat::C4:
        return blocks(image.width, 8u) * blocks(image.height, 8u) * 32u;
    case TexFormat::C8:
        return blocks(image.width, 8u) * blocks(image.height, 4u) * 32u;
    case TexFormat::C14X2:
        return blocks(image.width, 4u) * blocks(image.height, 4u) * 32u;
    case TexFormat::CMPR:
        return blocks(image.width, 8u) * blocks(image.height, 8u) * 32u;
    default:
        return 0u;
    }
}

[[nodiscard]] const std::byte* resolve_guest_const_bytes(
    GuestMemoryV1* memory,
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
        const GuestMemoryRegionV1& r = memory->regions[i];
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

void trace_thp_plane_stats(
    const char* label,
    const TexImage& image,
    GuestMemoryV1* memory) {
    const std::uint32_t size = trace_tex_data_size(image);
    const std::byte* bytes =
        resolve_guest_const_bytes(memory, image.guest_addr, size);
    if (bytes == nullptr || size == 0u) {
        std::fprintf(stderr,
            "[thptex] %s addr=%08X size=%ux%u fmt=%u bytes=%u unmapped\n",
            label, image.guest_addr, image.width, image.height,
            static_cast<unsigned>(image.format), size);
        return;
    }

    std::uint8_t min_value = 255u;
    std::uint8_t max_value = 0u;
    std::uint64_t sum = 0u;
    std::uint32_t nonzero = 0u;
    std::uint32_t changes = 0u;
    std::uint8_t previous =
        static_cast<std::uint8_t>(std::to_integer<unsigned char>(bytes[0]));
    for (std::uint32_t i = 0; i < size; ++i) {
        const std::uint8_t value =
            static_cast<std::uint8_t>(std::to_integer<unsigned char>(bytes[i]));
        min_value = std::min(min_value, value);
        max_value = std::max(max_value, value);
        sum += value;
        nonzero += value != 0u ? 1u : 0u;
        if (i != 0u && value != previous) {
            ++changes;
        }
        previous = value;
    }
    const double mean = static_cast<double>(sum) / static_cast<double>(size);
    std::fprintf(stderr,
        "[thptex] %s addr=%08X size=%ux%u fmt=%u bytes=%u "
        "min=%u max=%u mean=%.3f nonzero=%u changes=%u first=",
        label, image.guest_addr, image.width, image.height,
        static_cast<unsigned>(image.format), size,
        static_cast<unsigned>(min_value), static_cast<unsigned>(max_value),
        mean, nonzero, changes);
    const std::uint32_t sample_count = std::min<std::uint32_t>(16u, size);
    for (std::uint32_t i = 0; i < sample_count; ++i) {
        const std::uint8_t value =
            static_cast<std::uint8_t>(std::to_integer<unsigned char>(bytes[i]));
        std::fprintf(stderr, "%s%02X", i == 0u ? "" : ",", value);
    }
    std::fprintf(stderr, "\n");
}

void trace_thp_draw_state(
    std::uint64_t frame,
    unsigned draw_index,
    PrimitiveClass primitive,
    const LoadedPrimitive& prim,
    const GxState& state,
    const PixelShaderKey& ps_key,
    GuestMemoryV1* memory,
    UploadRing& vertex_ring,
    bool force_log) {
    const GenMode gen = state.gen_mode();
    const bool exact = looks_like_thp_yuv_draw(state, primitive, prim);
    if (!exact && !force_log && !trace_thp_draw_all_enabled()) {
        return;
    }
    if (!exact && !force_log &&
        (gen.num_tev_stages < 4u || gen.num_texgens < 2u ||
         prim.vertex_count > 8u)) {
        return;
    }

    std::fprintf(stderr,
        "[thpdraw] frame=%llu draw=%u exact=%u prim=%u vtx=%u idx=%u "
        "texgens=%u tev=%u cmode=0x%06X/0x%06X zmode=0x%02X "
        "acmp=0x%06X\n",
        static_cast<unsigned long long>(frame), draw_index, exact ? 1u : 0u,
        static_cast<unsigned>(primitive), prim.vertex_count, prim.index_count,
        static_cast<unsigned>(gen.num_texgens),
        static_cast<unsigned>(gen.num_tev_stages),
        state.bp(static_cast<std::uint8_t>(0x41)),
        state.bp(static_cast<std::uint8_t>(0x42)),
        state.bp(static_cast<std::uint8_t>(0x40)),
        state.bp(bp::kAlphaCompare));
    const auto regs = state.tev_register_colors();
    const auto konst = state.tev_konst_colors();
    std::fprintf(stderr,
        "[thpdraw] regs prev=%08X r0=%08X r1=%08X r2=%08X "
        "k0=%08X k1=%08X k2=%08X k3=%08X\n",
        regs[0], regs[1], regs[2], regs[3],
        konst[0], konst[1], konst[2], konst[3]);
    for (unsigned s = 0; s < gen.num_tev_stages && s < 4u; ++s) {
        const TevStageConfig cfg = state.tev_stage(s);
        std::fprintf(stderr,
            "[thptev] stage=%u cenv=0x%06X aenv=0x%06X order=0x%03X "
            "map=%u coord=%u ten=%u kc=%u ka=%u dest=%u/%u\n",
            s, ps_key.stages[s].color_env, ps_key.stages[s].alpha_env,
            ps_key.stages[s].order,
            static_cast<unsigned>(cfg.order.texmap),
            static_cast<unsigned>(cfg.order.texcoord),
            cfg.order.tex_enable ? 1u : 0u,
            static_cast<unsigned>(cfg.kcsel),
            static_cast<unsigned>(cfg.kasel),
            static_cast<unsigned>(cfg.color_dest),
            static_cast<unsigned>(cfg.alpha_dest));
    }
    for (unsigned m = 0; m < 3u; ++m) {
        const TexImage image = state.tex_image(m);
        const TexMode mode = state.tex_mode(m);
        std::fprintf(stderr,
            "[thptex] map=%u addr=%08X size=%ux%u fmt=%u wrap=%u/%u "
            "min=%u mag=%u lod=%u..%u bias=%d\n",
            m, image.guest_addr, image.width, image.height,
            static_cast<unsigned>(image.format),
            static_cast<unsigned>(mode.wrap_s),
            static_cast<unsigned>(mode.wrap_t),
            static_cast<unsigned>(mode.min_filter),
            static_cast<unsigned>(mode.mag_filter),
            static_cast<unsigned>(mode.min_lod_x16),
            static_cast<unsigned>(mode.max_lod_x16),
            static_cast<int>(mode.lod_bias_x32));
    }
    trace_thp_plane_stats("Y", state.tex_image(0), memory);
    trace_thp_plane_stats("U", state.tex_image(1), memory);
    trace_thp_plane_stats("V", state.tex_image(2), memory);

    void* ring_cpu = nullptr;
    const D3D12_RANGE no_read{0, 0};
    if (FAILED(vertex_ring.resource()->Map(0, &no_read, &ring_cpu)) ||
        ring_cpu == nullptr) {
        std::fprintf(stderr, "[thpdraw] vertices unavailable\n");
        return;
    }
    const auto* verts = reinterpret_cast<const GxVertexOut*>(
        static_cast<const std::byte*>(ring_cpu) + prim.vertex_byte_offset);
    const std::uint32_t vertex_limit =
        std::min<std::uint32_t>(prim.vertex_count, 8u);
    for (std::uint32_t v = 0; v < vertex_limit; ++v) {
        std::fprintf(stderr,
            "[thpvtx] v=%u pos=(%.3f,%.3f,%.3f) "
            "uv0=(%.6f,%.6f) uv1=(%.6f,%.6f) col0=%08X mi=%08X/%08X/%08X\n",
            v, verts[v].position[0], verts[v].position[1],
            verts[v].position[2], verts[v].uv[0][0], verts[v].uv[0][1],
            verts[v].uv[1][0], verts[v].uv[1][1],
            verts[v].color0, verts[v].mtx_indices[0],
            verts[v].mtx_indices[1], verts[v].mtx_indices[2]);
    }
    vertex_ring.resource()->Unmap(0, &no_read);
}

// Converts the raw XF projection registers to a column-major 4x4 matrix stored row-major (HLSL
// row_major float4x4 == stored row-by-row).
// GX perspective (type=0): maps to a left-hand clip space with z in [0,1].
// GX orthographic (type=1): same re-mapping.
//
// Build GxPsConstants from current GxState.
void fill_ps_constants(const GxState& state, GxPsConstants& out) {
    std::memset(&out, 0, sizeof(out));

    const GenMode gm = state.gen_mode();

    // TEV stage_config words. TevStagePacked is 4 x u32, laid out identically to uber_constants
    // GxPsConstants::stage_config[stage][0..3].
    for (unsigned s = 0; s < gm.num_tev_stages && s < kMaxTevStages; ++s) {
        // color_env: BP 0xC0 + 2*stage
        out.stage_config[s][0] =
            state.bp(static_cast<std::uint8_t>(bp::kTevColorEnvBase + s * 2u));
        // alpha_env: BP 0xC1 + 2*stage
        out.stage_config[s][1] =
            state.bp(static_cast<std::uint8_t>(bp::kTevAlphaEnvBase + s * 2u));
        // Repack TevOrder for this stage into low 12 bits of word 2.
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

    // TEV blending registers: PREV (index 0) and REG0-2 (indices 1-3).
    // Hardware stores signed 11-bit channel values; the shader converts these
    // back to int registers before running Dolphin-style TEV math.
    {
        const auto regs = state.tev_register_values();
        for (unsigned r = 0; r < 4; ++r) {
            out.tev_regs[r][0] = static_cast<float>(regs[r][0]) / 255.0f;
            out.tev_regs[r][1] = static_cast<float>(regs[r][1]) / 255.0f;
            out.tev_regs[r][2] = static_cast<float>(regs[r][2]) / 255.0f;
            out.tev_regs[r][3] = static_cast<float>(regs[r][3]) / 255.0f;
        }
    }

    // Konst K0-K3: same packing convention as tev_konst_colors().
    {
        const auto krgba = state.tev_konst_colors();
        for (unsigned k = 0; k < 4; ++k) {
            const std::uint32_t v = krgba[k];
            out.konst[k][0] = ((v >> 24) & 0xFFu) / 255.0f;  // R
            out.konst[k][1] = ((v >> 16) & 0xFFu) / 255.0f;  // G
            out.konst[k][2] = ((v >>  8) & 0xFFu) / 255.0f;  // B
            out.konst[k][3] = ((v       ) & 0xFFu) / 255.0f; // A
        }
    }

    // Alpha compare.
    {
        const AlphaCompare ac = state.alpha_compare();
        out.alpha_refs[0] = ac.ref0 / 255.0f;
        out.alpha_refs[1] = ac.ref1 / 255.0f;
        // Pack comp0|comp1|logic into a float-encoded uint for the uber shader.
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

    // Fog A (BP 0xEE) and C (BP 0xF1 bits 0-19) are 20-bit packed floats, unpacked here on the
    // CPU the same way Dolphin's PixelShaderManager SetFogParamChanged stores GetA()/GetC().
    // B magnitude (u24) and B shift (u5) stay integral: both are exactly representable in float,
    // and the PS recovers them with uint() casts.
    {
        const FogParams fp = state.fog();
        out.fog_params[0] =
            std::bit_cast<float>(fog_param_float_bits(fp.a_raw & 0xFFFFFu));
        out.fog_params[1] =
            static_cast<float>(fp.b_magnitude_raw & 0xFFFFFFu);
        out.fog_params[2] = static_cast<float>(fp.b_shift_raw & 0x1Fu);
        out.fog_params[3] =
            std::bit_cast<float>(fog_param_float_bits(fp.c_raw));
        // color rgb as normalized floats in [4..6].
        out.fog_params[4] = ((fp.color_rgb >> 16) & 0xFFu) / 255.0f;
        out.fog_params[5] = ((fp.color_rgb >>  8) & 0xFFu) / 255.0f;
        out.fog_params[6] = ((fp.color_rgb      ) & 0xFFu) / 255.0f;
        // Projection flag (1.0 = orthographic; FogProjection enum).
        out.fog_params[7] = fp.projection ? 1.0f : 0.0f;
    }

    // Texture dimensions and GX texture-coordinate scale. Dolphin keeps both
    // in the same constant: xy are the sampled texture dimensions; zw are
    // BP 0x30-0x3F scale_minus_1 + 1 for the texcoord channel. Indirect TEV
    // uses the coord scale before sampling the selected texmap.
    for (unsigned m = 0; m < kMaxTextureMaps; ++m) {
        const TexImage img = state.tex_image(m);
        const float w = static_cast<float>(img.width  > 0 ? img.width  : 1);
        const float h = static_cast<float>(img.height > 0 ? img.height : 1);
        const std::uint32_t s_scale_raw =
            state.bp(static_cast<std::uint8_t>(
                bp::kTexCoordSizeBase + m * 2u));
        const std::uint32_t t_scale_raw =
            state.bp(static_cast<std::uint8_t>(
                bp::kTexCoordSizeBase + m * 2u + 1u));
        out.tex_dims[m][0] = w;
        out.tex_dims[m][1] = h;
        out.tex_dims[m][2] =
            static_cast<float>((s_scale_raw & 0xFFFFu) + 1u);
        out.tex_dims[m][3] =
            static_cast<float>((t_scale_raw & 0xFFFFu) + 1u);
    }

    // Z-texture bias (BP 0xF4, 24-bit z24 units), normalized to [0,1).
    out.ztex_params[0] =
        static_cast<float>(state.bp(bp::kTevZEnv0) & 0xFFFFFFu) / 16777216.0f;

    // Indirect TEV commands and constants. Matrix packing mirrors Dolphin's
    // pixel constants: rows are {ma,mc,me,17-scale} and {mb,md,mf,17-scale}.
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
    D3D12_PRIMITIVE_TOPOLOGY& topology,
    D3D12_PRIMITIVE_TOPOLOGY_TYPE& topology_type) {
    topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    switch (primitive) {
    case PrimitiveClass::Lines:
    case PrimitiveClass::LineStrip:
        topology = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
        break;
    case PrimitiveClass::Points:
        topology = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
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
// One contiguous snapshot 0x0000-0x067F keeps the upload a single memcpy and
// lets the VS fetch matrices AND lights from the same structured buffer.
inline constexpr std::size_t kMatrixPaletteWords = 0x0680u;  // 0x0000-0x067F
// The palette upload copies only the chunks `GxState` reports as written, so this bound and
// GxState's must describe the same word range. Otherwise the upload would silently skip words
// inside the palette or copy past its end.
static_assert(
    kMatrixPaletteWords == galaxy::gx::GxState::kMatrixPaletteWords,
    "backend palette bound must match the tracked GxState palette range");

}  // anonymous namespace

FramePeEventClassifier::FramePeEventClassifier(bool event_only)
    : event_only_(event_only) {
    parser_.set_event_classification_only(event_only_);
    static const bool verify = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(&length, value, sizeof(value), "GALAXY_PE_CLASSIFY_VERIFY") == 0 &&
            length > 1u && value[0] != '0';
    }();
    if (event_only_ && verify) {
        verifier_ = std::make_unique<FramePeEventClassifier>(false);
    }
}

void FramePeEventClassifier::reset() {
    if (active_events_ != nullptr) {
        throw std::logic_error(
            "GX PE event classifier reset during an active parse");
    }
    if (verifier_ != nullptr) {
        verifier_->reset();
    }
    state_ = GxState{};
    parser_ = FifoParser{};
    parser_.set_event_classification_only(event_only_);
    parser_.set_dump_dir("generated");
    pending_fifo_.clear();
}

void FramePeEventClassifier::classify(
    const std::byte* fifo_data,
    std::size_t fifo_size,
    GuestMemoryV1* memory,
    std::vector<FramePeEventSignature>& events,
    bool frozen_regions) {
    events.clear();
    if (fifo_data == nullptr || fifo_size == 0u) {
        return;
    }
    if (active_events_ != nullptr) {
        throw std::logic_error("GX PE event classifier is reentrant");
    }

    active_events_ = &events;
    try {
        if (pending_fifo_.empty()) {
            const std::span<const std::byte> fifo(fifo_data, fifo_size);
            const std::size_t consumed =
                parser_.run_available(fifo, memory, *this, state_, frozen_regions);
            if (consumed < fifo.size()) {
                pending_fifo_.assign(
                    fifo.begin() + static_cast<std::ptrdiff_t>(consumed),
                    fifo.end());
            }
        } else {
            pending_fifo_.insert(
                pending_fifo_.end(), fifo_data, fifo_data + fifo_size);
            const std::size_t consumed = parser_.run_available(
                pending_fifo_, memory, *this, state_, frozen_regions);
            if (consumed == pending_fifo_.size()) {
                pending_fifo_.clear();
            } else if (consumed > 0u) {
                pending_fifo_.erase(
                    pending_fifo_.begin(),
                    pending_fifo_.begin() +
                        static_cast<std::ptrdiff_t>(consumed));
            }
        }
    } catch (...) {
        active_events_ = nullptr;
        throw;
    }
    active_events_ = nullptr;
    if (verifier_ != nullptr) {
        verifier_->classify(fifo_data, fifo_size, memory, verifier_events_, frozen_regions);
        ++verified_chunks_;
        if (verifier_events_ != events) {
            ++verify_mismatches_;
            if (verify_mismatches_ <= 16u) {
                std::cerr << "[pe-classify-verify] mismatch chunk=" << verified_chunks_
                          << " lean-events=" << events.size()
                          << " full-events=" << verifier_events_.size()
                          << " fifo-bytes=" << fifo_size << '\n';
            }
        }
        if ((verified_chunks_ % 6000u) == 0u) {
            std::cerr << "[pe-classify-verify] chunks=" << verified_chunks_
                      << " mismatches=" << verify_mismatches_ << '\n';
        }
    }
}

std::size_t FramePeEventClassifier::invalidate_display_list_cache_range(
    std::uint32_t guest_addr,
    std::uint32_t size) {
    if (verifier_ != nullptr) {
        (void)verifier_->invalidate_display_list_cache_range(guest_addr, size);
    }
    return parser_.invalidate_display_list_cache_range(guest_addr, size);
}

std::size_t FramePeEventClassifier::draw_payload_size(
    std::uint8_t vtxfmt,
    std::uint16_t vertex_count) const {
    // Recompute the layout only when a CP register that describes it moved.
    // This is an exact test, not a heuristic: `vertex_desc(vtxfmt)` reads
    // exactly CP VCD_LO, VCD_HI and VAT_A/B/C[vtxfmt], and
    // `source_vertex_size()` is a pure function of that descriptor. The parser
    // writes those registers through the same `state_` this sink reads, so a
    // draw observes the register values that were in effect for it.
    if (vtxfmt >= vtx_payload_stride_memo_.size()) {
        return VertexLoader::source_vertex_size(state_.vertex_desc(vtxfmt)) *
            static_cast<std::size_t>(vertex_count);
    }
    VtxPayloadStrideMemo& slot = vtx_payload_stride_memo_[vtxfmt];
    const std::uint32_t vcd_lo = state_.cp(cp::kVcdLo);
    const std::uint32_t vcd_hi = state_.cp(cp::kVcdHi);
    const std::uint32_t vat_a =
        state_.cp(static_cast<std::uint8_t>(cp::kVatABase + vtxfmt));
    const std::uint32_t vat_b =
        state_.cp(static_cast<std::uint8_t>(cp::kVatBBase + vtxfmt));
    const std::uint32_t vat_c =
        state_.cp(static_cast<std::uint8_t>(cp::kVatCBase + vtxfmt));
    if (!slot.valid || slot.vcd_lo != vcd_lo || slot.vcd_hi != vcd_hi ||
        slot.vat_a != vat_a || slot.vat_b != vat_b || slot.vat_c != vat_c) {
        slot.valid = true;
        slot.vcd_lo = vcd_lo;
        slot.vcd_hi = vcd_hi;
        slot.vat_a = vat_a;
        slot.vat_b = vat_b;
        slot.vat_c = vat_c;
        slot.stride =
            VertexLoader::source_vertex_size(state_.vertex_desc(vtxfmt));
    }
    return slot.stride * static_cast<std::size_t>(vertex_count);
}

void FramePeEventClassifier::on_draw(
    PrimitiveClass,
    std::uint8_t vtxfmt,
    FifoCursor& cursor) {
    const std::uint16_t vertex_count = cursor.read_u16();
    (void)cursor.take(draw_payload_size(vtxfmt, vertex_count));
}

bool FramePeEventClassifier::on_cached_simple_draw_run(
    PrimitiveClass,
    std::uint8_t,
    std::span<const std::byte>,
    std::size_t,
    std::span<const CachedDrawPacket>) {
    return true;
}

bool FramePeEventClassifier::on_cached_prepared_draw_run(
    PrimitiveClass,
    std::uint8_t,
    std::span<const std::byte>,
    std::size_t,
    std::size_t,
    std::uint8_t,
    std::size_t) {
    return true;
}

bool FramePeEventClassifier::on_cached_packet_draw_run(
    std::uint64_t,
    std::size_t,
    PrimitiveClass,
    std::uint8_t,
    std::span<const std::byte>,
    std::size_t,
    std::span<const CachedDrawPacket>,
    std::uint32_t,
    std::uint32_t,
    std::span<const std::uint16_t>) {
    return true;
}

void FramePeEventClassifier::on_efb_copy(std::uint32_t) {}

void FramePeEventClassifier::on_pe_finish() {
    if (active_events_ == nullptr) {
        throw std::logic_error(
            "GX PE classifier emitted FINISH outside an active parse");
    }
    active_events_->push_back(
        FramePeEventSignature{FramePeEventSignature::Kind::Finish, 0u, false});
}

void FramePeEventClassifier::on_pe_token(
    std::uint16_t token,
    bool interrupt) {
    if (active_events_ == nullptr) {
        throw std::logic_error(
            "GX PE classifier emitted TOKEN outside an active parse");
    }
    active_events_->push_back(
        FramePeEventSignature{
            FramePeEventSignature::Kind::Token,
            token,
            interrupt});
}

void FramePeEventClassifier::on_invalidate_textures() {}
void FramePeEventClassifier::on_tlut_load() {}
void FramePeEventClassifier::on_invalidate_vertex_cache() {}

std::size_t GxBackend::invalidate_dirty_display_list_ranges(
    FifoParser& parser,
    const std::vector<GxBackend::GuestWriteRange>& ranges) {
    if (!display_list_dirty_invalidate_enabled()) {
        return 0;
    }
    std::size_t invalidated = 0;
    for (const GxBackend::GuestWriteRange& range : ranges) {
        invalidated += parser.invalidate_display_list_cache_range(
            range.guest_addr,
            range.size);
    }
    return invalidated;
}

void GxBackend::reset_fifo_session_state() {
    // The render parser, direct-event parser, dependency scanner, and PE
    // preclassifier consume the same command stream on different schedules.
    // Reset them atomically while no producer or render thread exists. A
    // stale state register or incomplete command tail in only one mirror can
    // otherwise change command boundaries and forge an event-free receipt.
    state_ = GxState{};
    state_.mark_all_dirty();
    // Replacing the parser releases every cached display list of the previous
    // session together with the retained vector capacity, so the new session
    // starts from an empty cache with exact byte accounting.
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
    dependency_draw_run_range_cache_.clear();
    dependency_draw_run_range_cache_tick_ = 0u;
    dependency_draw_run_range_cache_bytes_ = 0u;

    pe_event_classifier_.reset();
    pending_async_dirty_ranges_.clear();
    decoded_packet_run_cache_.clear();
    decoded_packet_run_cache_tick_ = 0u;
    decoded_packet_run_cache_bytes_ = 0u;
    // Runs before the renderer (re)initializes: nothing can still read it.
    release_resident_vertex_buffer();
    std::vector<GxVertexOut>{}.swap(decoded_packet_vertex_scratch_);
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

    // Parsed register state feeds these derived CPU/GPU caches. They cannot
    // survive a renderer teardown: several entries contain descriptor or
    // resource identities owned by the old D3D12 session.
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
    current_sampler_table_ = {};
    current_matrix_palette_ = 0u;
    matrix_palette_stale_ = true;
    current_palette_required_ = true;
    inline_matrices_stale_ = true;
    // No palette bytes are valid anywhere until an upload lands in the new
    // allocation; the ring segment is reused, so the previous frame's chunks
    // must not be treated as current.
    xf_palette_valid_ = 0u;
    frame_bindings_dirty_ = true;
    texture_bindings_dirty_ = true;
    current_texture_map_mask_ = 0u;
    current_texture_map_mask_valid_ = false;
    current_texture_binding_key_ = {};
    current_texture_binding_key_mask_ = 0u;
    // clear_texture_binding_table_cache() also clears
    // current_texture_binding_key_valid_, so a remembered key can never outlive
    // the frame-local tables it was captured alongside.
    clear_texture_binding_table_cache();
    texture_handle_cache_.clear();
    dirty_texture_ranges_scratch_.clear();

    // The new EFB/XFB resources have no relationship to the old session's
    // seed, frame stamps, or registry addresses.
    frame_index_ = 0u;
    timing_frame_index_.store(0u, std::memory_order_relaxed);
    causal_trace_frame_index_.store(0u, std::memory_order_relaxed);
    deferred_xfb_causal_count_ = 0u;
    deferred_xfb_causal_dropped_ = 0u;
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

// GxBackend lifecycle

GxBackend::~GxBackend() noexcept {
    if (!initialized_) {
        return;
    }
    try {
        shutdown();
    } catch (const std::exception& error) {
        std::cerr
            << "[GxBackend] fatal "
            << (std::uncaught_exceptions() != 0
                    ? "stack-unwind"
                    : "destructor")
            << " shutdown failure: " << error.what() << '\n';
        std::terminate();
    } catch (...) {
        std::cerr
            << "[GxBackend] fatal "
            << (std::uncaught_exceptions() != 0
                    ? "stack-unwind"
                    : "destructor")
            << " shutdown failure: non-standard exception\n";
        std::terminate();
    }
}

bool GxBackend::initialize(int w, int h) {
    if (initialized_) {
        return true;
    }

    if (worker_memory_snapshot_enabled()) {
        const GxTimingConfig& timing = get_gx_timing_config();
        if (!async_render_thread_enabled() ||
            !timing.render_live_memory_wait || timing.render_memory_snapshot ||
            !timing.pe_events_gpu_fence || immutable_range_ownership_enabled()) {
            throw std::logic_error(
                "[GxBackend] worker memory snapshot requires asynchronous live "
                "memory waiting, GPU-fenced PE events, and pooled strict copies");
        }
    }

    cadence_diagnostics_ = cadence::global_session_if_enabled();

    // Reset runs before native resource acquisition. It audits the previous session and advances
    // the token epoch, so a failed prior session cannot be hidden by renderer reinitialization, and
    // an old token can never alias this one.
    frame_pe_completions_.reset();
    reset_fifo_session_state();
    deferred_gx_stats_count_ = 0;
    deferred_gx_stats_dropped_ = 0;
    // Reservoir state must reset with the ring it indexes: a stale `seen_` would
    // make the first replacement index of the new session out of range for the
    // new sample count, and a stale rng would correlate the two sessions' rows.
    deferred_gx_stats_seen_ = 0;
    deferred_gx_stats_rng_ = 0x9e3779b97f4a7c15ull;
    deferred_gx_stats_first_drop_frame_ = 0;

    const RenderConfig cfg = get_render_config();

    if (!renderer_.initialize(w, h, cfg.efb_scale)) {
        std::cerr << "[GxBackend] RendererD3D12::initialize failed\n";
        return false;
    }

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
    // The resident decoded-vertex buffer (a 64 MB upload heap) is created with the renderer rather
    // than at first use in gameplay, where its creation is slow.
    if (resident_decoded_vertices_enabled()) {
        (void)ensure_resident_vertex_buffer();
    }
    renderer_.create_first_use_resources();

    // Determine shader-cache directory.
    std::filesystem::path cache_dir;
    {
        wchar_t* local_app = nullptr;
        std::size_t local_app_len = 0;
        if (_wdupenv_s(&local_app, &local_app_len, L"LOCALAPPDATA") == 0 && local_app != nullptr) {
            cache_dir = std::filesystem::path(local_app)
                / L"Nebula" / L"shadercache" / L"RMGE01";
            std::free(local_app);
        } else {
            if (local_app) std::free(local_app);
            cache_dir = std::filesystem::path("generated") / "shadercache";
        }
    }

    // Worker count: keep low-end systems conservative, but let high-end
    // machines warm cached PSOs before gameplay reaches shader-heavy scenes.
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

    initialized_ = true;
    stop_render_thread_ = false;
    render_chunks_in_flight_ = 0;
    present_chunks_in_flight_ = 0;
    utility_chunks_in_flight_ = 0;
    render_cpu_reads_in_flight_ = 0;
    render_fifo_effects_in_flight_ = 0;
    pending_frame_effects_.clear();
    pending_pointer_depth_capture_ = {};
    if (async_render_thread_enabled()) {
        render_thread_ = std::thread(&GxBackend::render_thread_main, this);
    }
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

void GxBackend::emit_deferred_gx_stats() {
    // shutdown() calls this only after joining the render thread. Do all potentially blocking
    // stderr work here, never from render_frame_on_thread.
    for (std::size_t index = 0; index < deferred_gx_stats_count_; ++index) {
        const DeferredGxStatsSample& sample = deferred_gx_stats_[index];
        const XfbPresentStats& present_stats = sample.present;
        std::cerr << std::dec
                  << "[gx-stats] frame " << sample.frame_index
                  << " total-draws=" << sample.total_draws
                  << " total-no-pso=" << sample.total_no_pso
                  << " frame-draws=" << sample.frame_draws
                  << " frame-no-pso=" << sample.frame_no_pso
                  << " xfb-copies=" << sample.xfb_copies
                  << " tex-copies=" << sample.tex_copies
                  << " specialized-hits=" << sample.specialized_hits
                  << " uber-fallbacks=" << sample.uber_fallbacks
                  << " xfb-latest=" << (sample.has_latest_xfb ? 1 : 0)
                  << " xfb-present-requests=" << present_stats.requests
                  << " xfb-present-presented=" << present_stats.presented
                  << " xfb-first-serial-presentations="
                  << present_stats.first_serial_presentations
                  << " xfb-repeated-or-stale-serial-requests="
                  << present_stats.repeated_or_stale_serial_requests
                  << " xfb-present-duplicate-run-max="
                  << present_stats.duplicate_run_max
                  << " xfb-out-of-order-serial-requests="
                  << present_stats.out_of_order_serial_requests
                  << " xfb-present-serial-delta0="
                  << present_stats.serial_delta_zero
                  << " xfb-present-serial-delta1="
                  << present_stats.serial_delta_one
                  << " xfb-present-serial-deltaMulti="
                  << present_stats.serial_delta_multi
                  << " xfb-present-last-addr=0x" << std::hex
                  << present_stats.last_presented_xfb_addr << std::dec
                  << " xfb-present-last-serial="
                  << present_stats.last_presented_copy_serial
                  << " xfb-present-last-stamp="
                  << present_stats.last_presented_frame_stamp
                  << " xfb-stale-fallbacks=" << sample.stale_fallbacks
                  << " xfb-missing-fallbacks=" << sample.missing_fallbacks
                  << " xfb-stale-preserved=" << sample.stale_preserved
                  << " xfb-missing-preserved=" << sample.missing_preserved
                  << " render-ms="
                  << static_cast<double>(sample.render_ns) / 1000000.0
                  << " producer-wait-ms="
                  << static_cast<double>(sample.queue_wait_ns) / 1000000.0
                  << " dependency-scan-ms="
                  << static_cast<double>(sample.dependency_scan_ns) /
                         1000000.0
                  << " dependency-cache-hits="
                  << sample.dependency_cache_hits
                  << " dependency-cache-misses="
                  << sample.dependency_cache_misses
                  << " dependency-cache-invalidations="
                  << sample.dependency_cache_invalidations
                  << " dependency-draw-run-cache-hits="
                  << sample.dependency_draw_run_cache_hits
                  << " dependency-draw-run-cache-misses="
                  << sample.dependency_draw_run_cache_misses
                  << " dependency-draw-run-cache-evictions="
                  << sample.dependency_draw_run_cache_evictions
                  << " snapshot-ms="
                  << static_cast<double>(sample.snapshot_ns) / 1000000.0
                  << " snapshot-bytes=" << sample.snapshot_bytes
                  << " snapshot-ranges=" << sample.snapshot_ranges
                  << " dirty-ranges=" << sample.dirty_ranges
                  << " dirty-bytes=" << sample.dirty_bytes
                  << " dirty-tex-evictions="
                  << sample.dirty_texture_evictions
                  << " dirty-dl-invalidations="
                  << sample.dirty_display_list_invalidations
                  << " palette-uploads=" << sample.palette_uploads
                  << " inline-matrix-uploads="
                  << sample.inline_matrix_uploads
                  // Clock-free per-frame attribution. `flush-calls` is the denominator for `flush-pso-us`: us per
                  // call is flush-pso-us / flush-calls, and draws per call is draws / flush-calls. Both are valid
                  // on an untraced sampled frame, unlike the *_us fields on this line. `[gx-frame-timing]` prints
                  // only over-threshold frames, so it cannot answer a per-frame question.
                  << " draws=" << sample.draws
                  << " draw-batches=" << sample.draw_batches
                  << " flush-calls=" << sample.flush_calls
                  << " pso-resolutions=" << sample.pso_resolutions
                  << " pso-route-early=" << sample.pso_route_early
                  << " pso-route-memo=" << sample.pso_route_memo
                  << " pso-route-get=" << sample.pso_route_get
                  << " flush-pso-ns=" << sample.flush_pso_ns
                  << " vertex-load-ns=" << sample.vertex_load_ns
                  << " cached-vertex-upload-ns="
                  << sample.cached_vertex_upload_ns
                  << " draw-record-ns=" << sample.draw_record_ns << '\n';
    }
    if (deferred_gx_stats_dropped_ != 0u) {
        // The ring is filled by reservoir sampling, so the retained rows are a uniform draw over the
        // whole session, not a prefix of it. State the retention ratio and the first-overflow frame so a
        // reader can tell "spread over the run" from "stopped early". The wording must not imply that
        // the rows cover only the opening.
        std::cerr << "[gx-stats] deferred-samples-seen="
                  << deferred_gx_stats_seen_
                  << " retained=" << deferred_gx_stats_count_
                  << " not-retained=" << deferred_gx_stats_dropped_
                  << " overflow-from-frame=" << deferred_gx_stats_first_drop_frame_
                  << " -- rows are a uniform sample of the whole session; raise"
                  << " GALAXY_GX_FRAME_TIMING_SAMPLE to widen coverage"
                  << " (capacity " << deferred_gx_stats_.size() << ")\n";
    }
}

void GxBackend::emit_deferred_xfb_causal_trace() {
    // shutdown() invokes this only after the render thread has stopped. The
    // records are therefore coherent without adding an inter-thread lock to
    // the render path.
    for (std::size_t index = 0; index < deferred_xfb_causal_count_; ++index) {
        const DeferredXfbCausalRecord& record = deferred_xfb_causal_[index];
        std::cerr << "[xfb-causal-deferred] frame=" << record.frame_index
                  << " fifo-bytes=" << record.fifo_bytes
                  << " fifo-complete=" << (record.fifo_complete ? 1 : 0)
                  << " parsed-copy-exec=" << record.copy_exec_count
                  << " parsed-xfb-copy-exec=" << record.xfb_copy_count
                  << " parsed-texture-copy-exec=" << record.texture_copy_count
                  << " present-only=" << (record.present_only ? 1 : 0)
                  << " present-request=" << (record.present_request ? 1 : 0)
                  << " present-submitted="
                  << (record.present_submitted ? 1 : 0)
                  << " selected-xfb=0x" << std::hex
                  << record.selected_xfb << std::dec
                  << " selected-serial=" << record.selected_serial
                  << " global-xfb-copies=" << record.global_xfb_copies
                  << " pe-classified-events=" << record.pe_classified_events
                  << " pe-rendered-events=" << record.pe_rendered_events
                  << '\n';
    }
    if (deferred_xfb_causal_dropped_ != 0u) {
        std::cerr << "[xfb-causal-deferred] samples-dropped="
                  << deferred_xfb_causal_dropped_ << '\n';
    }
}

void GxBackend::shutdown() {
    frame_telemetry_.reset(); // Join before atomic counter owners are destroyed.
    if (!initialized_) {
        return;
    }
    if (render_thread_.joinable()) {
        // Stop the render thread first: it drains queued frames, then exits,
        // so the subsystems below are torn down with no consumer alive.
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            stop_render_thread_ = true;
        }
        queue_cv_.notify_one();
        queue_space_cv_.notify_one();
        render_thread_.join();
    }
    emit_deferred_gx_stats();
    emit_deferred_xfb_causal_trace();
    std::cerr << "[scene-markers] handled=" << scene_markers_seen_ << std::endl;
    std::cerr << "[render-queue] stalled-admissions=" << stalled_queue_admissions_
              << " stalled-max-depth=" << stalled_queue_max_depth_ << '\n';

    std::string shutdown_failures;
    const auto append_shutdown_failure = [&shutdown_failures](
                                             const std::string& failure) {
        if (!shutdown_failures.empty()) {
            shutdown_failures += "; ";
        }
        shutdown_failures += failure;
    };
    const auto describe_exception = [](std::exception_ptr error) {
        if (error == nullptr) {
            return std::string{};
        }
        try {
            std::rethrow_exception(error);
        } catch (const std::exception& exception) {
            return std::string(exception.what());
        } catch (...) {
            return std::string("non-standard exception");
        }
    };

    // No producer or render consumer is alive after join, so this is a stable
    // conservation snapshot. Capture it before clearing any queue: teardown is
    // never allowed to turn missing PE completion into an apparently clean
    // next session.
    const std::exception_ptr stored_render_error = render_thread_error_;
    render_thread_error_ = nullptr;
    const FramePeCompletionStats pe_stats = frame_pe_completions_.stats();
    try {
        frame_pe_completions_.audit_drained();
    } catch (const std::exception& error) {
        append_shutdown_failure(error.what());
    }
    if (stored_render_error != nullptr) {
        append_shutdown_failure(
            "stored render-thread failure: " +
            describe_exception(stored_render_error));
    }
    const bool queue_conserved =
        frame_queue_.empty() && pending_frame_effects_.empty() &&
        !pending_pointer_depth_capture_.capture &&
        render_chunks_in_flight_ == 0u &&
        present_chunks_in_flight_ == 0u &&
        utility_chunks_in_flight_ == 0u &&
        render_cpu_reads_in_flight_ == 0u &&
        render_fifo_effects_in_flight_ == 0u;
    if (!queue_conserved) {
        append_shutdown_failure(
            "GX shutdown queue conservation failed: queued=" +
            std::to_string(frame_queue_.size()) +
            " pending-effects=" +
            std::to_string(pending_frame_effects_.size()) +
            " render-in-flight=" +
            std::to_string(render_chunks_in_flight_) +
            " present-in-flight=" +
            std::to_string(present_chunks_in_flight_) +
            " utility-in-flight=" +
            std::to_string(utility_chunks_in_flight_) +
            " cpu-reads-in-flight=" +
            std::to_string(render_cpu_reads_in_flight_) +
            " fifo-effects-in-flight=" +
            std::to_string(render_fifo_effects_in_flight_));
    }
    stop_render_thread_ = false;
    frame_queue_.clear();
    render_chunks_in_flight_ = 0;
    present_chunks_in_flight_ = 0;
    utility_chunks_in_flight_ = 0;
    render_cpu_reads_in_flight_ = 0;
    render_fifo_effects_in_flight_ = 0;
    pending_frame_effects_.clear();
    pending_pointer_depth_capture_ = {};
    fifo_pool_.clear();
    memory_range_pool_.clear();
    memory_snapshot_pool_.clear();
    immutable_range_cache_.clear();
    immutable_range_cache_tick_ = 0u;
    immutable_range_cache_bytes_ = 0u;
    dependency_pending_fifo_.clear();
    dependency_draw_run_range_cache_.clear();
    dependency_draw_run_range_cache_tick_ = 0;
    dependency_draw_run_range_cache_bytes_ = 0;
    try {
        renderer_.drain_gpu();
    } catch (...) {
        append_shutdown_failure(
            "renderer drain failure: " +
            describe_exception(std::current_exception()));
    }
    for (auto& [key, entry] : decoded_packet_run_cache_) {
        (void)key;
        release_decoded_entry_residency(entry);
    }
    release_resident_vertex_buffer();
    try {
        pipeline_cache_.shutdown();
    } catch (...) {
        append_shutdown_failure(
            "pipeline-cache shutdown failure: " +
            describe_exception(std::current_exception()));
    }
    try {
        texture_cache_.shutdown();
    } catch (...) {
        append_shutdown_failure(
            "texture-cache shutdown failure: " +
            describe_exception(std::current_exception()));
    }
    try {
        efb_copies_.shutdown();
    } catch (...) {
        append_shutdown_failure(
            "EFB-copy shutdown failure: " +
            describe_exception(std::current_exception()));
    }
    try {
        renderer_.shutdown();
    } catch (...) {
        append_shutdown_failure(
            "renderer shutdown failure: " +
            describe_exception(std::current_exception()));
    }
    initialized_ = false;
    if (!shutdown_failures.empty()) {
        append_shutdown_failure(
            "PE audit epoch=" + std::to_string(pe_stats.epoch) +
            " issued=" + std::to_string(pe_stats.issued) +
            " resolved=" + std::to_string(pe_stats.resolved) +
            " completed=" + std::to_string(pe_stats.completed) +
            " consumed=" + std::to_string(pe_stats.consumed) +
            " pending=" +
            std::to_string(
                pe_stats.unresolved + pe_stats.submission_ready +
                pe_stats.fence_pending));
        throw std::runtime_error("GX shutdown failed: " + shutdown_failures);
    }
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

void GxBackend::prune_immutable_range_cache(std::size_t incoming_bytes) {
    if (incoming_bytes > kImmutableRangeCacheMaxBytes) {
        throw std::runtime_error(
            "[GxBackend] immutable range exceeds ownership cache budget");
    }
    while (immutable_range_cache_.size() >= kImmutableRangeCacheMaxEntries ||
           immutable_range_cache_bytes_ >
               kImmutableRangeCacheMaxBytes - incoming_bytes) {
        auto victim = immutable_range_cache_.end();
        for (auto it = immutable_range_cache_.begin();
             it != immutable_range_cache_.end();
             ++it) {
            if (it->bytes == nullptr || it->bytes.use_count() != 1u) {
                continue;
            }
            if (victim == immutable_range_cache_.end() ||
                (!it->valid && victim->valid) ||
                (it->valid == victim->valid &&
                 it->last_used < victim->last_used)) {
                victim = it;
            }
        }
        if (victim == immutable_range_cache_.end()) {
            throw std::runtime_error(
                "[GxBackend] immutable range ownership cache exhausted by in-flight frames");
        }
        immutable_range_cache_bytes_ -= victim->bytes->size();
        // Swap-and-pop instead of erase(victim). This loop can drop many entries in one pass, and erase
        // from the middle of a std::vector shifts every later element, so dropping k of n entries costs
        // O(k*n) moves. Swap-and-pop is O(1) per eviction.
        //
        // The victim is still chosen by the scan above, so the eviction order is unchanged. Nothing
        // addresses this cache by position: acquire_immutable_range matches on guest_base/size/
        // source_identity, invalidate_immutable_range_cache walks every entry, and clear() empties it.
        // last_used comes from a monotonic tick, so the tie-break is total and the LRU order does not
        // depend on physical layout.
        *victim = std::move(immutable_range_cache_.back());
        immutable_range_cache_.pop_back();
    }
}

std::shared_ptr<std::vector<std::byte>> GxBackend::acquire_immutable_range(
    std::uint32_t guest_base,
    const std::byte* source,
    std::uint32_t size,
    bool& reused) {
    reused = false;
    if (source == nullptr || size == 0u) {
        throw std::runtime_error(
            "[GxBackend] immutable range ownership received an empty source");
    }
    for (ImmutableRangeCacheEntry& entry : immutable_range_cache_) {
        if (entry.valid && entry.guest_base == guest_base &&
            entry.size == size && entry.source_identity == source &&
            entry.bytes != nullptr) {
            entry.last_used = ++immutable_range_cache_tick_;
            reused = true;
            return entry.bytes;
        }
    }

    prune_immutable_range_cache(size);
    auto bytes = std::make_shared<std::vector<std::byte>>(size);
    std::memcpy(bytes->data(), source, size);
    immutable_range_cache_.push_back(ImmutableRangeCacheEntry{
        guest_base,
        size,
        source,
        bytes,
        ++immutable_range_cache_tick_,
        true,
    });
    immutable_range_cache_bytes_ += size;
    return bytes;
}

void GxBackend::invalidate_immutable_range_cache(
    const std::vector<GuestWriteRange>& ranges) noexcept {
    if (ranges.empty()) {
        return;
    }
    for (ImmutableRangeCacheEntry& entry : immutable_range_cache_) {
        if (!entry.valid) {
            continue;
        }
        // Both production callers pass the same coalesced physical dirty
        // ranges used by dependency invalidation. Normalize the owned alias
        // once rather than checking every entry/write pair.
        if (sorted_dirty_ranges_overlap(
                std::span<const GuestWriteRange>{ranges},
                normalize_dependency_guest_addr(entry.guest_base), entry.size)) {
            entry.valid = false;
        }
    }
}

void GxBackend::copy_memory_snapshot_storage(MemorySnapshot& snapshot) {
    const auto copy_one = [](MemorySnapshotStorage* storage) {
        std::memcpy(storage->bytes.data(), storage->source, storage->size);
        storage->copied = true;
        // Sealed packets own their bytes. Never retain a dormant live alias.
        storage->source = nullptr;
    };
    if (!parallel_memory_snapshot_copy_enabled() || snapshot_copy_worker_count() <= 1u) {
        // Normal serial capture needs no heap-allocated work list or second
        // walk. Preserve storage order and detach each copied live alias.
        for (MemorySnapshotStorage& storage : snapshot.storage) {
            if (storage.used && !storage.copied &&
                storage.source != nullptr && storage.size != 0u) {
                copy_one(&storage);
            }
        }
        return;
    }
    std::vector<MemorySnapshotStorage*> pending;
    pending.reserve(snapshot.storage.size());
    std::uint64_t pending_bytes = 0;
    for (MemorySnapshotStorage& storage : snapshot.storage) {
        if (!storage.used || storage.copied ||
            storage.source == nullptr || storage.size == 0u) {
            continue;
        }
        pending.push_back(&storage);
        pending_bytes += storage.size;
    }

    if (pending.empty()) {
        return;
    }

    const std::size_t worker_count = std::min<std::size_t>(
        snapshot_copy_worker_count(),
        pending.size());
    if (worker_count <= 1u ||
        pending_bytes < snapshot_parallel_copy_threshold()) {
        for (MemorySnapshotStorage* storage : pending) {
            copy_one(storage);
        }
        return;
    }

    std::atomic<std::size_t> next_index{0};
    const auto copy_next = [&] {
        for (;;) {
            const std::size_t index =
                next_index.fetch_add(1u, std::memory_order_relaxed);
            if (index >= pending.size()) {
                break;
            }
            copy_one(pending[index]);
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(worker_count - 1u);
    try {
        for (std::size_t i = 1u; i < worker_count; ++i) {
            workers.emplace_back(copy_next);
        }
        copy_next();
    } catch (...) {
        for (std::thread& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        for (MemorySnapshotStorage* storage : pending) {
            if (!storage->copied) {
                copy_one(storage);
            }
        }
        return;
    }

    for (std::thread& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

std::uint64_t GxBackend::memory_snapshot_used_bytes(
    const MemorySnapshot& snapshot) noexcept {
    if (snapshot.immutable_range_ownership) {
        return snapshot.copied_bytes;
    }
    std::uint64_t bytes = 0;
    for (const MemorySnapshotStorage& storage : snapshot.storage) {
        if (storage.used && storage.copied) {
            bytes += storage.size;
        }
    }
    return bytes;
}

void GxBackend::capture_memory_snapshot(
    GuestMemoryV1* source,
    MemorySnapshot& snapshot,
    const std::vector<GuestMemoryRange>& dependency_ranges,
    bool strict_ranges,
    bool immutable_range_ownership) {
    snapshot.storage_used_count = 0u;
    snapshot.storage_lookup_active = false;
    snapshot.immutable_ranges.clear();
    snapshot.regions_disjoint_sorted = false;
    snapshot.copied_bytes = 0u;
    snapshot.reused_bytes = 0u;
    snapshot.immutable_range_ownership = false;
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
    const std::vector<GuestMemoryRange> configured_ranges =
        render_memory_snapshot_ranges();
    ranges.insert(
        ranges.end(),
        configured_ranges.begin(),
        configured_ranges.end());
    normalize_guest_memory_ranges(ranges);

    snapshot.memory = *source;
    // The render-side view is read-only and self-owned. None of the host
    // callbacks or invalidation maps are valid dependencies for delayed FIFO
    // parsing; leaving them installed would make a nominal snapshot capable
    // of observing or mutating the live simulation again.
    snapshot.memory.user = nullptr;
    snapshot.memory.read_device = nullptr;
    snapshot.memory.write_device = nullptr;
    snapshot.memory.notify_write = nullptr;
    snapshot.memory.flat_guest_read_base = nullptr;
    snapshot.memory.dirty_page_words = nullptr;
    snapshot.memory.dirty_tracked_base = 0u;
    snapshot.memory.dirty_tracked_size = 0u;
    snapshot.memory.dirty_page_shift = 0u;
    snapshot.memory.dirty_page_word_count = 0u;
    snapshot.memory.dirty_word_summary = nullptr;
    snapshot.memory.dirty_word_summary_count = 0u;
    snapshot.memory.dirty_word_summary_pad = 0u;
    snapshot.memory.cpu_dirty_page_words = nullptr;
    snapshot.memory.cpu_dirty_tracked_base = 0u;
    snapshot.memory.cpu_dirty_tracked_size = 0u;
    snapshot.memory.cpu_dirty_page_shift = 0u;
    snapshot.memory.cpu_dirty_page_word_count = 0u;
    snapshot.regions.clear();
    snapshot.regions.reserve(
        source->region_count +
        static_cast<std::uint32_t>(ranges.size()));

    if (immutable_range_ownership && !strict_ranges) {
        throw std::runtime_error(
            "[GxBackend] immutable range ownership requires strict dependency ranges");
    }

    if (strict_ranges) {
        const auto append_snapshot_copy =
            [&](const GuestMemoryRange& range) {
                std::uint64_t cursor = range.guest_base;
                const std::uint64_t range_end = cursor + range.size;
                while (cursor < range_end) {
                    const GuestMemoryRegionV1* source_region = nullptr;
                    std::uint64_t source_region_end = 0;
                    for (std::uint32_t i = 0; i < source->region_count; ++i) {
                        const GuestMemoryRegionV1& region = source->regions[i];
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
                        if (strict_ranges) {
                            throw std::runtime_error(
                                "[GxBackend] compact memory snapshot range is unmapped");
                        }
                        break;
                    }

                    const std::uint64_t copy_end =
                        std::min(range_end, source_region_end);
                    const std::uint32_t copy_size =
                        static_cast<std::uint32_t>(copy_end - cursor);
                    const std::uint32_t source_offset =
                        static_cast<std::uint32_t>(
                            cursor - source_region->guest_base);
                    GuestMemoryRegionV1 copy{};
                    copy.guest_base = static_cast<std::uint32_t>(cursor);
                    copy.size = copy_size;
                    std::byte* source_bytes =
                        source_region->host_base + source_offset;
                    if (immutable_range_ownership) {
                        bool reused = false;
                        auto owned = acquire_immutable_range(
                            copy.guest_base,
                            source_bytes,
                            copy_size,
                            reused);
                        copy.host_base = owned->data();
                        if (reused) {
                            snapshot.reused_bytes += copy_size;
                        } else {
                            snapshot.copied_bytes += copy_size;
                        }
                        snapshot.immutable_ranges.push_back(std::move(owned));
                        snapshot.regions.push_back(copy);
                    } else if (MemorySnapshotStorage* storage =
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

        for (GuestMemoryFastRegionV1& fast : snapshot.memory.fast_regions) {
            fast = {};
        }
        snapshot.memory.region_count =
            static_cast<std::uint32_t>(snapshot.regions.size());
        snapshot.memory.regions = snapshot.regions.data();
        if (!immutable_range_ownership) {
            copy_memory_snapshot_storage(snapshot);
        }
        snapshot.immutable_range_ownership = immutable_range_ownership;
        // Avoid binary-search overhead on tiny views. Strict capture's output
        // is normally sorted/disjoint, but certify actual output rather than
        // assuming it from the dependency list or from the source mappings.
        snapshot.regions_disjoint_sorted = snapshot.regions.size() >= 32u &&
            detail::snapshot_regions_are_disjoint_sorted(snapshot.regions);
        snapshot.sealed = true;
        return;
    }

    for (std::uint32_t i = 0; i < source->region_count; ++i) {
        const GuestMemoryRegionV1& region = source->regions[i];
        GuestMemoryRegionV1 copy = region;
        if (MemorySnapshotStorage* storage =
                snapshot_storage_for(snapshot, region.host_base, region.size);
            storage != nullptr) {
            copy.host_base = storage->bytes.data();
        }
        snapshot.regions.push_back(copy);
    }

    for (GuestMemoryFastRegionV1& fast : snapshot.memory.fast_regions) {
        fast = {};
    }
    for (std::size_t i = 0; i < 16u; ++i) {
        const GuestMemoryFastRegionV1& fast = source->fast_regions[i];
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
    GuestMemoryV1* memory,
    std::vector<GuestMemoryRange>& out,
    bool& cache_hit) {
    out.clear();
    cache_hit = false;
    if (fifo_data == nullptr || fifo_size == 0u) {
        return;
    }

    const std::span<const std::byte> fifo(fifo_data, fifo_size);
    const bool cacheable = dependency_pending_fifo_.empty();
    // This fingerprint is a lookup filter; matching entries still compare the
    // complete FIFO bytes and exact initial state below before restoring state.
    const std::uint64_t fifo_hash =
        cacheable ? dependency_fifo_fingerprint(fifo) : 0u;
    const std::uint64_t state_hash =
        cacheable ? dependency_shape_state_hash(dependency_state_) : 0u;
    const auto shape_words = cacheable ? dependency_state_.dependency_shape_words()
                                      : GxState::DependencyShapeWords{};
    // Whole-FIFO hits restore final parser state. The one-shot mask can alter
    // both writes in this FIFO and a later chunk even with identical banks.
    // Draw-range keys only describe already-decoded state and do not need it.
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
            stat_dependency_cache_hits_.fetch_add(
                1u,
                std::memory_order_relaxed);
            return;
        }
    }

    if (cacheable) {
        stat_dependency_cache_misses_.fetch_add(
            1u,
            std::memory_order_relaxed);
    }
    std::vector<GuestMemoryRange> shape_ranges;
    DependencyRangeSink sink(
        dependency_state_,
        memory,
        out,
        dependency_draw_run_range_cache_,
        dependency_draw_run_range_cache_tick_,
        dependency_draw_run_range_cache_bytes_,
        stat_dependency_draw_run_cache_hits_,
        stat_dependency_draw_run_cache_misses_,
        stat_dependency_draw_run_cache_evictions_);
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
    // CALL_DL bytes are both the dependency shape and the render input. Owning every recursively
    // visited display-list range keeps compact snapshots independent of the parser cache, so cache
    // invalidation and delayed parsing are correct on their own.
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

std::uint64_t GxBackend::xfb_causal_trace_frame_index() const noexcept {
    return causal_trace_frame_index_.load(std::memory_order_relaxed);
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

XfbMonotonicCadenceStats GxBackend::snapshot_monotonic_cadence(
    const MonotonicCadenceAccumulator& accumulator) noexcept {
    const std::uint64_t intervals =
        accumulator.intervals.load(std::memory_order_relaxed);
    const std::uint64_t minimum =
        accumulator.min_interval_ns.load(std::memory_order_relaxed);
    std::uint64_t p99_interval_ns = 0;
    std::uint64_t p95_interval_ns = 0;
    if (accumulator.retained_interval_samples != 0u &&
        !accumulator.interval_samples_overflowed) {
        // The caller quiesces the render thread before taking this snapshot. The copy goes into fixed
        // storage, so repeated snapshots neither change the retained data nor allocate.
        std::array<
            std::uint64_t,
            MonotonicCadenceAccumulator::kMaxRetainedIntervals>
            ordered{};
        std::copy_n(
            accumulator.interval_samples.begin(),
            accumulator.retained_interval_samples,
            ordered.begin());
        const std::size_t nearest_rank =
            ((99u * accumulator.retained_interval_samples + 99u) / 100u) - 1u;
        std::nth_element(
            ordered.begin(),
            ordered.begin() + static_cast<std::ptrdiff_t>(nearest_rank),
            ordered.begin() + static_cast<std::ptrdiff_t>(
                                  accumulator.retained_interval_samples));
        p99_interval_ns = ordered[nearest_rank];
        const std::size_t p95_rank =
            ((95u * accumulator.retained_interval_samples + 99u) / 100u) - 1u;
        std::nth_element(
            ordered.begin(),
            ordered.begin() + static_cast<std::ptrdiff_t>(p95_rank),
            ordered.begin() + static_cast<std::ptrdiff_t>(
                                  accumulator.retained_interval_samples));
        p95_interval_ns = ordered[p95_rank];
    }
    return XfbMonotonicCadenceStats{
        accumulator.samples.load(std::memory_order_relaxed),
        intervals,
        accumulator.total_interval_ns.load(std::memory_order_relaxed),
        intervals == 0u || minimum == ~std::uint64_t{0} ? 0u : minimum,
        accumulator.max_interval_ns.load(std::memory_order_relaxed),
        accumulator.published_first_sample_ns.load(std::memory_order_relaxed),
        accumulator.published_last_sample_ns.load(std::memory_order_relaxed),
        p99_interval_ns,
        static_cast<std::uint64_t>(accumulator.retained_interval_samples),
        accumulator.interval_samples_overflowed,
        p95_interval_ns,
    };
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

XfbMeasurementCadenceStats GxBackend::xfb_measurement_cadence_stats()
    const noexcept {
    return XfbMeasurementCadenceStats{
        snapshot_monotonic_cadence(xfb_copy_production_cadence_),
        snapshot_monotonic_cadence(xfb_first_serial_present_cadence_),
    };
}

void GxBackend::reset_xfb_measurement_window_stats() noexcept {
    // The public contract requires the caller to quiesce the render thread.
    // That makes these render-thread-owned state writes race-free.
    frame_tail_report_ns_=0u;
    xfb_serial_accounting_.current_nonfirst_run = 0;
    stat_xfb_present_duplicate_run_max_.store(0, std::memory_order_relaxed);
    reset_monotonic_cadence(xfb_copy_production_cadence_);
    reset_monotonic_cadence(xfb_first_serial_present_cadence_);
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
            // Percentile staircase, so the shape of the interval distribution is visible rather than
            // summarized.
            //
            // Percentiles are used instead of the full 1 ms histogram. Printing every non-zero bin makes
            // rows too wide, and spike windows would spread the bins so far that the bulk becomes a few
            // counts among many. Each reported percentile is a value inside the bulk.
            //
            // A bulk set by a continuous process spreads smoothly, so the ladder steps evenly. A bulk
            // quantized to display retraces plateaus instead, because most intervals land in one or two
            // 16.67 ms bands and several adjacent ranks report the same bin. What matters is how evenly the
            // ladder steps, not the absolute values.
            //
            // Read it alongside `-intervals=`. At a 1 ms bin width, several cut points can resolve to the
            // same bin simply because the window holds few intervals, which would look like a plateau. A
            // plateau is evidence only in a window with tens of intervals.
            //
            // Gated by GALAXY_MONITOR_FRAME_TAILS like the rest of this row. It adds no per-frame work: the
            // bins are counted once per interval.
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

void GxBackend::install_guest_dirty_tracker(GuestMemoryV1* memory) noexcept {
    if (memory == nullptr) {
        return;
    }
    memory->dirty_page_words = dirty_page_words_.data();
    memory->dirty_tracked_base = kDirtyTrackedBase;
    memory->dirty_tracked_size = kDirtyTrackedSize;
    memory->dirty_page_shift = kDirtyPageShift;
    memory->dirty_page_word_count =
        static_cast<std::uint32_t>(dirty_page_words_.size());
    memory->dirty_word_summary = dirty_word_summary_.data();
    memory->dirty_word_summary_count =
        static_cast<std::uint32_t>(dirty_word_summary_.size());
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
        // The CPU-side callback arrives with absolute addresses and has no
        // tracker geometry to read, so keep the explicit bounds check before
        // handing the page range to the shared bulk publisher.
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

    // Both dirty producers restrict writes to MEM1/MEM2 and their aliases.
    // The physical address hole cannot contain dirty bits. Keep the bitmap's
    // guest-address indexing, but avoid scanning the hole on every submission.
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
        // Visit the bitmap a group of 64 words at a time and read the group's
        // summary word first. Empty groups (the overwhelming majority: the
        // tracked window is 320 MiB, so a group spans 128 KiB of guest RAM)
        // cost one load instead of 64. A group with no summary bit set has
        // held no publication since the previous drain of this group, because
        // producers set the summary bit unconditionally and this loop clears
        // the group's summary bits before exchanging any word of it.
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
            // A summary bit is a hint: producers set it before (or together
            // with) the page bit, and the authoritative answer is the word.
            if (dirty_page_words_[candidate].load(std::memory_order_relaxed) ==
                0u) {
                continue;
            }
            word_index = candidate;
            // An empty word needs no locked exchange. A concurrent producer
            // that marks it after this observation leaves its bit for the next
            // drain, just as it would after an exchange that returned zero.
            // Never clear a nonempty word with a plain store: preserve
            // concurrent additions.
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
    invalidate_dirty_display_list_ranges(dependency_parser_, ranges);

    std::uint64_t invalidated = 0;
    for (DependencyRangeCacheEntry& entry : dependency_range_cache_) {
        if (!entry.valid || entry.shape_ranges.empty()) {
            continue;
        }
        bool overlaps = false;
        // Dirty ranges come from the physical RAM tracker and are coalesced
        // before each caller reaches this method. Their ends are monotonic.
        // Search once per recorded shape rather than normalizing every
        // shape/dirty pair. Keep the same half-open overlap and alias rules.
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
    if (invalidated != 0u) {
        stat_dependency_cache_invalidations_.fetch_add(
            invalidated,
            std::memory_order_relaxed);
    }
}

std::size_t GxBackend::DecodedPacketRunCacheKeyHash::operator()(
    const DecodedPacketRunCacheKey& key) const noexcept {
    std::uint64_t hash = 1469598103934665603ull;
    const auto mix = [&hash](std::uint64_t value) noexcept {
        // Each typed field contributes once. This hash only selects an
        // in-memory bucket; complete key equality remains authoritative.
        hash = (hash ^ value) * 1099511628211ull;
    };
    mix(key.cache_token);
    mix(static_cast<std::uint64_t>(key.packet_run_index));
    mix(static_cast<std::uint8_t>(key.primitive));
    mix(key.vtxfmt);
    mix(key.vcd_lo);
    mix(key.vcd_hi);
    mix(key.vat_a);
    mix(key.vat_b);
    mix(key.vat_c);
    mix(key.matrix_index_a);
    mix(key.matrix_index_b);
    for (const std::uint32_t value : key.array_base_stride) {
        mix(value);
    }
    // Aligned addresses and narrow register fields need their upper bits
    // diffused into the bucket bits after whole-word mixing.
    hash ^= hash >> 30u;
    hash *= 0xBF58476D1CE4E5B9ull;
    hash ^= hash >> 27u;
    hash *= 0x94D049BB133111EBull;
    return static_cast<std::size_t>(hash ^ (hash >> 31u));
}

GxBackend::DecodedPacketRunCacheKey GxBackend::decoded_packet_run_cache_key(
    std::uint64_t cache_token,
    std::size_t packet_run_index,
    PrimitiveClass primitive,
    std::uint8_t vtxfmt,
    const VertexDescriptor& desc) const {
    DecodedPacketRunCacheKey key{};
    key.cache_token = cache_token;
    key.packet_run_index = packet_run_index;
    key.primitive = primitive;
    key.vtxfmt = vtxfmt;
    key.vcd_lo = state_.cp(cp::kVcdLo);
    key.vcd_hi = state_.cp(cp::kVcdHi);
    key.vat_a = state_.cp(static_cast<std::uint8_t>(cp::kVatABase + vtxfmt));
    key.vat_b = state_.cp(static_cast<std::uint8_t>(cp::kVatBBase + vtxfmt));
    key.vat_c = state_.cp(static_cast<std::uint8_t>(cp::kVatCBase + vtxfmt));
    // The decoder reads only the default position index, and only when the
    // packet has no PNMTXIDX. Texture-matrix defaults are consumed by fresh
    // VS constants, not by decoded vertices. Keep them out of this CPU cache.
    key.matrix_index_a = desc.has_pn_matrix_index ? 0u :
        state_.cp(cp::kMatrixIndexA) & 0x3fu;
    for (unsigned attr = 0; attr < 12u; ++attr) {
        const VcdType type = attr == 0u ? desc.position.vcd :
            attr == 1u ? desc.normal.vcd :
            attr < 4u ? desc.color[attr - 2u].vcd : desc.texcoord[attr - 4u].vcd;
        if (type == VcdType::Index8 || type == VcdType::Index16) {
            key.array_base_stride[attr * 2u] = state_.array_base(attr);
            key.array_base_stride[attr * 2u + 1u] = state_.array_stride(attr);
        }
    }
    return key;
}

bool GxBackend::capture_decoded_packet_run_dependencies(
    GuestMemoryV1* memory,
    const std::vector<VertexDecodeGuestRange>& dependencies,
    std::size_t byte_budget,
    DecodedPacketRunCacheEntry::GuestDependencySnapshots& snapshots,
    std::size_t& captured_bytes,
    bool regions_disjoint_sorted) {
    using Snapshots = DecodedPacketRunCacheEntry::GuestDependencySnapshots;
    const auto fail = [&]() {
        snapshots.clear();
        captured_bytes = 0u;
        return false;
    };
    struct Interval {
        std::uint64_t begin = 0;
        std::uint64_t end = 0;
    };
    std::vector<Interval> reads;
    reads.reserve(dependencies.size());
    for (const VertexDecodeGuestRange& dependency : dependencies) {
        if (dependency.size == 0u) {
            continue;
        }
        const std::uint64_t begin = dependency.guest_base;
        reads.push_back(Interval{begin, begin + dependency.size});
    }
    std::sort(reads.begin(), reads.end(),
        [](const Interval& lhs, const Interval& rhs) noexcept {
            return lhs.begin != rhs.begin ? lhs.begin < rhs.begin
                                          : lhs.end < rhs.end;
        });

    // Merge only overlapping or touching reads: the union covers exactly the
    // bytes that were read, never a gap. A merge must also stay inside one
    // host mapping; two adjacent guest regions that are not contiguous on the
    // host are kept as separate ranges.
    const auto resolve = [&](std::uint64_t begin, std::uint64_t end)
        -> const std::byte* {
        if (end - begin >
            static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())) {
            return nullptr;
        }
        return resolve_guest_const_bytes(
            memory, static_cast<std::uint32_t>(begin),
            static_cast<std::uint32_t>(end - begin), regions_disjoint_sorted);
    };
    std::vector<Interval> merged;
    merged.reserve(reads.size());
    for (const Interval& read : reads) {
        if (!merged.empty() && read.begin <= merged.back().end) {
            Interval& current = merged.back();
            const std::uint64_t end = std::max(current.end, read.end);
            if (end == current.end || resolve(current.begin, end) != nullptr) {
                current.end = end;
                continue;
            }
        }
        merged.push_back(read);
    }

    std::size_t total_bytes = 0u;
    for (const Interval& range : merged) {
        const std::uint64_t size = range.end - range.begin;
        if (total_bytes > byte_budget || size > byte_budget - total_bytes) {
            return fail();
        }
        total_bytes += static_cast<std::size_t>(size);
    }
    Snapshots captured;
    captured.ranges.reserve(merged.size());
    captured.bytes.resize(total_bytes);
    std::size_t offset = 0u;
    for (const Interval& range : merged) {
        const std::byte* source = resolve(range.begin, range.end);
        if (source == nullptr) {
            return fail();
        }
        const auto size = static_cast<std::uint32_t>(range.end - range.begin);
        std::memcpy(captured.bytes.data() + offset, source, size);
        captured.ranges.push_back(Snapshots::Range{
            static_cast<std::uint32_t>(range.begin), size, offset});
        offset += size;
    }
    snapshots = std::move(captured);
    captured_bytes = total_bytes;
    return true;
}

bool GxBackend::decoded_packet_run_dependencies_match(
    GuestMemoryV1* memory,
    std::uint64_t entry_generation,
    std::uint64_t current_generation,
    const DecodedPacketRunCacheEntry::GuestDependencySnapshots& snapshots,
    std::uint64_t& new_generation,
    bool regions_disjoint_sorted)
    noexcept {
    new_generation = current_generation;
    // Fast path: this entry was already compared against live guest memory after the latest
    // guest-write batch. Guest writes reach the render thread only through render_frame()'s drained
    // ranges, which are applied (bumping the generation) before the frame's first replay. The render
    // thread sees a frozen snapshot of guest memory for the rest of the frame, so a key replayed
    // thousands of times per frame pays the snapshot compare once. Any write that could change these
    // bytes bumps the generation and forces the full compare.
    if (entry_generation != 0u && entry_generation == current_generation &&
        !snapshots.empty()) {
        return true;
    }
    const std::size_t stored_bytes = snapshots.bytes.size();
    for (const auto& range : snapshots.ranges) {
        if (range.size == 0u) {
            continue;
        }
        if (range.offset > stored_bytes ||
            range.size > stored_bytes - range.offset) {
            return false;
        }
        const std::byte* source = resolve_guest_const_bytes(
            memory, range.guest_base, range.size, regions_disjoint_sorted);
        if (source == nullptr ||
            std::memcmp(source, snapshots.bytes.data() + range.offset,
                        range.size) != 0) {
            return false;
        }
    }
    return true;
}

bool GxBackend::ensure_resident_vertex_buffer() {
    ID3D12Device* const device = renderer_.device();
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
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = static_cast<UINT64>(kResidentDecodedVertexCapacity) *
        sizeof(GxVertexOut);
    rd.Height = 1;
    rd.DepthOrArraySize = 1;
    rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
    void* mapped = nullptr;
    const D3D12_RANGE no_read{0, 0};
    if (FAILED(device->CreateCommittedResource(
            &hp, D3D12_HEAP_FLAG_NONE, &rd,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(&buffer))) ||
        FAILED(buffer->Map(0, &no_read, &mapped)) || mapped == nullptr) {
        // Not fatal: every hit keeps using the per-frame ring upload.
        resident_vertex_buffer_failed_ = true;
        std::cerr << "[gx-resident-vertices] buffer creation failed; "
                     "decoded vertices stay on the frame ring\n";
        return false;
    }
    buffer->SetName(L"GX resident decoded vertices");
    resident_vertex_buffer_ = std::move(buffer);
    resident_vertex_mapped_ = static_cast<std::byte*>(mapped);
    resident_vertex_gpu_ = resident_vertex_buffer_->GetGPUVirtualAddress();
    resident_vertices_.reset(kResidentDecodedVertexCapacity, kFramesInFlight);
    renderer_.set_resident_vertices(resident_vertex_gpu_, resident_vertex_buffer_->GetDesc().Width);
    return true;
}

void GxBackend::release_resident_vertex_buffer() noexcept {
    // Callers guarantee the GPU no longer reads the buffer (session reset
    // before renderer initialization, or shutdown after drain_gpu()). The
    // arena reset bumps its epoch, so every entry's block becomes invalid.
    resident_vertex_buffer_.Reset();
    resident_vertex_mapped_ = nullptr;
    resident_vertex_gpu_ = 0;
    renderer_.set_resident_vertices(0u, 0u);
    resident_vertex_device_ = nullptr;
    resident_vertex_buffer_failed_ = false;
    resident_vertices_.reset(0u, kFramesInFlight);
}

void GxBackend::release_decoded_entry_residency(
    DecodedPacketRunCacheEntry& entry) noexcept {
    if (entry.resident_epoch != 0u &&
        entry.resident_epoch == resident_vertices_.epoch()) {
        resident_vertices_.release(entry.resident_offset, entry.resident_units);
        if (resident_hold_segment_ != ~0u) [[unlikely]] {
            ++interp_held_vertex_blocks_;
        }
    }
    entry.resident_epoch = 0u;
    entry.resident_offset = 0u;
    entry.resident_units = 0u;
}

bool GxBackend::make_decoded_entry_resident(DecodedPacketRunCacheEntry& entry) {
    if (!resident_decoded_vertices_enabled() || entry.vertices.empty() ||
        entry.vertices.size() >
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return false;
    }
    if (!ensure_resident_vertex_buffer()) {
        return false;
    }
    const auto units = static_cast<std::uint32_t>(entry.vertices.size());
    if (entry.resident_epoch == resident_vertices_.epoch() &&
        entry.resident_units == units) {
        return true;
    }
    entry.resident_epoch = 0u;
    const std::optional<std::uint32_t> offset = resident_vertices_.allocate(units);
    if (!offset.has_value()) {
        return false;
    }
    // Written once; the block is never rewritten while it belongs to this
    // entry, so no in-flight frame can observe a partial update.
    std::memcpy(
        resident_vertex_mapped_ +
            static_cast<std::size_t>(*offset) * sizeof(GxVertexOut),
        entry.vertices.data(),
        static_cast<std::size_t>(units) * sizeof(GxVertexOut));
    entry.resident_epoch = resident_vertices_.epoch();
    entry.resident_offset = *offset;
    entry.resident_units = units;
    return true;
}

void GxBackend::prune_decoded_packet_run_cache() {
    if (decoded_packet_run_cache_bytes_ <= kDecodedPacketRunCacheMaxBytes ||
        decoded_packet_run_cache_.empty()) {
        return;
    }

    struct PruneCandidate {
        std::uint64_t last_used = 0;
        DecodedPacketRunCacheKey key{};
    };

    std::vector<PruneCandidate> candidates;
    candidates.reserve(decoded_packet_run_cache_.size());
    for (const auto& [key, entry] : decoded_packet_run_cache_) {
        candidates.push_back(PruneCandidate{entry.last_used, key});
    }
    std::sort(
        candidates.begin(),
        candidates.end(),
        [](const PruneCandidate& lhs, const PruneCandidate& rhs) noexcept {
            return lhs.last_used < rhs.last_used;
        });

    for (const PruneCandidate& candidate : candidates) {
        if (decoded_packet_run_cache_bytes_ <= kDecodedPacketRunCacheMaxBytes) {
            break;
        }
        const auto it = decoded_packet_run_cache_.find(candidate.key);
        if (it == decoded_packet_run_cache_.end()) {
            continue;
        }
        decoded_packet_run_cache_bytes_ -= it->second.byte_size;
        release_decoded_entry_residency(it->second);
        decoded_packet_run_cache_.erase(it);
        ++frame_decoded_vertex_cache_evictions_;
    }
}

std::size_t GxBackend::invalidate_dirty_decoded_packet_run_ranges(
    const std::vector<GuestWriteRange>& ranges) noexcept {
    // Every batch of guest-write notifications must invalidate the per-entry snapshot validation,
    // including an empty batch. The generation assert below means "no write has been observed since
    // this entry was compared", and only this function can observe writes. Bumping unconditionally
    // keeps that true without depending on which caller passed a range list. The increment is one
    // add per frame.
    ++decoded_packet_run_generation_;
    if (decoded_packet_run_generation_ == 0u) {
        // Wrapped. An older, unused entry can still hold generation 1;
        // renumbering alone would turn it into a false already-validated hit.
        // The rare wrap must reset every validation stamp before reusing 1.
        for (auto& [key, entry] : decoded_packet_run_cache_) {
            entry.validated_generation = 0u;
        }
        decoded_packet_run_generation_ = 1u;
    }
    if (ranges.empty() || decoded_packet_run_cache_.empty()) {
        return 0;
    }

    if (lazy_vertex_invalidation_enabled()) {
        // Admission requires a complete immutable snapshot of every indirect vertex-array read. A new
        // generation forces the first real reuse to compare those bytes against this chunk's frozen
        // memory. A mismatch takes the original decode and upload path with a fresh upload token. Keep
        // the generation bump, including the wrap reset, even for empty batches: a notification never
        // permits skipping byte validation. Byte-budget pruning still bounds retained cold entries.
        return 0;
    }

    std::size_t invalidated = 0;
    const bool use_sorted_ranges = sorted_vertex_invalidation_enabled();
    const bool page_filter = vertex_dependency_page_filter_enabled();
    std::uint64_t dirty_page_mask = 0u;
    if (page_filter) {
        for (const GuestWriteRange& dirty : ranges) {
            dirty_page_mask |= dependency_page_mask(
                normalize_dependency_guest_addr(dirty.guest_addr), dirty.size);
            if (dirty_page_mask == ~std::uint64_t{0}) break;
        }
    }
    for (auto it = decoded_packet_run_cache_.begin();
         it != decoded_packet_run_cache_.end();) {
        if (page_filter && (it->second.guest_array_page_mask & dirty_page_mask) == 0u) {
            ++it;
            continue;
        }
        bool overlaps = false;
        if (use_sorted_ranges) {
            for (const VertexDecodeGuestRange& dependency :
                 it->second.guest_array_reads) {
                if (sorted_dirty_ranges_overlap(
                        std::span<const GuestWriteRange>{ranges},
                        normalize_dependency_guest_addr(dependency.guest_base),
                        dependency.size)) {
                    overlaps = true;
                    break;
                }
            }
        } else {
            for (const GuestWriteRange& dirty : ranges) {
                for (const VertexDecodeGuestRange& dependency :
                     it->second.guest_array_reads) {
                    if (dependency_ranges_overlap(
                            dirty.guest_addr, dirty.size,
                            dependency.guest_base, dependency.size)) {
                        overlaps = true;
                        break;
                    }
                }
                if (overlaps) break;
            }
        }
        if (overlaps) {
            decoded_packet_run_cache_bytes_ -= it->second.byte_size;
            release_decoded_entry_residency(it->second);
            it = decoded_packet_run_cache_.erase(it);
            ++invalidated;
        } else {
            ++it;
        }
    }
    return invalidated;
}

// Per-frame rendering

void GxBackend::scan_pe_events_on_sim_thread(
    const std::byte* fifo_data,
    std::size_t fifo_size,
    GuestMemoryV1* memory,
    const NativeServicesV1* services) {
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

// Producer (sim thread): snapshot the frame and hand it to the render thread. Blocks only when
// the consumer is a full frame behind.
FramePeCompletionToken GxBackend::render_frame(
    const std::byte* fifo_data,
    std::size_t fifo_size,
    GuestMemoryV1* memory,
    const NativeServicesV1* services,
    bool present_swap_chain,
    std::uint32_t displayed_xfb_addr)
{
    if (!initialized_) {
        return {};
    }

    if (!async_render_thread_enabled()) {
        FrameChunk chunk;
        chunk.pointer_response=producer_pointer_response_;
        take_attached_pointer_depth_capture(chunk, fifo_size);
        take_attached_fifo_markers(chunk, fifo_size);
        take_attached_stripe_particles(chunk, fifo_size);
        chunk.prerecorded_movie=producer_movie_content_;
        chunk.safety_surround=producer_safety_surround_;
        chunk.services = services;
        chunk.memory = memory;
        chunk.present_swap_chain = present_swap_chain;
        chunk.displayed_xfb_addr = displayed_xfb_addr;
        if (fifo_data != nullptr && fifo_size > 0) {
            chunk.fifo.assign(fifo_data, fifo_data + fifo_size);
            std::lock_guard<std::mutex> lock(queue_mutex_);
            chunk.pe_completion_token = frame_pe_completions_.issue();
            chunk.fifo_effects_pending = true;
            chunk.pe_completion_requires_resolution = true;
            if (cadence_diagnostics_ != nullptr) {
                cadence_diagnostics_->record_token_issued(
                    chunk.pe_completion_token.kind);
            }
        }
        drain_guest_memory_writes(chunk.dirty_ranges);
        coalesce_dirty_ranges(chunk.dirty_ranges);
        invalidate_dirty_display_list_ranges(event_parser_, chunk.dirty_ranges);
        render_frame_on_thread(chunk);
        return chunk.pe_completion_token;
    }

    const bool trace_gx_stats = trace_gx_stats_enabled();
    const bool trace_gx_stalls = trace_gx_stalls_enabled();
    const bool trace_gx_microprofile = trace_gx_microprofile_enabled();
    // Producer and renderer use the same diagnostic sampling interval. Read
    // only the atomically published completed-frame ordinal here: frame_index_
    // belongs to the renderer. Queued chunks can make their sampled intervals
    // differ; the ordinal supplies no guest-memory ownership or timing guarantee.
    const bool producer_time_detail =
        trace_gx_stats || trace_gx_stalls ||
        frame_gx_timing_sample_enabled(
            timing_frame_index_.load(std::memory_order_relaxed));
    const bool fifo_empty = fifo_data == nullptr || fifo_size == 0u;
    const bool worker_snapshot = worker_memory_snapshot_enabled();
    if (worker_snapshot && !fifo_empty && memory == nullptr) {
        throw std::logic_error(
            "[GxBackend] worker memory snapshot has no guest memory owner");
    }
    std::vector<GuestWriteRange>& drained_dirty_ranges = drain_scratch_ranges_;
    drain_guest_memory_writes(drained_dirty_ranges);
    coalesce_dirty_ranges(drained_dirty_ranges);
    if (fifo_empty) {
        if (!drained_dirty_ranges.empty()) {
            if (!worker_snapshot) {
                invalidate_dependency_range_cache(drained_dirty_ranges);
            }
            invalidate_dirty_display_list_ranges(
                event_parser_,
                drained_dirty_ranges);
            if (display_list_dirty_invalidate_enabled() && !worker_snapshot) {
                for (const GuestWriteRange& range : drained_dirty_ranges) {
                    (void)pe_event_classifier_
                        .invalidate_display_list_cache_range(
                            range.guest_addr,
                            range.size);
                }
            }
            if (!worker_snapshot) {
                invalidate_dirty_display_list_ranges(
                    dependency_parser_,
                    drained_dirty_ranges);
            }
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
    // The start sample must be taken on exactly the frames that consume it: `producer_time_detail`,
    // not the narrower `trace_gx_stats || trace_gx_stalls`. The wait is measured under
    // `producer_time_detail`, which includes `frame_gx_timing_sample_enabled`. Reading the start only
    // on the narrower condition left sampled frames with a default-constructed time_point, so
    // steady_clock::now() - time_point{} produced an epoch-sized duration in producer_wait_ns. The
    // field measures the simulation thread's coupling to the renderer (queue_space_cv_.wait,
    // GALAXY_RENDER_QUEUE_DEPTH). Gating both sides on the same predicate costs one clock read per
    // sampled or traced frame and nothing on an ordinary frame.
    const auto wait_start = producer_time_detail
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    FrameChunk chunk;
    chunk.pointer_response=producer_pointer_response_;
    take_attached_pointer_depth_capture(chunk, fifo_size);
    take_attached_fifo_markers(chunk, fifo_size);
    take_attached_stripe_particles(chunk, fifo_size);
    chunk.prerecorded_movie=producer_movie_content_;
    chunk.safety_surround=producer_safety_surround_;
    chunk.services = services;
    chunk.memory   = memory;
    chunk.present_swap_chain = present_swap_chain;
    chunk.displayed_xfb_addr = displayed_xfb_addr;
    chunk.worker_memory_snapshot = worker_snapshot;
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
    const bool immutable_range_ownership =
        compact_snapshot && immutable_range_ownership_enabled();
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
                ++stalled_queue_admissions_;
                stalled_queue_max_depth_ = std::max(stalled_queue_max_depth_, queued + 1u);
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
            // Hard-fail: surface the render-thread failure on the sim thread
            // instead of presenting a frozen display forever.
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
        if ((snapshot_memory || worker_snapshot) &&
            !memory_snapshot_pool_.empty()) {
            chunk.memory_snapshot = std::move(memory_snapshot_pool_.back());
            memory_snapshot_pool_.pop_back();
        }
        if ((compact_snapshot || worker_snapshot) &&
            !memory_range_pool_.empty()) {
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
    // In worker-snapshot mode these caches and parser belong exclusively to
    // the worker. Dirty ranges travel with the FIFO, including empty-chunk
    // writes, and are applied before its dependency prepass.
    if (!worker_snapshot) {
        invalidate_dependency_range_cache(chunk.dirty_ranges);
        invalidate_immutable_range_cache(chunk.dirty_ranges);
    }
    invalidate_dirty_display_list_ranges(event_parser_, drained_dirty_ranges);
    // invalidate_dependency_range_cache above invalidates the CPU-owned dependency parser. Worker
    // mode does so in its ordered prepass.
    if (timing.pe_events_gpu_fence) {
        if (display_list_dirty_invalidate_enabled() && !worker_snapshot) {
            for (const GuestWriteRange& range : chunk.dirty_ranges) {
                (void)pe_event_classifier_
                    .invalidate_display_list_cache_range(
                        range.guest_addr,
                        range.size);
            }
        }
    }
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
            chunk.memory_snapshot_strict,
            immutable_range_ownership);
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
        chunk.snapshot_reused_bytes = chunk.memory_snapshot.reused_bytes;
        if (trace_gx_stats) {
            stat_dependency_scan_ns_.fetch_add(
                chunk.dependency_scan_ns,
                std::memory_order_relaxed);
            stat_snapshot_ns_.fetch_add(
                chunk.snapshot_ns,
                std::memory_order_relaxed);
            stat_snapshot_bytes_.fetch_add(
                chunk.snapshot_bytes,
                std::memory_order_relaxed);
            stat_snapshot_ranges_.fetch_add(
                static_cast<std::uint64_t>(chunk.snapshot_range_count),
                std::memory_order_relaxed);
        }
        chunk.memory = &chunk.memory_snapshot.memory;
        chunk.memory_snapshot_active = true;
        chunk.immutable_range_ownership = immutable_range_ownership;
    }
    if (timing.pe_events_gpu_fence && !worker_snapshot && pe_preclassify_enabled()) {
        // Classify the immutable FIFO copy and the exact memory view the render parser will consume. In
        // snapshot mode, classifying live guest memory before capture let a concurrent native DMA write
        // make CALL_DL/LOAD_INDX command boundaries disagree after an event-free receipt was issued.
        const auto classification_start =
            (producer_time_detail || trace_gx_microprofile)
                ? std::chrono::steady_clock::now()
                : std::chrono::steady_clock::time_point{};
        pe_event_classifier_.classify(
            chunk.fifo.data(),
            chunk.fifo.size(),
            chunk.memory_snapshot_active ? chunk.memory : memory,
            chunk.preclassified_pe_events,
            chunk.memory_snapshot_active && chunk.memory_snapshot.sealed &&
                chunk.memory == &chunk.memory_snapshot.memory &&
                chunk.memory_snapshot.regions_disjoint_sorted);
        if (producer_time_detail || trace_gx_microprofile) {
            chunk.pe_classification_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - classification_start)
                    .count());
        }
        chunk.pe_events_preclassified = true;
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
                      << " reused-bytes=" << chunk.snapshot_reused_bytes
                      << " ranges=" << chunk.snapshot_range_count
                      << " range-bytes=" << chunk.snapshot_range_bytes
                      << " strict=" << (chunk.memory_snapshot_strict ? 1 : 0)
                      << '\n';
        }
    }

    chunk.producer_wait_ns = producer_wait_ns;
    const auto worker_submission = worker_snapshot
        ? std::make_shared<FramePeCompletionToken>() : nullptr;
    chunk.worker_submission_token = worker_submission;
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
                chunk.memory_snapshot.immutable_ranges.clear();
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
        const bool event_free_receipt =
            chunk.pe_events_preclassified &&
            chunk.preclassified_pe_events.empty();
        chunk.pe_event_free_receipt = event_free_receipt;
        if (!worker_snapshot) {
            submitted_token = event_free_receipt
                ? frame_pe_completions_.issue_event_free()
                : frame_pe_completions_.issue();
        }
        chunk.pe_completion_token = submitted_token;
        chunk.pe_completion_requires_resolution =
            !worker_snapshot && !event_free_receipt;
        frame_queue_.push_back(std::move(chunk));
        queue_depth_after_push = frame_queue_.size();
    }
    if (trace_gx_stats) {
        stat_queue_wait_ns_.fetch_add(
            producer_wait_ns,
            std::memory_order_relaxed);
    }
    if (cadence_diagnostics_ != nullptr) {
        cadence_diagnostics_->record_phase(
            cadence::TimingPhase::ProducerQueueWait,
            diagnostic_queue_wait_ns);
        if (!worker_snapshot) {
            cadence_diagnostics_->record_token_issued(submitted_token.kind);
        }
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
    if (worker_submission) {
        // The live-read wait acquires the mutex used by the worker to publish
        // this token, so the producer sees it only after a successful seal.
        submitted_token = *worker_submission;
        if (!submitted_token) {
            throw std::logic_error(
                "worker memory snapshot released without a classified receipt");
        }
        if (cadence_diagnostics_ != nullptr) {
            cadence_diagnostics_->record_token_issued(submitted_token.kind);
        }
    }
    galaxy::timeline::record(galaxy::timeline::Event::SubmitEnd, submitted_token.value,
                             static_cast<std::uint64_t>(submitted_token.kind));
    return submitted_token;
}

void GxBackend::wait_for_render_idle() {
    if (!initialized_) {
        return;
    }

    if (!async_render_thread_enabled()) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (render_thread_error_ != nullptr) {
            std::exception_ptr error = render_thread_error_;
            render_thread_error_ = nullptr;
            std::rethrow_exception(error);
        }
        return;
    }

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

    if (!async_render_thread_enabled()) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (render_thread_error_ != nullptr) {
            std::exception_ptr error = render_thread_error_;
            render_thread_error_ = nullptr;
            std::rethrow_exception(error);
        }
        return;
    }

    const GxTimingConfig& timing = get_gx_timing_config();
    // When the renderer consumes the producer's sealed snapshot it does not read live guest RAM.
    // render_frame() points the chunk at its own copy before publishing it, and the render thread
    // reads only `chunk.memory_snapshot_active ? &chunk.memory_snapshot.memory : chunk.memory` (see
    // render_frame_on_thread). Every dependency range the request touches is in that sealed copy, so
    // a producer write to live RAM cannot reach a queued chunk.
    //
    // Draining the whole queue in that case is not a barrier. It only serializes: the producer waits
    // for the renderer to finish this frame before starting the next, so the frame rate becomes
    // producer + renderer rather than max(producer, renderer). The 3-deep queue exists to decouple
    // them.
    //
    // The bounded queue-depth wait below uses the same predicate as render_frame()'s push, so the
    // renderer is never more than render_queue_depth frames behind. render_cpu_reads_in_flight_ is
    // still decremented by mark_frame_cpu_reads_done() on every completion, so shutdown conservation
    // and wait_for_render_fifo_effects_idle() are unaffected.
    //
    // Live-memory mode is unchanged: there `chunk.memory` is live guest RAM, and the full drain is a
    // real correctness requirement.
    const std::size_t queue_depth_limit = timing.render_queue_depth;
    const bool renderer_owns_its_memory =
        timing.render_memory_snapshot || timing.render_live_memory_reads;

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
            // The queue-depth gate alone. `render_cpu_reads_in_flight_` must not
            // be required here: it is decremented by mark_frame_cpu_reads_done(),
            // which the render thread may reach only after popping the chunk,
            // while this call happens after a push that already filled the queue
            // to the limit. Waiting for both would deadlock the producer against
            // the very backlog it is waiting to shrink. Depth is also the only
            // condition that has to hold for the next push to succeed.
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

void GxBackend::wait_for_render_fifo_effects_idle() {
    if (!initialized_) {
        return;
    }

    if (!async_render_thread_enabled()) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (render_thread_error_ != nullptr) {
            std::exception_ptr error = render_thread_error_;
            render_thread_error_ = nullptr;
            std::rethrow_exception(error);
        }
        return;
    }

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
    queue_space_cv_.wait(lock, [this, &queued_render_chunks] {
        return stop_render_thread_ ||
               render_thread_error_ != nullptr ||
               (queued_render_chunks() == 0u &&
                render_fifo_effects_in_flight_ == 0u);
    });
    if (render_thread_error_ != nullptr) {
        std::exception_ptr error = render_thread_error_;
        render_thread_error_ = nullptr;
        std::rethrow_exception(error);
    }
}

void GxBackend::wait_for_frame_pe_completion(
    FramePeCompletionToken token) {
    if (wait_for_frame_pe_completion_for(
            token, kFramePeCompletionTimeoutMs)) {
        return;
    }
    // Close the timeout-boundary race: completion may publish between the
    // timed wait's final probe and this diagnostic path.
    if (wait_for_frame_pe_completion_for(token, 0u)) {
        return;
    }
    std::lock_guard<std::mutex> lock(queue_mutex_);
    const FramePeCompletionStats stats = frame_pe_completions_.stats();
    throw std::runtime_error(
        "frame-specific PE completion wait timed out: token=" +
        std::to_string(token.epoch) + ":" +
        std::to_string(token.value) +
        " issued=" + std::to_string(stats.issued) +
        " resolved=" + std::to_string(stats.resolved) +
        " completed=" + std::to_string(stats.completed) +
        " consumed=" + std::to_string(stats.consumed) +
        " unresolved=" + std::to_string(stats.unresolved) +
        " submission-ready=" +
        std::to_string(stats.submission_ready) +
        " fence-pending=" + std::to_string(stats.fence_pending));
}

bool GxBackend::wait_for_frame_pe_completion_for(
    FramePeCompletionToken token,
    std::uint32_t timeout_ms) {
    if (!initialized_) {
        throw std::logic_error(
            "frame-specific PE completion wait used an uninitialized GX backend");
    }

    const bool asynchronous = async_render_thread_enabled();
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeout_ms);
    for (;;) {
        // The synchronous renderer has no consumer thread to poll native fence
        // progress. Polling is nonblocking; PE callbacks are still delivered
        // before the contract frontier advances.
        if (!asynchronous) {
            poll_completed_frame_effects();
        }

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
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
            if (!asynchronous) {
                const auto pending = std::find_if(
                    pending_frame_effects_.begin(),
                    pending_frame_effects_.end(),
                    [token](const PendingFrameEffects& effects) {
                        return effects.pe_completion_token == token;
                    });
                if (pending == pending_frame_effects_.end()) {
                    throw std::logic_error(
                        "synchronous frame PE token has no exact GPU fence");
                }
            }
            if (stop_render_thread_) {
                throw std::runtime_error(
                    "render thread stopped before frame-specific PE completion");
            }
            if (timeout_ms == 0u ||
                std::chrono::steady_clock::now() >= deadline) {
                return false;
            }
            if (asynchronous) {
                (void)queue_space_cv_.wait_until(lock, deadline);
                continue;
            }
        }

        // In synchronous mode no condition-variable publisher exists. Sleep a
        // maximum of one millisecond, then poll the exact fence again. This is
        // still bounded by the caller's absolute slice deadline.
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return false;
        }
        const auto one_millisecond =
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::milliseconds(1));
        std::this_thread::sleep_for(
            std::min(deadline - now, one_millisecond));
    }
}

bool GxBackend::render_queue_has_space() {
    if (!initialized_ || !async_render_thread_enabled()) {
        return false;
    }
    const std::size_t depth = get_gx_timing_config().render_queue_depth;
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (stop_render_thread_ || render_thread_error_ != nullptr) {
        return false;
    }
    const auto queued = static_cast<std::size_t>(std::count_if(
        frame_queue_.begin(), frame_queue_.end(),
        [](const FrameChunk& queued_chunk) { return !queued_chunk.present_only; }));
    return queued < depth;
}

void GxBackend::attach_pointer_depth_capture(
    const std::shared_ptr<PointerDepthCapture>& capture, std::size_t fifo_offset) {
    if (!initialized_ || !capture) return;
    if (!capture->complete.exchange(false, std::memory_order_acq_rel))
        throw std::logic_error("pointer depth capture reused while owned by render worker");
    capture->success = false; capture->error = nullptr; capture->worker_ns = 0u;
    capture->rgba = nullptr;
    if (producer_pointer_depth_capture_) {
        // Two boundaries without a submission between them: the older request
        // can no longer be placed. Release it unsuccessful.
        producer_pointer_depth_capture_->publish_complete();
    }
    producer_pointer_depth_capture_ = capture;
    producer_pointer_depth_capture_offset_ = fifo_offset;
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
    const D3D12_GPU_VIRTUAL_ADDRESS ring_base = renderer_.matrix_ring().gpu_base();
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
        (static_cast<std::uint64_t>(state_.cp(cp::kArrayBaseBase)) << 32u) ^
            (static_cast<std::uint64_t>(state_.cp(cp::kVcdLo)) << 16u) ^
            state_.cp(cp::kVcdHi));
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
    scene_camera_epoch_known_ = false;
    scene_image_effect_record_ = -1;
    scene_image_effect_offset_ = -1;
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

void GxBackend::on_host_marker(std::uint8_t kind, std::uint16_t aux, std::uint32_t a,
                               std::uint32_t b, std::size_t offset) {
    if (kind < identity_markers_by_kind_.size()) {
        ++identity_markers_by_kind_[kind];
    }
    if (kind >= motion::kMarkerCategory) {
        on_motion_marker(kind, aux, a, b, offset);
        return;
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
    } else if (motion_trace_signals()) [[unlikely]] {
        // Shape packets, NameObjs and emitters carry their object's epoch
        // above the pass bit (generation 20): a change is a new identity.
        const auto epoch = static_cast<std::uint16_t>(aux >> 1u);
        const auto [it, inserted] = trace_object_epochs_.try_emplace(a, epoch);
        if (!inserted && it->second != epoch) {
            std::fprintf(stderr, "[motion-identity] new-life kind=%u object=%08X epoch=%u->%u scene=%llu\n",
                         static_cast<unsigned>(kind), a, static_cast<unsigned>(it->second),
                         static_cast<unsigned>(epoch), static_cast<unsigned long long>(identity_scene_index_));
            it->second = epoch;
        }
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

void GxBackend::on_motion_marker(std::uint8_t kind, std::uint16_t aux, std::uint32_t a, std::uint32_t b,
                                 std::size_t offset) {
    switch (kind) {
    case motion::kMarkerCategory: {
        ++motion_category_markers_;
        if (aux == motion::kCategoryPhase) {
            note_rt_phase(static_cast<std::uint8_t>(a));
        }
        if (scene_2d_open_) {
            ++scene_2d_categories_;
        }
        if (!history_scene_) {
            return;
        }
        std::uint8_t phase = motion::category_phase(identity_category_);
        if (aux == motion::kCategoryPhase) {
            phase = static_cast<std::uint8_t>(a);
            if (phase == static_cast<std::uint8_t>(motion::ScenePhase::ImageEffect)) {
                ++motion_image_effect_markers_;
                scene_image_effect_record_ = static_cast<std::int64_t>(transform_history_.current().size());
                scene_image_effect_offset_ =
                    static_cast<std::int64_t>(parse_span_base_ + offset) - static_cast<std::int64_t>(parse_span_prefix_);
                if (motion_trace_signals()) [[unlikely]] {
                    std::fprintf(stderr, "[gx-phase] image-effect scene=%llu record=%lld chunk-offset=%lld\n",
                                 static_cast<unsigned long long>(identity_scene_index_),
                                 static_cast<long long>(scene_image_effect_record_),
                                 static_cast<long long>(scene_image_effect_offset_));
                }
            }
        }
        identity_category_ = motion::encode_category(phase, aux, a);
        return;
    }
    case motion::kMarkerScene:
        ++motion_scene_markers_;
        if (!history_scene_) {
            return;
        }
        scene_camera_epoch_ = a;
        scene_camera_epoch_known_ = true;
        // The game cut the view: the temporal upscaler's history is invalid.
        if (temporal_scene_ && (aux & motion::kSceneCameraCut) != 0u) {
            renderer_.reset_temporal_history();
        }
        if (interp_recording_) {
            InterpScene& scene = interp_scenes_[interp_scene_];
            scene.camera_epoch = a;
            scene.camera_epoch_known = true;
        }
        if (motion_trace_signals() && (aux & motion::kSceneCameraCut) != 0u) [[unlikely]] {
            std::fprintf(stderr, "[gx-scene] camera-cut scene=%llu epoch=%u object-events=%u\n",
                         static_cast<unsigned long long>(identity_scene_index_), a, b);
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
        if (motion_trace_signals()) [[unlikely]] {
            static std::uint64_t lines = 0u;
            if (lines++ < 200u) {
                std::fprintf(stderr, "[gx-pass] %s scene=%llu history-scene=%d\n",
                             aux == static_cast<std::uint16_t>(motion::PassMarker::MirrorBegin) ? "mirror-begin"
                                                                                                 : "mirror-end",
                             static_cast<unsigned long long>(identity_scene_index_), history_scene_ ? 1 : 0);
            }
        }
        return;
    case motion::kMarkerLight: {
        ++motion_light_markers_;
        const ActorLightLoad load = actor_light_load(aux, b);
        rt_follow_lights_ = static_cast<std::uint8_t>((rt_follow_lights_ & ~load.written) | load.follow);
        if (rt_light_aim_.valid) {
            rt_follow_pending_ = static_cast<std::uint8_t>((rt_follow_pending_ & ~load.written) | load.follow);
        }
        return;
    }
    case motion::kMarkerLayoutPane:
    case motion::kMarkerLayoutActor:
    case motion::kMarkerStarPointer: {
        const std::size_t index = static_cast<std::size_t>(kind - motion::kMarkerLayoutPane);
        if (scene_2d_open_) {
            ++scene_2d_layout_markers_[index];
            ++layout_markers_in_2d_[index];
        } else {
            ++layout_markers_outside_2d_[index];
        }
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

// GameScene::draw2D's entry and epilogue (FifoMarker::Scene2DBegin/End): the
// 2D layer's FIFO span in the chunk. Recording only.
void GxBackend::on_scene_2d_marker(bool begin, std::size_t chunk_offset) {
    if (begin && scene_phase_) {
        // GameScene::draw3D returned before its clearZBuffer (movie, nerve):
        // the scene ends here.
        on_fifo_marker(FifoMarker::SceneEnd, nullptr);
    }
    if (begin) {
        scene_2d_open_ = true;
        ++scene_2d_serial_;
        ++window_2d_scenes_;
        scene_2d_begin_offset_ = static_cast<std::int64_t>(chunk_offset);
        scene_2d_end_offset_ = -1;
        scene_2d_layout_markers_ = {};
        scene_2d_categories_ = 0u;
        return;
    }
    scene_2d_open_ = false;
    scene_2d_end_offset_ = static_cast<std::int64_t>(chunk_offset);
    if (motion_trace_signals() && (scene_2d_serial_ <= 30u || scene_2d_serial_ % 600u == 0u)) [[unlikely]] {
        std::fprintf(stderr,
                     "[gx-2d] scene=%llu begin=%lld end=%lld panes=%u layout-actors=%u star-pointer=%u "
                     "categories=%u total-in-2d(pane/actor/star)=%llu/%llu/%llu outside-2d=%llu/%llu/%llu\n",
                     static_cast<unsigned long long>(scene_2d_serial_),
                     static_cast<long long>(scene_2d_begin_offset_), static_cast<long long>(scene_2d_end_offset_),
                     scene_2d_layout_markers_[0], scene_2d_layout_markers_[1], scene_2d_layout_markers_[2],
                     scene_2d_categories_, static_cast<unsigned long long>(layout_markers_in_2d_[0]),
                     static_cast<unsigned long long>(layout_markers_in_2d_[1]),
                     static_cast<unsigned long long>(layout_markers_in_2d_[2]),
                     static_cast<unsigned long long>(layout_markers_outside_2d_[0]),
                     static_cast<unsigned long long>(layout_markers_outside_2d_[1]),
                     static_cast<unsigned long long>(layout_markers_outside_2d_[2]));
    }
}

bool GxBackend::motion_trace_signals() noexcept {
    static const bool enabled = read_env_u64("GALAXY_MOTION_TRACE_SIGNALS", 0u) != 0u;
    return enabled;
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

void GxBackend::note_stripe_particles(const FrameChunk& chunk) {
    motion_stripe_marks_ += chunk.stripe_particles.size();
    if (!motion_trace_signals()) {
        return;
    }
    static std::uint64_t lines = 0u;
    if (lines >= 400u) {
        return;
    }
    // One strip = consecutive marks of one strip id with rising offsets.
    std::size_t strips = 0u;
    std::uint32_t min_age = 0xFFFFu, max_age = 0u;
    for (std::size_t i = 0; i < chunk.stripe_particles.size(); ++i) {
        const StripeParticleMark& mark = chunk.stripe_particles[i];
        if (i == 0u || mark.strip != chunk.stripe_particles[i - 1u].strip ||
            mark.offset <= chunk.stripe_particles[i - 1u].offset) {
            ++strips;
        }
        min_age = std::min(min_age, mark.age_lifetime >> 16u);
        max_age = std::max(max_age, mark.age_lifetime >> 16u);
    }
    ++lines;
    std::fprintf(stderr, "[gx-stripe] scene=%llu marks=%zu strips=%zu first-offset=%zu age=%u..%u\n",
                 static_cast<unsigned long long>(identity_scene_index_), chunk.stripe_particles.size(), strips,
                 chunk.stripe_particles.front().offset, min_age, max_age);
}

void GxBackend::take_attached_fifo_markers(FrameChunk& chunk, std::size_t fifo_size) noexcept {
    chunk.fifo_marker_count = 0;
    chunk.fifo_marker_view_mask = 0;
    for (std::uint8_t i = 0; i < producer_fifo_marker_count_; ++i) {
        if (producer_fifo_markers_[i].first <= fifo_size &&
            chunk.fifo_marker_count < chunk.fifo_markers.size()) {
            const std::uint8_t index = chunk.fifo_marker_count++;
            chunk.fifo_markers[index] = producer_fifo_markers_[i];
            if ((producer_fifo_marker_view_mask_ & (1u << i)) != 0u) {
                chunk.fifo_marker_views[index] = producer_fifo_marker_views_[i];
                chunk.fifo_marker_view_mask |= static_cast<std::uint8_t>(1u << index);
            }
        }
    }
    producer_fifo_marker_count_ = 0;
    producer_fifo_marker_view_mask_ = 0;
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

bool GxBackend::rt_geometry_draw() const noexcept {
    if (!rt_state_eligible_ || !scene_phase_ || side_layer_ != kSceneLayer || !renderer_.rt_scene_open()) {
        return false;
    }
    using motion::ScenePhase;
    return rt_phase_ == static_cast<std::uint8_t>(ScenePhase::OpaBeforeVolumeShadow) ||
           rt_phase_ == static_cast<std::uint8_t>(ScenePhase::OpaBeforeSilhouette) ||
           rt_phase_ == static_cast<std::uint8_t>(ScenePhase::Opa) ||
           rt_phase_ == static_cast<std::uint8_t>(ScenePhase::ListOpa);
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

bool GxBackend::begin_rt_batch(const DrawCall& call, RtDrawInfo& info) {
    if (!scene_phase_ || !(renderer_.rt_scene_open() || renderer_.rt_glow_open())) {
        return false;
    }
    const ScaledGxViewport viewport = compute_scaled_gx_viewport(call.xf_viewport, renderer_.efb_scale());
    const bool flat = !call.has_viewport || !viewport.valid() || viewport.max_depth - viewport.min_depth < 0.5f;
    // MR::clearZBuffer: a depth write through a flattened viewport puts the far plane under
    // everything drawn before (the sky and air layers); that geometry occludes nothing.
    if (flat && (render_state_key_.zmode_bits & (1u << 4u)) != 0u) {
        renderer_.rt_begin_scene();
        rt_view_set_ = false;
        return false;
    }
    if (flat) {
        return false;
    }
    const bool glow = !rt_geometry_draw();
    if (glow && !rt_glow_draw()) {
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
    const RtSurface surface = classify_rt_surface(vs_key_, ps_key_);
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
        const XfChannelCtrl color = state_.channel_ctrl(0u);
        info.surface = color.lighting_enable && color.light_mask != 0u ? kRtDrawLit : 0u;
        info.surface |= color.lighting_enable && (color.light_mask & 0x10u) != 0u ? kRtDrawPointLit : 0u;
        // The lighting reads its ambient register and light 0 colour (RtDrawInfo).
        if (info.surface != 0u && (color.light_mask & 1u) != 0u && !color.ambient_from_vertex &&
            (rt_draw_lights_ & 1u) != 0u && !rt_light_aim_.point) {
            info.surface |= kRtDrawSunKey;
        }
        if (glow) {
            info.surface = kRtDrawGlow;
        } else {
            info.surface |= static_cast<std::uint32_t>(surface.surface_class) << kRtDrawClassShift;
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
    if (!rt_view_set_) {
        // fill_vs_projection: D3D clip z = -(z E + F) (1 - 1e-7).
        constexpr float kFarEps = 1.0f - 1e-7f;
        RtView view{};
        view.projection = {projection(0u), projection(1u), projection(2u), projection(3u)};
        view.z_scale = -projection(4u) * kFarEps;
        view.z_offset = -projection(5u) * kFarEps;
        view.viewport = {viewport.x, viewport.y, viewport.width, viewport.height};
        view.min_depth = viewport.min_depth;
        view.max_depth = viewport.max_depth;
        renderer_.rt_set_view(view);
        rt_view_set_ = true;
    }
    return true;
}

// Actor lights (actor_lights.h): each scene's follow-camera lights go towards the traced sun,
// else to the game point light the ray tracer weighs strongest; with neither, or without the
// scene camera, they stay the game's. GALAXY_RT_ACTOR_LIGHTS=0 always leaves them; =2 moves them in
// odd scenes only (a frame-to-frame comparison).
void GxBackend::aim_rt_scene_lights(const std::array<float, 12>* view) {
    static const std::uint64_t mode = read_env_u64("GALAXY_RT_ACTOR_LIGHTS", 1u);
    rt_light_aim_ = {};
    rt_follow_lights_ = 0u;
    rt_follow_pending_ = 0u;
    ++rt_light_serial_;
    const bool enabled = mode == 1u || (mode == 2u && (rt_light_serial_ & 1u) != 0u);
    std::array<float, 3> sun{};
    if (view != nullptr && enabled && renderer_.rt_sun_direction(sun)) {
        rt_light_aim_ = aim_at_sun(*view, sun);
    } else if (view != nullptr && enabled) {
        std::array<GameLight, 32> lights{};
        const std::size_t count = game_point_lights(lights, 500u);
        const float radius = renderer_.rt_point_radius();
        float best = 0.0f;
        for (std::size_t i = 0; i < count; ++i) {
            // The ray tracer's reach: twelve half-light distances.
            const float half = lights[i].half_distance > 1.0f ? lights[i].half_distance : radius;
            const float weight = point_light_weight(*view, lights[i].position, lights[i].color, 12.0f * half);
            if (weight > best) {
                best = weight;
                rt_light_aim_ = aim_at_point(*view, lights[i].position);
            }
        }
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
                const std::uint64_t a = __rdtsc();
                total += __rdtsc() - a;
            }
            return total / 4096u;
        }();
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch());
        std::fprintf(stderr,
                     "[rt-actor-light] scene=%u aim=%s key=(%.3f %.3f %.3f) moved=%llu added=%llu cycles=%llu "
                     "regions=%llu pair=%llu snapshots=%llu snapshot-cycles=%llu tsc=%llu ns=%lld\n",
                     rt_light_serial_, !rt_light_aim_.valid ? "none" : rt_light_aim_.point ? "point" : "sun",
                     rt_light_aim_.key[0], rt_light_aim_.key[1], rt_light_aim_.key[2],
                     static_cast<unsigned long long>(rt_light_uploads_),
                     static_cast<unsigned long long>(rt_light_forced_),
                     static_cast<unsigned long long>(rt_light_cycles_),
                     static_cast<unsigned long long>(rt_light_regions_), static_cast<unsigned long long>(pair),
                     static_cast<unsigned long long>(rt_snapshot_count_),
                     static_cast<unsigned long long>(rt_snapshot_cycles_), static_cast<unsigned long long>(__rdtsc()),
                     static_cast<long long>(ns.count()));
    }
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
            std::memcmp(words, rt_follow_words_[n].data(), sizeof(rt_follow_words_[n])) == 0) {
            lights |= 1u << n;
        }
    }
    return lights != 0u ? (rt_light_serial_ << 8u) | lights : 0u;
}

// Moves the flushed draw's lights in a palette snapshot (write-only: the upload is write-combined
// memory). A point light's fill is opposite it at the draw's object: PNMTX0 for a per-vertex
// palette, else the default position matrix.
void GxBackend::write_rt_lights(std::uint32_t* palette) noexcept {
    if (!rt_light_aim_.valid) {
        return;
    }
    const std::uint64_t start = trace_actor_lights_enabled() ? __rdtsc() : 0u;
    float origin[3]{};
    if (rt_light_aim_.point) {
        const std::uint32_t row = (state_.cp(cp::kVcdLo) & 1u) != 0u
            ? 0u : std::min(state_.cp(cp::kMatrixIndexA) & 0x3Fu, 61u);
        for (std::uint32_t r = 0; r < 3u; ++r) {
            origin[r] = std::bit_cast<float>(state_.xf_raw()[(row + r) * 4u + 3u]);
        }
    }
    aim_actor_lights(palette, static_cast<std::uint8_t>(rt_draw_lights_ & 0x01u),
                     static_cast<std::uint8_t>(rt_draw_lights_ & 0x02u), rt_light_aim_, origin);
    if (start != 0u) [[unlikely]] {
        rt_light_cycles_ += __rdtsc() - start;
        ++rt_light_regions_;
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
    }
}

void GxBackend::trace_rt_scene() {
    if (rt_scene_traced_) {
        return;
    }
    rt_scene_traced_ = true;
    flush_pending_draw_batch();
    renderer_.rt_trace_scene();
}

void GxBackend::on_fifo_marker(FifoMarker marker, const std::array<float, 12>* view) {
    flush_pending_draw_batch();
    {
        static const std::uint64_t trace_from = read_env_u64("GALAXY_TRACE_DRAW_ORDER", 0u);
        if (trace_from != 0u && frame_index_ >= trace_from && (frame_index_ - trace_from) < 8u &&
            frame_index_ < trace_from + 6000u) [[unlikely]] {
            std::fprintf(stderr, "[draw-order] frame=%llu marker=%s phase=%u\n",
                         static_cast<unsigned long long>(frame_index_),
                         marker == FifoMarker::SceneBegin ? "scene-begin" : "scene-end", static_cast<unsigned>(rt_phase_));
        }
    }
    ++scene_markers_seen_;
    if (marker == FifoMarker::SceneBegin) {
        end_side_layer();
    }
    scene_phase_ = marker == FifoMarker::SceneBegin;
    if (marker == FifoMarker::SceneBegin) {
        rt_phase_ = 0xFFu;
        rt_view_set_ = false;
        rt_scene_traced_ = !renderer_.rt_active();
        if (!rt_scene_traced_) {
            renderer_.rt_begin_scene();
            renderer_.rt_set_camera(view);
        }
        aim_rt_scene_lights(renderer_.rt_lighting() ? view : nullptr);
    } else {
        trace_rt_scene();
        if (renderer_.rt_active() && side_layer_ == kSceneLayer) {
            renderer_.rt_capture_picture();
        }
    }
    const bool was_temporal = temporal_scene_;
    const bool was_motion = motion_scene_;
    if (marker == FifoMarker::SceneBegin) {
        temporal_scene_ = renderer_.scene_temporal_begin(
            scene_jitter_px_[0], scene_jitter_px_[1]);
        motion_scene_ = temporal_scene_ && renderer_.begin_motion_scene();
        scene_camera_captured_ = false;
        history_scene_ = temporal_scene_ || interp_recording_;
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
            static const bool motion_debug = read_env_u64("GALAXY_MOTION_DEBUG", 0u) != 0u;
            if (motion_debug && motion_scene_) {
                renderer_.record_motion_debug_view();
            }
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
            static const bool trace_motion = read_env_u64("GALAXY_MOTION_TRACE", 0u) != 0u;
            if (trace_motion && (motion_trace_scenes_++ % 120u) == 0u) {
                std::cerr << "[motion] frame=" << renderer_.frame_index()
                          << " records=" << stats.records
                          << " matched=" << stats.matched
                          << " previous=" << stats.previous_records
                          << " prev-frame-gap="
                          << (transform_history_.current_frame() -
                              transform_history_.previous_frame())
                          << " camera=" << (transform_history_.camera_known() ? 1 : 0)
                          << " writers=" << (motion_scene_ ? 1 : 0) << '\n';
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
    if (was_temporal || temporal_scene_ || interp_recording_) {
        frame_bindings_dirty_ = true;
    }
    // Motion keys switch on or off: resolve the pipeline again.
    if (was_motion != motion_scene_) {
        current_pipeline_ = nullptr;
    }
    // Scene end: the 2D layer renders into the output-resolution EFB. A new
    // scene that begins before a display copy restores the render EFB.
    bool switched = false;
    if (marker == FifoMarker::SceneEnd) {
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
        begin_side_layer(kScreenLayer);
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

bool GxBackend::enqueue_pointer_depth_capture(const std::shared_ptr<PointerDepthCapture>& capture) {
    if (!initialized_ || !capture) return false;
    if (!capture->complete.exchange(false,std::memory_order_acq_rel))
        throw std::logic_error("pointer depth capture reused while owned by render worker");
    capture->success=false; capture->error=nullptr; capture->worker_ns=0u; capture->rgba=nullptr;
    try {
        if (!async_render_thread_enabled()) {
            const auto start=std::chrono::steady_clock::now();
            capture->success=renderer_.capture_pointer_depth(capture->pixels);
            capture->worker_ns=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());
            capture->publish_complete();
            return capture->success;
        }
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (render_thread_error_) std::rethrow_exception(render_thread_error_);
            if (stop_render_thread_) {capture->publish_complete();return false;}
            FrameChunk chunk;
            chunk.efb_peek_only=true;
            chunk.pointer_depth_capture=capture;
            frame_queue_.push_back(std::move(chunk));
        }
        queue_cv_.notify_one();
        return true;
    } catch (...) {
        capture->error=std::current_exception();
        capture->publish_complete();
        throw;
    }
}

bool GxBackend::capture_pointer_depth(std::span<std::uint32_t> pixels) {
    if (!initialized_) {
        return false;
    }

    if (!async_render_thread_enabled()) {
        return renderer_.capture_pointer_depth(pixels);
    }

    EfbPeekRequest request;
    request.kind = EfbPeekKind::Depth;
    request.depth_pixels = pixels;

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
        std::unique_lock<std::mutex> lock(request.mutex);
        request.cv.wait(lock, [&request] { return request.done; });
    }
    if (request.error != nullptr) {
        std::rethrow_exception(request.error);
    }
    return request.success;
}

bool GxBackend::peek_efb(
    std::uint16_t x,
    std::uint16_t y,
    EfbPeekKind kind,
    std::uint32_t& value) {
    if (!initialized_) {
        return false;
    }

    if (!async_render_thread_enabled()) {
        return renderer_.peek_efb(x, y, kind, value);
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
// GALAXY_INTERP_EVENTS=first,last: one line per interpolation event while the
// latest log serial is in [first, last].
void GxBackend::interp_event(const char* what, std::uint64_t a, std::uint64_t b) const {
    static const std::pair<std::uint64_t, std::uint64_t> range = [] {
        char value[64]{};
        std::size_t length = 0u;
        std::pair<std::uint64_t, std::uint64_t> r{1u, 0u};
        if (getenv_s(&length, value, sizeof(value), "GALAXY_INTERP_EVENTS") == 0 && length > 1u) {
            unsigned long long first = 0, last = 0;
            if (sscanf_s(value, "%llu,%llu", &first, &last) == 2) r = {first, last};
        }
        return r;
    }();
    const std::uint64_t serial = renderer_.replay_serial();
    if (serial < range.first || serial > range.second) {
        return;
    }
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    std::ostringstream message;
    message << "[interp-event] qpc=" << now.QuadPart << " log=" << serial << ' ' << what
            << ' ' << a << ' ' << b << '\n';
    std::cerr << message.str();
}

void GxBackend::note_interp_log_completion() {
    const std::uint64_t serial = renderer_.replay_serial();
    if (serial == interp_seen_serial_) {
        return;
    }
    interp_seen_serial_ = serial;
    interp_event("log-closed", renderer_.replay_available() ? 1u : 0u, renderer_.replay_xfb_addr());
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
    static const std::int64_t frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return static_cast<std::int64_t>(value.QuadPart);
    }();
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    if (!renderer_.start_replay_playback()) {
        return;
    }
    const RenderConfig cfg = get_render_config();
    double rate = std::max(61.0, present_frame_rate(
        cfg.max_fps, cfg.vsync_interval, renderer_.display_refresh_hz()));
    ++interp_sequence_serial_;
    ++interp_started_;
    // Logs completed but never played (a newer one replaced them first).
    const std::uint64_t played = renderer_.playing_serial();
    if (interp_last_played_serial_ != 0u && played > interp_last_played_serial_ + 1u &&
        played - interp_last_played_serial_ < 8u) {
        interp_logs_skipped_ += played - interp_last_played_serial_ - 1u;
    }
    interp_last_played_serial_ = played;
    note_game_frame_cadence(now.QuadPart);
    interp_event("start", played, xfb_addr);
    static const std::uint64_t trace_level = read_env_u64("GALAXY_INTERP_TRACE", 0u);
    if (trace_level >= 2u) {
        std::cerr << "[interp-start] seq=" << interp_sequence_serial_ << " log=" << played << '\n';
    }
    interp_sequence_.active = true;
    interp_sequence_.flush_requested = false;
    interp_sequence_.classified = false;
    interp_sequence_.scene = interp_log_scene_;
    select_interp_before();
    interp_sequence_.xfb_addr = xfb_addr;
    interp_sequence_.subframes = interp_gpu_governor(rate);
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
    interp_slot_period_qpc_ = static_cast<std::int64_t>(static_cast<double>(frequency) / rate);
    const bool locked = std::abs(rate / 60.0 - std::round(rate / 60.0)) < 0.02;
    // The arrival clock: the render thread sees a frame's present when it
    // reaches the queue (a chunk in progress delays it by milliseconds), so
    // arrivals are the game's frame cadence, smoothed: the prediction one or
    // more frame periods on, pulled 1/8 toward the measured arrival, with the
    // period tracking the game's rate. A miss by a third of a period or more
    // (pause, hitch) restarts it at the measured time.
    if (interp_frame_period_qpc_ == 0) {
        interp_frame_period_qpc_ = frequency / 60;
    }
    std::int64_t arrival = now.QuadPart;
    if (interp_arrival_qpc_ != 0 && now.QuadPart > interp_arrival_qpc_) {
        const std::int64_t periods = std::max<std::int64_t>(1,
            (now.QuadPart - interp_arrival_qpc_ + interp_frame_period_qpc_ / 2) /
                interp_frame_period_qpc_);
        const std::int64_t predicted = interp_arrival_qpc_ + periods * interp_frame_period_qpc_;
        const std::int64_t error = now.QuadPart - predicted;
        if (periods <= 4 && std::abs(error) < interp_frame_period_qpc_ / 3) {
            arrival = predicted + error / 8;
            interp_frame_period_qpc_ = std::clamp<std::int64_t>(
                interp_frame_period_qpc_ + error / (64 * periods), frequency / 62, frequency / 58);
        }
    }
    interp_arrival_qpc_ = arrival;
    interp_sequence_.frame_qpc = arrival;
    interp_sequence_.lead = locked ? 60.0 / rate : 0.0;
    if (locked || interp_slot_due_qpc_ == 0 ||
        now.QuadPart - interp_slot_due_qpc_ > interp_slot_period_qpc_ ||
        interp_slot_due_qpc_ - now.QuadPart > interp_slot_period_qpc_) {
        // Locked rates start the slots at the (smoothed) arrival.
        interp_slot_due_qpc_ = locked && arrival <= now.QuadPart + interp_slot_period_qpc_
            ? arrival : now.QuadPart;
    }
}

// B18: the scene before the starting one was never on screen (its log was
// replaced before a present started it, or its sequence ended before showing
// anything). The sub-frames then start from the scene shown last, records
// matched through the skipped scenes, instead of from a frame never shown.
// Vertex blending needs consecutive logs (prepare_vertex_blend) and stays off
// for such a sequence.
void GxBackend::select_interp_before() {
    const unsigned scene = interp_sequence_.scene;
    interp_sequence_.before = (scene + 3u) % 4u;
    interp_sequence_.chained = false;
    if (interp_shown_scene_ < 0 ||
        static_cast<unsigned>(interp_shown_scene_) == interp_sequence_.before ||
        static_cast<unsigned>(interp_shown_scene_) == scene) {
        return;
    }
    // GALAXY_INTERP_CHAIN=0: start from the scene before regardless (A/B).
    static const bool chain = read_env_u64("GALAXY_INTERP_CHAIN", 1u) != 0u;
    const unsigned shown = static_cast<unsigned>(interp_shown_scene_);
    const unsigned depth = (scene + 4u - shown) % 4u;  // 2 or 3 scenes back
    const InterpScene& target = interp_scenes_[scene];
    bool consecutive = chain && target.valid && target.serial != 0u;
    for (unsigned step = 1u; consecutive && step <= depth; ++step) {
        const InterpScene& older = interp_scenes_[(scene + 4u - step) % 4u];
        consecutive = older.valid && older.serial + step == target.serial;
    }
    static const bool trace = read_env_u64("GALAXY_INTERP_TRACE", 0u) != 0u;
    if (trace) {
        std::cerr << "[interp-chain] seq=" << interp_sequence_serial_ << " depth=" << depth
                  << " chained=" << (consecutive ? 1 : 0) << '\n';
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

// B22: as many sub-frames per game frame as the GPU fits. A sub-frame
// replays the whole frame, so a game frame costs about (k + 1) sub-frame
// times (measured with GPU timestamps). k drops at once when over budget and
// rises one step after 10 s under it; with none left, one is tried again
// every 20 s. A capped sequence runs the slot clock at 60 * (k + 1).
// GALAXY_INTERP_GPU_BUDGET_MS: GPU time per game frame (default 15 of 16.7);
// GALAXY_INTERP_GPU_GOVERNOR=0 turns it off.
unsigned GxBackend::interp_gpu_governor(double& rate) {
    const unsigned configured = interp_stopping_ ? 0u : interp_frames_;
    static const bool enabled = read_env_u64("GALAXY_INTERP_GPU_GOVERNOR", 1u) != 0u;
    static const double budget_ms = [] {
        char value[32]{};
        std::size_t length = 0u;
        const double ms = getenv_s(&length, value, sizeof(value), "GALAXY_INTERP_GPU_BUDGET_MS") == 0 &&
                                  length > 1u
            ? std::strtod(value, nullptr) : 0.0;
        return ms > 0.0 ? ms : 15.0;
    }();
    if (configured == 0u) {
        return configured;
    }
    const unsigned allowed = std::min(configured, interp_cadence_cap_);
    if (!enabled) {
        if (allowed < configured) {
            ++interp_gpu_capped_;
            rate = 60.0 * static_cast<double>(allowed + 1u);
        }
        return allowed;
    }
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
                ++interp_gpu_cap_;
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

// Sub-frames must never cost the game its own frames. Each game frame shown is
// timed against the VI period: an interval above 1.5 periods missed VIs. The
// game's own misses are measured while no sub-frames run. Two or more misses
// per second beyond those take a sub-frame away (half of them at eight); a try
// of one more sub-frame is undone at its first miss when the game misses none
// on its own. Tries follow a clean interval that doubles after each failed
// one (8 s to 64 s) and halves after one that held.
// GALAXY_INTERP_CADENCE_GOVERNOR=0 turns it off.
void GxBackend::restart_interp_governors() {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    interp_gpu_cap_ = 7u;
    interp_gpu_cap_serial_ = interp_sequence_serial_;
    interp_gpu_checked_ = interp_sequence_serial_;
    interp_gpu_warm_until_ = interp_sequence_serial_ + 60u;
    renderer_.reset_subframe_gpu_ema();
    interp_cadence_cap_ = 7u;
    interp_cadence_interval_s_ = 8u;
    interp_cadence_try_qpc_ = 0;
    interp_cadence_change_qpc_ = now.QuadPart;
    interp_cadence_last_qpc_ = 0;
    interp_cadence_grace_until_qpc_ = now.QuadPart + frequency.QuadPart * 3 / 2;
}

void GxBackend::note_game_frame_cadence(std::int64_t now_qpc) {
    static const bool enabled = read_env_u64("GALAXY_INTERP_CADENCE_GOVERNOR", 1u) != 0u;
    static const std::int64_t frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return static_cast<std::int64_t>(value.QuadPart);
    }();
    if (!enabled) {
        return;
    }
    const std::int64_t period = frequency * 1001 / 60000;
    if (interp_cadence_last_qpc_ == 0 || now_qpc - interp_cadence_last_qpc_ > 4 * frequency) {
        // First frame, or a pause (menu, load): a new window.
        interp_cadence_last_qpc_ = now_qpc;
        interp_cadence_window_qpc_ = now_qpc;
        interp_cadence_frames_ = 0u;
        interp_cadence_misses_ = 0u;
        return;
    }
    const std::int64_t interval = now_qpc - interp_cadence_last_qpc_;
    interp_cadence_last_qpc_ = now_qpc;
    if (now_qpc < interp_cadence_grace_until_qpc_) {
        // Just after a rate or upscaler change: its one-time work is no measure.
        interp_cadence_window_qpc_ = now_qpc;
        interp_cadence_frames_ = 0u;
        interp_cadence_misses_ = 0u;
        interp_cadence_stalls_ = 0u;
        return;
    }
    ++interp_cadence_frames_;
    // One stall of four periods or more in a window (a load, a shader or upscaler being
    // created) is not the sub-frames' doing: they cost VIs every frame, not one long wait.
    if (interval > period * 4 && interp_cadence_stalls_++ == 0u) {
        return;
    }
    const std::uint32_t missed =
        interval > period * 3 / 2 ? static_cast<std::uint32_t>((interval + period / 2) / period) - 1u : 0u;
    interp_cadence_misses_ += missed;
    const unsigned running = std::min({interp_frames_, interp_cadence_cap_, interp_gpu_cap_});
    const bool trying = interp_cadence_try_qpc_ != 0 && now_qpc - interp_cadence_try_qpc_ < 4 * frequency;
    const auto drop = [&](unsigned cap) {
        if (trying) {
            interp_cadence_interval_s_ = std::min(interp_cadence_interval_s_ * 2u, 64u);
        }
        // The replay at t = 1 alone costs the game its frames (measured to
        // take a quarter of a frame or more, not a load hitch).
        const double replay_ms = renderer_.subframe_gpu_ema_ms();
        if (cap == 0u && renderer_.temporal_interpolation() && replay_ms * 4.0 >= 1000.0 / 60.0) {
            renderer_.suspend_temporal_interpolation(replay_ms);
        }
        interp_cadence_cap_ = cap;
        interp_cadence_change_qpc_ = now_qpc;
        interp_cadence_try_qpc_ = 0;
        ++interp_cadence_drops_;
    };
    if (trying && missed != 0u && running > 0u && interp_cadence_own_misses_ < 0.5) {
        drop(running - 1u);
        return;
    }
    const std::int64_t elapsed = now_qpc - interp_cadence_window_qpc_;
    if (elapsed < frequency) {
        return;
    }
    const double seconds = static_cast<double>(elapsed) / static_cast<double>(frequency);
    interp_cadence_fps_ = static_cast<double>(interp_cadence_frames_) / seconds;
    const double misses = static_cast<double>(interp_cadence_misses_) / seconds;
    interp_cadence_window_qpc_ = now_qpc;
    interp_cadence_frames_ = 0u;
    interp_cadence_misses_ = 0u;
    interp_cadence_stalls_ = 0u;
    if (running == 0u) {
        interp_cadence_own_misses_ = 0.5 * (interp_cadence_own_misses_ + misses);
    } else {
        interp_cadence_own_misses_ = std::min(interp_cadence_own_misses_, misses);
    }
    const double excess = misses - interp_cadence_own_misses_;
    if (running > 0u && excess >= 2.0) {
        drop(excess >= 8.0 ? running / 2u : running - 1u);
        return;
    }
    if (interp_cadence_cap_ >= interp_frames_) {
        return;
    }
    // Misses with the GPU this far from its budget came from elsewhere (CPU stalls): the
    // sub-frames come back after 2 s, all at once.
    const double sub_ms = renderer_.subframe_gpu_ema_ms();
    const bool headroom = sub_ms > 0.0 && sub_ms * static_cast<double>(interp_frames_ + 2u) < 1000.0 / 60.0 * 0.5;
    const std::int64_t wait = headroom ? 2 * frequency
                                       : static_cast<std::int64_t>(interp_cadence_interval_s_) * frequency;
    if (now_qpc - interp_cadence_change_qpc_ >= wait) {
        interp_cadence_cap_ = headroom ? interp_frames_ : interp_cadence_cap_ + 1u;
        interp_cadence_change_qpc_ = now_qpc;
        interp_cadence_try_qpc_ = now_qpc;
    } else if (interp_cadence_try_qpc_ != 0 && now_qpc - interp_cadence_try_qpc_ >= 4 * frequency) {
        interp_cadence_interval_s_ = std::max(interp_cadence_interval_s_ / 2u, 8u);
        interp_cadence_try_qpc_ = 0;
    }
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
    interp_event("superseded", interp_last_played_serial_, interp_sequence_.next);
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
    static const std::uint64_t every = std::max<std::uint64_t>(read_env_u64("GALAXY_INTERP_SNAPSHOT_EVERY", 1u), 1u);
    if (directory.empty() || interp_sequence_serial_ < first || interp_sequence_serial_ > last ||
        (interp_sequence_serial_ - first) % every != 0u) {
        return {};
    }
    return directory + "\\seq_" + std::to_string(interp_sequence_serial_) + "_" + suffix + ".ppm";
}

// t of the next slot in the playing sequence (see advance_interp_sequence).
double GxBackend::interp_slot_t() const {
    static const std::int64_t frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return static_cast<std::int64_t>(value.QuadPart);
    }();
    return static_cast<double>(interp_slot_due_qpc_ - interp_sequence_.frame_qpc) * 60.0 /
               static_cast<double>(frequency) + interp_sequence_.lead;
}

std::int64_t GxBackend::interp_remaining_us() const {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    static const std::int64_t frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return static_cast<std::int64_t>(value.QuadPart);
    }();
    return (interp_slot_due_qpc_ - now.QuadPart) * 1'000'000 / frequency;
}

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

// Render thread: a short wait to a present slot, finer than the scheduler tick.
void GxBackend::interp_sleep_us(std::int64_t us) {
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
    std::this_thread::sleep_for(std::chrono::microseconds(us));
}

void GxBackend::advance_interp_sequence(bool flush) {
    static const std::int64_t frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return static_cast<std::int64_t>(value.QuadPart);
    }();
    flush = flush || interp_sequence_.flush_requested;
    while (interp_sequence_.active) {
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        InterpSequence& sequence = interp_sequence_;
        // Slots run on the QPC clock with VSync too: a Present that waits
        // for the display would hold back the frame chunks behind it.
        if (!flush && now.QuadPart < interp_slot_due_qpc_) {
            return;
        }
        const std::int64_t slot = interp_slot_due_qpc_;
        // A free-running slot due just before the arrival shows t = 0.
        const double t = std::max(0.0, static_cast<double>(slot - sequence.frame_qpc) * 60.0 /
                                           static_cast<double>(frequency) + sequence.lead);
        if (!flush) {
            interp_slot_due_qpc_ = slot + interp_slot_period_qpc_;
            // A stall skips the slots it missed instead of bunching them.
            if (now.QuadPart >= interp_slot_due_qpc_) {
                interp_late_slots_ += static_cast<std::uint64_t>(
                    (now.QuadPart - interp_slot_due_qpc_) / interp_slot_period_qpc_ + 1);
                interp_slot_due_qpc_ = now.QuadPart + interp_slot_period_qpc_;
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
            if (!snapshot.empty()) {
                renderer_.set_debug_capture_paths({}, snapshot);
                renderer_.set_subframe_capture(true);
                renderer_.set_capture_waits(true);
            }
            // GALAXY_INTERP_FORCE_T=<0..1>: every sub-frame at that t (replay checks: 1 must
            // reproduce the frame, 0 the frame before).
            static const double force_t = [] {
                char value[32]{};
                std::size_t length = 0u;
                return getenv_s(&length, value, sizeof(value), "GALAXY_INTERP_FORCE_T") == 0 && length > 1u
                    ? std::clamp(std::strtod(value, nullptr), 0.0, 1.0) : -1.0;
            }();
            // GALAXY_INTERP_COPY_AB=1: sequences alternate redirected and guarded copies; =3 adds
            // a third with neither (A/B in one run, diagnostics).
            static const std::uint64_t copy_ab = read_env_u64("GALAXY_INTERP_COPY_AB", 0u);
            if (copy_ab != 0u) {
                const std::uint64_t phase = interp_sequence_serial_ % (copy_ab == 3u ? 3u : 2u);
                renderer_.set_subframe_copy_redirect(phase == 0u ? 1 : phase == 1u ? 0 : 2);
            }
            const bool presented = render_interp_subframe(static_cast<float>(force_t >= 0.0 ? force_t : t));
            interp_event("sub", interp_last_played_serial_, static_cast<std::uint64_t>(t * 1000.0));
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
            } else {
                ++interp_failed_;
            }
            ++sequence.next;
            // Rates that are not multiples of 60 rarely present real frames.
            report_interp_stats();
            return;
        }
        sequence.active = false;
        interp_event("real", interp_last_played_serial_, static_cast<std::uint64_t>(std::max(0.0, t) * 1000.0));
        interp_snapshot_path_ = interp_snapshot_path("real");
        renderer_.set_capture_waits(!interp_snapshot_path_.empty());
        interp_presenting_real_ = true;
        present_cached_xfb_on_thread(sequence.xfb_addr, false, true);
        interp_presenting_real_ = false;
        interp_shown_scene_ = static_cast<std::int32_t>(sequence.scene);
        renderer_.set_capture_waits(false);
        interp_snapshot_path_.clear();
        ++interp_sequences_;
        report_interp_stats();
        return;
    }
}

// A game frame presented while interpolating, without sub-frames: why.
void GxBackend::note_interp_real_present() {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    note_game_frame_cadence(now.QuadPart);
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
    static const std::int64_t frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return static_cast<std::int64_t>(value.QuadPart);
    }();
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    if (interp_stats_qpc_ == 0) interp_stats_qpc_ = now.QuadPart;
    if (now.QuadPart - interp_stats_qpc_ < 2 * frequency) {
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
    const double seconds = static_cast<double>(now.QuadPart - interp_stats_qpc_) /
                           static_cast<double>(frequency);
    const double presented = static_cast<double>(interp_presented_);
    std::uint64_t real_only = 0u;
    for (const std::uint64_t count : interp_real_only_) real_only += count;
    const RendererD3D12::CopyRedirectStats redirect = renderer_.take_copy_redirect_stats();
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
            << " superseded=" << interp_superseded_
            << " started=" << interp_started_
            << " logs-skipped=" << interp_logs_skipped_
            << " chained-sequences=" << interp_chained_sequences_
            << " unchained-skips=" << interp_unchained_skips_
            << " presents-during-sequence=" << interp_present_skipped_
            << " snapped-records=" << interp_snapped_
            << " failed=" << interp_failed_
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
    interp_superseded_ = 0u;
    interp_started_ = 0u;
    interp_logs_skipped_ = 0u;
    interp_chained_sequences_ = 0u;
    interp_unchained_skips_ = 0u;
    interp_present_skipped_ = 0u;
    interp_snapped_ = 0u;
    interp_failed_ = 0u;
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
    interp_stats_qpc_ = now.QuadPart;
}

// One interpolated frame at t (0 = previous game frame, 1 = the latest):
// the camera at t, and every scene record whose constants change rebuilt
// from its matched previous record (frame_interpolation.h): position and
// normal matrices in world space under the camera at t, texture matrices,
// lights and projection; then the latest frame's operations replayed.
bool GxBackend::render_interp_subframe(float t) {
    const InterpScene& scene = interp_scenes_[interp_sequence_.scene];
    const InterpScene& before_scene = interp_scenes_[interp_sequence_.before];
    // Matched records of the scene before (through skipped scenes: B18).
    const std::vector<std::int32_t>& prev_index =
        interp_sequence_.chained ? interp_chain_prev_ : scene.prev_index;
    if (!scene.valid) {
        renderer_.note_replay_failure(9u);
        return false;
    }
    const auto build_start = std::chrono::steady_clock::now();
    const std::size_t count = scene.constants.size();
    constexpr std::size_t kWords = kMatrixPaletteWords;
    static_assert(kMatrixPaletteWords == kXfPaletteWords);
    const bool before_valid = before_scene.valid;
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
        // The game's camera epoch (generation 20) changes when the view was set
        // without blending: a hard cut whatever the thresholds say. The
        // thresholds remain for cuts the game does not flag.
        const bool game_cut = before_valid && scene.camera_epoch_known && before_scene.camera_epoch_known &&
                              scene.camera_epoch != before_scene.camera_epoch;
        const bool threshold_cut = interp_camera_.cut;
        if (game_cut && interp_camera_.known) {
            interp_camera_.cut = true;
        }
        interp_camera_move_ = interp_camera_.known && !game_cut ? interp_camera_.moved : 0.0f;
        if (interp_camera_.cut) {
            ++interp_camera_cuts_;
            ++(game_cut ? interp_game_cuts_ : interp_threshold_cuts_);
        }
        static const std::uint64_t dump_records = read_env_u64("GALAXY_INTERP_DUMP_RECORDS", 0u);
        if (dump_records != 0u && interp_sequence_serial_ == dump_records) {
            const auto& current = transform_history_.previous();  // the playing scene's records
            std::ostringstream out;
            out << "[interp-records] seq=" << interp_sequence_serial_ << " count=" << count
                << " history-previous=" << current.size() << '\n';
            for (std::size_t r = 0; r < count; ++r) {
                const std::int32_t m = prev_index[r];
                const GxVsConstants& now = scene.constants[r];
                out << "  r=" << r << " m=" << m << " pv=" << int(scene.per_vertex[r])
                    << " pal=" << scene.palette[r]
                    << " id=" << std::hex << (r < current.size() ? current[r].identity : 0u) << std::dec
                    << " t=(" << now.inline_pos_matrix[0][3] << ',' << now.inline_pos_matrix[1][3] << ','
                    << now.inline_pos_matrix[2][3] << ')';
                if (before_valid && m >= 0 && static_cast<std::size_t>(m) < before_scene.constants.size()) {
                    const GxVsConstants& b = before_scene.constants[static_cast<std::size_t>(m)];
                    out << " prev=(" << b.inline_pos_matrix[0][3] << ',' << b.inline_pos_matrix[1][3] << ','
                        << b.inline_pos_matrix[2][3] << ')';
                }
                out << '\n';
            }
            std::cerr << out.str();
        }
        static const bool trace_camera = read_env_u64("GALAXY_INTERP_TRACE_CAMERA", 0u) != 0u;
        if (trace_camera) {
            std::ostringstream line;
            line << "[interp-camera] seq=" << interp_sequence_serial_ << " scene=" << interp_sequence_.scene
                 << " known=" << interp_camera_.known << " rigid=" << interp_camera_.rigid
                 << " cut=" << interp_camera_.cut << " game-cut=" << game_cut << " threshold-cut=" << threshold_cut
                 << " epochs=" << (before_scene.camera_epoch_known ? static_cast<std::int64_t>(before_scene.camera_epoch) : -1)
                 << '/' << (scene.camera_epoch_known ? static_cast<std::int64_t>(scene.camera_epoch) : -1)
                 << " moved=" << interp_camera_.moved
                 << " turn=" << interp_camera_.turn_degrees << " before-valid=" << before_valid
                 << " records=" << count << " before-records=" << before_scene.constants.size() << " Va=";
            for (float v : before_scene.view) line << v << ',';
            line << " Vb=";
            for (float v : scene.view) line << v << ',';
            line << '\n';
            std::cerr << line.str();
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
        if (trace_camera) {
            std::size_t matched = 0u, inline_records = 0u, inline_unmatched = 0u, snapped = 0u;
            for (std::size_t r = 0; r < count; ++r) {
                matched += prev_index[r] >= 0 ? 1u : 0u;
                if (scene.per_vertex[r] == 0u) {
                    ++inline_records;
                    inline_unmatched += prev_index[r] < 0 ? 1u : 0u;
                }
            }
            for (const InterpRecordMotion& moving : interp_moving_) {
                snapped += moving.position.snapped ? 1u : 0u;
            }
            std::size_t pairs_without_before = 0u, palette_snapped_slots = 0u;
            for (const InterpPaletteMotion& entry : interp_palette_motions_) {
                pairs_without_before += entry.before < 0 ? 1u : 0u;
                palette_snapped_slots += entry.motion.snapped;
            }
            std::ostringstream line;
            line << "[interp-seq] seq=" << interp_sequence_serial_ << " records=" << count
                 << " matched=" << matched << " inline=" << inline_records
                 << " inline-unmatched=" << inline_unmatched << " inline-snapped=" << snapped
                 << " moving=" << interp_moving_.size() << " palette-pairs=" << interp_palette_motions_.size()
                 << " pairs-without-before=" << pairs_without_before
                 << " palette-snapped-slots=" << palette_snapped_slots
                 << " palettes=" << scene.palette_count << '/' << before_scene.palette_count;
            // Records per draw phase (galaxy/motion_signals.h; the last is none).
            std::array<std::uint32_t, 11> phase_records{};
            for (const std::uint32_t category : scene.category) {
                const std::uint8_t phase = motion::category_phase(category);
                ++phase_records[phase < 10u ? phase : 10u];
            }
            line << " phase-records=";
            for (std::size_t i = 0; i < phase_records.size(); ++i) line << (i != 0u ? "," : "") << phase_records[i];
            line << '\n';
            std::cerr << line.str();
        }
        const auto blend_start = std::chrono::steady_clock::now();
        // Vertex blending pairs draws with the log run before: not for a
        // sequence chained past a skipped scene (B18).
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
    // GALAXY_INTERP_VERIFY=1 with GALAXY_INTERP_FORCE_T=0: every matched block at
    // t = 0 must equal the previous frame's (diagnostics).
    static const bool verify = read_env_u64("GALAXY_INTERP_VERIFY", 0u) != 0u;
    if (verify && t == 0.0f && interp_sequence_.next == 0u) {
        verify_interp_program(scene, before_scene, camera_t);
    }
    // The frame presented before: earlier in this sequence, or this sequence's
    // t = 0 when it was the scene before at t = 1. Anything else (a frame
    // presented alone, a cut, a gap) starts the upscaler's history again.
    RendererD3D12::SubframeTime time{};
    time.t = t;
    time.t_prev = t;
    if (interp_shown_serial_ == interp_sequence_serial_ && interp_shown_t_ < t) {
        time.t_prev = interp_shown_t_;
    } else if (interp_shown_t_ >= 1.0f && interp_shown_scene_ == static_cast<std::int32_t>(interp_sequence_.before) &&
               !(interp_camera_.known && interp_camera_.cut)) {
        time.t_prev = 0.0f;
    } else {
        time.reset = true;
    }
    const Affine34 camera_prev = time.t_prev == t ? camera_t : camera_at(interp_camera_, time.t_prev);
    check_interp_eval(scene, camera_t, t, camera_prev, time.t_prev);
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
    // GALAXY_INTERP_PARTS: bit 0 inline position, 1 texture matrices, 2 projection, 3 palette,
    // 4 vertex blending, 5 colours (diagnostics).
    static const std::uint64_t parts = read_env_u64("GALAXY_INTERP_PARTS", 0x3Fu);
    constexpr std::size_t kWords = kMatrixPaletteWords;
    const std::size_t count = scene.constants.size();
    const auto before_palette = [&](std::int32_t shadow) -> const std::uint32_t* {
        return before_valid && shadow >= 0 && static_cast<std::size_t>(shadow) < before_scene.palette_count
            ? before_scene.palettes.data() + static_cast<std::size_t>(shadow) * kWords
            : nullptr;
    };
    interp_program_.clear();
    interp_vs_slot_.assign(count, -1);
    interp_record_palette_slot_.assign(count, -1);
    interp_ps_slot_.assign(scene.ps_constants.size(), -1);
    interp_palette_slot_.assign(interp_palette_motions_.size(), -1);
    interp_palette_source_.clear();
    interp_palette_entry_.clear();
    if ((parts & 8u) != 0u) {
        for (const InterpRecordMotion& moving : interp_moving_) {
            if (moving.palette < 0) continue;
            std::int32_t& slot = interp_palette_slot_[static_cast<std::size_t>(moving.palette)];
            if (slot < 0) {
                slot = static_cast<std::int32_t>(interp_palette_source_.size());
                interp_palette_source_.push_back(scene.constants[moving.record].mtx_palette_base);
                interp_palette_entry_.push_back(moving.palette);
            }
        }
    }
    const bool colours = (parts & 0x20u) != 0u;
    // Temporal upscaling of the replays: the motion vector fields of each
    // moving block at the frame presented before (blocks the program leaves
    // alone carry their own transform there: no motion).
    const bool motion = renderer_.temporal_interpolation();
    interp_layout_ = {static_cast<std::uint32_t>(interp_moving_.size()),
                      static_cast<std::uint32_t>(interp_palette_source_.size()),
                      colours ? static_cast<std::uint32_t>(interp_ps_moving_.size()) : 0u,
                      motion ? static_cast<std::uint32_t>(interp_palette_source_.size()) : 0u};
    const interp_gpu::Layout& layout = interp_layout_;
    for (std::size_t s = 0; s < interp_moving_.size(); ++s) {
        const InterpRecordMotion& moving = interp_moving_[s];
        interp_vs_slot_[moving.record] = static_cast<std::int32_t>(s);
        const std::uint32_t base = layout.vs_offset(static_cast<std::uint32_t>(s)) / 4u;
        const GxVsConstants& recorded = scene.constants[moving.record];
        const GxVsConstants* before = moving.prev >= 0
            ? &before_scene.constants[static_cast<std::size_t>(moving.prev)] : nullptr;
        if (moving.position_moves && (parts & 1u) != 0u) {
            interp_program_.eye(base + interp_gpu::kVsPosition, moving.position);
        }
        if (moving.tex_moves && before != nullptr && (parts & 2u) != 0u) {
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
        if (moving.colour_moves && before != nullptr && colours) {
            interp_program_.channel_colours(base, *before, recorded);
        }
        if (moving.projection_moves && before != nullptr && (parts & 4u) != 0u) {
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
            if (moving.position_moves && (parts & 1u) != 0u) {
                interp_program_.eye(base + interp_gpu::kVsMotionPrevPos, moving.position);
            }
            if (moving.projection_moves && before != nullptr && (parts & 4u) != 0u) {
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
                                              entry.motion);
            interp_program_.at_previous(false);
        }
    }
    if (colours) {
        for (std::size_t k = 0; k < interp_ps_moving_.size(); ++k) {
            const auto [p, q] = interp_ps_moving_[k];
            interp_ps_slot_[p] = static_cast<std::int32_t>(k);
            interp_program_.ps_colours(layout.ps_offset(static_cast<std::uint32_t>(k)) / 4u,
                                       before_scene.ps_constants[q], scene.ps_constants[p]);
        }
    }
    interp_program_ops_ += interp_program_.ops.size();
    interp_program_words_ += interp_program_.payload.size();
    RendererD3D12::ReplaySequence sequence{};
    sequence.layout = layout;
    sequence.record_vs_slot = interp_vs_slot_;
    sequence.record_palette_slot = interp_record_palette_slot_;
    sequence.ps_slot = interp_ps_slot_;
    sequence.palette_source = interp_palette_source_;
    sequence.program = &interp_program_;
    sequence.vertex_blend = (parts & 0x10u) != 0u;
    return renderer_.begin_replay_sequence(sequence);
}

// The output the replay reads, built on the CPU: each block as recorded, then
// the program at t (interp_gpu::evaluate).
void GxBackend::build_interp_reference(const InterpScene& scene, const interp_gpu::FrameTimes& times,
                                       std::vector<std::uint32_t>& out) const {
    const interp_gpu::Layout& layout = interp_layout_;
    out.assign(layout.bytes() / 4u, 0u);
    for (std::size_t s = 0; s < interp_moving_.size(); ++s) {
        std::memcpy(out.data() + layout.vs_offset(static_cast<std::uint32_t>(s)) / 4u,
                    &scene.constants[interp_moving_[s].record], sizeof(GxVsConstants));
    }
    for (std::size_t q = 0; q < interp_palette_entry_.size(); ++q) {
        const InterpPaletteMotion& entry = interp_palette_motions_[static_cast<std::size_t>(interp_palette_entry_[q])];
        const std::uint32_t* words = scene.palettes.data() + static_cast<std::size_t>(entry.shadow) * kMatrixPaletteWords;
        std::memcpy(out.data() + layout.palette_offset(static_cast<std::uint32_t>(q)) / 4u, words,
                    interp_gpu::kPaletteBytes);
        if (q < layout.prev_palette_count) {
            std::memcpy(out.data() + layout.prev_palette_offset(static_cast<std::uint32_t>(q)) / 4u, words,
                        interp_gpu::kPrevPaletteBytes);
        }
    }
    for (std::size_t k = 0; k < layout.ps_count; ++k) {
        std::memcpy(out.data() + layout.ps_offset(static_cast<std::uint32_t>(k)) / 4u,
                    &scene.ps_constants[interp_ps_moving_[k][0]], sizeof(GxPsConstants));
    }
    interp_gpu::evaluate(interp_program_, times, out.data());
}

// GALAXY_INTERP_VERIFY: at t = 0 every matched record's moving block must equal
// the previous frame's (positions, projection, texture matrices) and every
// palette with a previous snapshot its words.
void GxBackend::verify_interp_program(const InterpScene& scene, const InterpScene& before_scene,
                                      const Affine34& camera_t) {
    std::vector<std::uint32_t> out;
    interp_gpu::FrameTimes times{};
    times.camera = camera_t;
    times.camera_prev = camera_t;
    build_interp_reference(scene, times, out);
    const interp_gpu::Layout& layout = interp_layout_;
    std::size_t checked = 0u, bad = 0u, palette_words = 0u;
    float worst = 0.0f;
    const auto compare = [&worst](const float* x, const float* y, std::size_t n) {
        float local = 0.0f;
        for (std::size_t i = 0; i < n; ++i) {
            local = std::max(local, std::fabs(x[i] - y[i]) / (1.0f + std::fabs(y[i])));
        }
        worst = std::max(worst, local);
        return local;
    };
    for (std::size_t s = 0; s < interp_moving_.size(); ++s) {
        const InterpRecordMotion& moving = interp_moving_[s];
        if (moving.prev < 0) continue;
        const GxVsConstants& was = before_scene.constants[static_cast<std::size_t>(moving.prev)];
        GxVsConstants built{};
        std::memcpy(&built, out.data() + layout.vs_offset(static_cast<std::uint32_t>(s)) / 4u, sizeof(built));
        ++checked;
        float local = compare(&built.projection[0][0], &was.projection[0][0], 16u);
        local = std::max(local, compare(&built.inline_tex_matrices[0][0][0], &was.inline_tex_matrices[0][0][0],
                                        kMaxTexGens * 12u));
        if (scene.per_vertex[moving.record] == 0u) {
            local = std::max(local, compare(&built.inline_pos_matrix[0][0], &was.inline_pos_matrix[0][0], 12u));
        }
        bad += local > 1e-3f ? 1u : 0u;
    }
    for (std::size_t q = 0; q < interp_palette_entry_.size(); ++q) {
        const InterpPaletteMotion& entry = interp_palette_motions_[static_cast<std::size_t>(interp_palette_entry_[q])];
        if (entry.before < 0) continue;
        const std::uint32_t* was =
            before_scene.palettes.data() + static_cast<std::size_t>(entry.before) * kMatrixPaletteWords;
        const std::uint32_t* got = out.data() + layout.palette_offset(static_cast<std::uint32_t>(q)) / 4u;
        for (std::size_t w = 0; w < kMatrixPaletteWords; ++w) {
            const float x = std::bit_cast<float>(got[w]);
            const float y = std::bit_cast<float>(was[w]);
            palette_words += got[w] != was[w] && !(std::fabs(x - y) <= 1e-3f * (1.0f + std::fabs(y))) ? 1u : 0u;
        }
    }
    std::cerr << "[interp-verify] seq=" << interp_sequence_serial_ << " checked=" << checked << " bad=" << bad
              << " palette-words-off=" << palette_words << " worst=" << worst << '\n';
}

// GALAXY_INTERP_EVAL_CHECK=<n>: every n-th sequence the GPU evaluation of its
// first sub-frame is read back and compared with the CPU reference: copied
// words exactly, each op's output to float precision of its magnitude
// (rounded colours and integers within one step).
void GxBackend::check_interp_eval(const InterpScene& scene, const Affine34& camera_t, float t,
                                  const Affine34& camera_prev, float t_prev) {
    static const std::uint64_t every = read_env_u64("GALAXY_INTERP_EVAL_CHECK", 0u);
    if (every == 0u) {
        return;
    }
    std::vector<std::uint32_t> got;
    float got_t = 0.0f;
    if (renderer_.take_eval_readback(got, got_t) && !interp_eval_expected_.empty()) {
        const std::vector<std::uint32_t>& want = interp_eval_expected_;
        const std::size_t words = std::min(got.size(), want.size());
        std::vector<std::uint8_t> covered(words, 0u);
        std::size_t ops_off = 0u, copies_off = 0u;
        float worst = 0.0f;
        std::ostringstream detail;
        unsigned listed = 0u;
        for (const interp_gpu::Op& op : interp_eval_ops_) {
            const std::uint32_t kind = interp_gpu::op_kind(op.control);
            const std::uint32_t n = kind == 0u ? 12u : kind == 1u ? 9u : kind == 5u ? 6u : kind == 4u ? 1u
                                                                                    : interp_gpu::op_count(op.control);
            if (op.dst + n > words) continue;
            float magnitude = 0.0f, error = 0.0f;
            bool step = false;
            for (std::uint32_t i = 0; i < n; ++i) {
                const std::uint32_t x = got[op.dst + i], y = want[op.dst + i];
                covered[op.dst + i] = 1u;
                if (kind == 4u) {
                    for (unsigned shift = 0; shift < 32u; shift += 8u) {
                        const int dx = static_cast<int>((x >> shift) & 0xFFu) - static_cast<int>((y >> shift) & 0xFFu);
                        step = step || dx > 1 || dx < -1;
                    }
                } else if (kind == 3u) {
                    const std::int64_t d = static_cast<std::int64_t>(static_cast<std::int32_t>(x)) -
                                           static_cast<std::int32_t>(y);
                    step = step || d > 1 || d < -1;
                } else if (kind != 6u) {
                    magnitude = std::max(magnitude, std::fabs(std::bit_cast<float>(y)));
                    error = std::max(error, std::fabs(std::bit_cast<float>(x) - std::bit_cast<float>(y)));
                } else {
                    step = step || x != y;
                }
            }
            const float relative = error / (1.0f + magnitude);
            if (step || !(relative <= 2e-5f)) {
                ++ops_off;
                worst = std::max(worst, step ? 1.0f : relative);
                if (listed++ < 6u) {
                    detail << " op(kind=" << kind << " dst=" << op.dst << " rel=" << relative << ')';
                }
            }
        }
        for (std::size_t w = 0; w < words; ++w) {
            if (covered[w] == 0u && got[w] != want[w]) {
                if (copies_off++ < 4u) {
                    detail << " copy(word=" << w << ' ' << got[w] << "!=" << want[w] << ')';
                }
            }
        }
        std::cerr << "[interp-eval-check] seq=" << interp_eval_expected_seq_ << " t=" << got_t << " words=" << words
                  << " ops=" << interp_eval_ops_.size() << " ops-off=" << ops_off << " copies-off=" << copies_off
                  << " worst=" << worst << detail.str() << '\n';
        interp_eval_expected_.clear();
    }
    if (interp_sequence_.next == 0u && interp_eval_expected_.empty() && interp_sequence_serial_ % every == 0u) {
        interp_gpu::FrameTimes times{};
        times.camera = camera_t;
        times.t = t;
        times.camera_prev = camera_prev;
        times.t_prev = t_prev;
        times.previous_only = t >= 1.0f;
        build_interp_reference(scene, times, interp_eval_expected_);
        interp_eval_expected_seq_ = interp_sequence_serial_;
        interp_eval_ops_ = interp_program_.ops;
        renderer_.request_eval_readback();
    }
}

bool GxBackend::skip_for_present_rate() {
    const RenderConfig cfg = get_render_config();
    const double rate = present_frame_rate(cfg.max_fps, cfg.vsync_interval, renderer_.display_refresh_hz());
    if (rate >= 59.0) {
        next_rate_present_qpc_ = 0;
        return false;
    }
    static const std::int64_t frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return static_cast<std::int64_t>(value.QuadPart);
    }();
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const auto period = static_cast<std::int64_t>(static_cast<double>(frequency) / rate);
    // A stall restarts the cadence instead of releasing a burst of presents.
    if (next_rate_present_qpc_ == 0 || now.QuadPart - next_rate_present_qpc_ > period) {
        next_rate_present_qpc_ = now.QuadPart;
    }
    if (now.QuadPart + period / 4 < next_rate_present_qpc_) return true;
    next_rate_present_qpc_ += period;
    return false;
}

void GxBackend::present_cached_xfb(
    std::uint32_t displayed_xfb_addr,
    bool present_swap_chain) {
    if (!initialized_ || !present_swap_chain) {
        return;
    }

    if (async_render_thread_enabled()) {
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
        return;
    }

    present_cached_xfb_on_thread(
        displayed_xfb_addr,
        false);
}

// Consumer loop (render thread): frames execute in submission order; queued
// frames are drained before a stop so the last presents land.
void GxBackend::render_thread_main() {
    galaxy::host::name_current_thread(L"Nebula GX Render");
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    // Hybrid CPUs: keep the FIFO parser on performance cores with the
    // simulation thread; the scheduler still balances inside that set.
    // Opt-in together with the simulation thread's placement.
    if (read_env_u64("GALAXY_PIN_PERFORMANCE_CORES", 0u) != 0u) {
        (void)galaxy::host::pin_current_thread_to_performance_cores();
    }
    {
        // Opt the whole process out of EcoQoS / execution-speed throttling so
        // hybrid CPUs (Intel P/E/LP-E cores, laptops) do not park the
        // simulation, render or DSP threads on efficiency cores. Once only.
        static std::once_flag power_throttling_once;
        std::call_once(power_throttling_once, [] {
            galaxy::host::opt_out_of_power_throttling_process();
            // Request 1 ms timer granularity. Without it Sleep/cv timeouts and
            // waitable timers round to the 15.6 ms scheduler tick whenever no
            // other process has raised it, which delays render-thread fence
            // polling and every sub-frame wait by up to a full tick.
            if (HMODULE winmm = LoadLibraryW(L"winmm.dll")) {
                using TimeBeginPeriodFn = UINT(WINAPI*)(UINT);
                if (auto begin = reinterpret_cast<TimeBeginPeriodFn>(
                        GetProcAddress(winmm, "timeBeginPeriod"))) {
                    (void)begin(1);
                }
            }
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
        std::uint64_t shutdown_fence = 0;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            const RenderConfig cfg = get_render_config();
            idle_present = cfg.frame_rate_decouple &&
                (cfg.vsync_interval != 0u || cfg.max_fps > 0.0f);
            const bool poll_frame_effects = !pending_frame_effects_.empty();
            const bool poll_pointer_depth =
                static_cast<bool>(pending_pointer_depth_capture_.capture) ||
                renderer_.any_async_peek_field_pending();
            // Draining for a stop: queued frames present without sub-frames.
            interp_stopping_ = stop_render_thread_;
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
            } else if (idle_present || poll_frame_effects || poll_pointer_depth) {
                // Completion polling occurs outside queue_mutex_ at the top of
                // the worker loop. Yielding under an empty queue without checking
                // a fence cannot observe an imminent completion; it only delays
                // that next poll. Park once, with newly submitted work and shutdown
                // still interrupting the wait through the existing predicate.
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
                    if (interp_recording_) [[unlikely]] {
                        interp_event("merged-present", present.displayed_xfb_addr, chunk.fifo.size());
                    }
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
                    if (interp_recording_) [[unlikely]] {
                        interp_event("vi-present", chunk.displayed_xfb_addr,
                                     interp_log_waiting_ ? 1u : 0u);
                    }
                    if (interp_log_waiting_ && renderer_.replay_available()) {
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
                    prepare_worker_memory_snapshot(chunk);
                    if (const RenderStallInjection& stall = render_stall_injection(); stall.ms != 0u) {
                        static std::uint32_t chunks = 0u;
                        if (++chunks % stall.every == 0u) {
                            Sleep(stall.ms);
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
            // Every return path funnels through the two blocks below, so the
            // pool caps live here rather than at each push site. Dropping a
            // buffer only costs one re-allocation the next time the queue grows
            // past it; retaining it past the in-flight bound keeps its peak
            // capacity for the rest of the session. See
            // kChunkPoolRetainedBuffers in gx_backend.h.
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
                // Guest bytes are consumed synchronously while command lists
                // are recorded. GPU work references only upload/default-heap
                // resources whose frame slots retire on their exact fence.
                chunk.memory_snapshot.immutable_ranges.clear();
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

bool GxBackend::peek_efb_published(
    std::uint16_t x,
    std::uint16_t y,
    EfbPeekKind kind,
    std::uint32_t& value) {
    if (!initialized_ || !async_render_thread_enabled() ||
        x >= kEfbWidth || y >= kEfbHeight) {
        return false;
    }
    AsyncPeekFieldState& state = async_peek_[async_peek_kind_index(kind)];
    state.requested.store(true, std::memory_order_release);
    const int slot = state.published.load(std::memory_order_acquire);
    if (slot < 0) {
        return false;
    }
    const std::uint8_t* field = state.rgba[static_cast<std::size_t>(slot)];
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

void GxBackend::poll_async_peek_fields() {
    for (std::size_t index = 0; index < async_peek_.size(); ++index) {
        const EfbPeekKind kind = async_peek_kind_at(index);
        if (!renderer_.async_peek_field_pending(kind)) {
            continue;
        }
        AsyncPeekFieldState& state = async_peek_[index];
        const int slot = state.pending_slot;
        if (slot < 0) {
            continue;
        }
        if (renderer_.try_complete_async_peek_field(kind)) {
            state.pending_slot = -1;
            ++state.completed;
            state.published.store(slot, std::memory_order_release);
        }
    }
}

void GxBackend::submit_async_peek_fields_if_requested() {
    for (std::size_t index = 0; index < async_peek_.size(); ++index) {
        const EfbPeekKind kind = async_peek_kind_at(index);
        if (kind == EfbPeekKind::Color && color_field_at_tokens_) {
            continue;
        }
        AsyncPeekFieldState& state = async_peek_[index];
        if (!state.requested.load(std::memory_order_acquire)) {
            continue;
        }
        if (renderer_.async_peek_field_pending(kind)) {
            // Keep the request alive for the next rendered chunk.
            continue;
        }
        state.requested.store(false, std::memory_order_relaxed);
        // Never the published slot: a reader may be converting its pixel.
        const int published = state.published.load(std::memory_order_relaxed);
        const int slot = published < 0 ? 0 : (published + 1) % 3;
        if (renderer_.submit_async_peek_field(kind, static_cast<std::size_t>(slot)) != 0u) {
            // The slot pointers are fixed once the resources exist; record
            // them before the first publication so readers never race a write.
            if (state.rgba[0] == nullptr) {
                for (std::size_t i = 0; i < state.rgba.size(); ++i) {
                    state.rgba[i] = renderer_.async_peek_field_rgba(kind, i);
                }
            }
            state.pending_slot = slot;
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
            static const bool defer_pointer_depth =
                read_env_u64("GALAXY_ASYNC_POINTER_DEPTH_READBACK", 0u) != 0u;
            if (defer_pointer_depth) {
                const auto fence_value = renderer_.submit_pointer_depth_capture();
                if (fence_value == 0u) {
                    throw std::runtime_error("owned pointer depth submission failed");
                }
                const std::lock_guard<std::mutex> lock(queue_mutex_);
                pending_pointer_depth_capture_ = {
                    chunk.pointer_depth_capture, fence_value, start};
                return;
            }
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
        // GALAXY_TRACE_PEEKS=1: every EFB peek with the scene state it read (diagnostics).
        static const bool trace_peeks = read_env_u64("GALAXY_TRACE_PEEKS", 0u) != 0u;
        if (trace_peeks && request->depth_pixels.empty()) [[unlikely]] {
            std::fprintf(stderr, "[peek] frame=%llu scene=%d phase=%u layer=%u kind=%u x=%u y=%u value=%08x ok=%d\n",
                         static_cast<unsigned long long>(renderer_.frame_index()), scene_phase_ ? 1 : 0,
                         static_cast<unsigned>(rt_phase_), side_layer_, static_cast<unsigned>(request->kind),
                         request->x, request->y, value, success ? 1 : 0);
        }
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

void GxBackend::prepare_worker_memory_snapshot(FrameChunk& chunk) {
    if (!chunk.worker_memory_snapshot) {
        return;
    }
    if (!chunk.cpu_reads_pending || chunk.memory == nullptr ||
        chunk.memory_snapshot_active) {
        throw std::logic_error(
            "[GxBackend] invalid worker memory snapshot ownership transition");
    }

    // The CPU owner is still blocked by wait_for_render_cpu_reads_idle(). No
    // live reads may remain after the release below. A failed scan/copy leaves
    // that obligation pending and propagates through render_thread_error_;
    // it never resumes using a partial snapshot or falls back to live RAM.
    const bool trace = trace_gx_stats_enabled() || trace_gx_stalls_enabled();
    const auto scan_start = trace ? std::chrono::steady_clock::now()
                                 : std::chrono::steady_clock::time_point{};
    {
        const cadence::ScopedPhaseTimer scan_timer{
            cadence_diagnostics_, cadence::TimingPhase::WorkerDependencyScan};
        invalidate_dependency_range_cache(chunk.dirty_ranges);
        invalidate_immutable_range_cache(chunk.dirty_ranges);
        {
            std::lock_guard<std::mutex> parser_lock(parser_cache_mutex_);
            invalidate_dirty_display_list_ranges(parser_, chunk.dirty_ranges);
        }
        // The decoded-packet-run vertex cache is keyed on the display-list token. The display lists were
        // invalidated above, so entries reachable from them are already unreachable. Invalidating the
        // decoded cache here too keeps the per-generation snapshot assert valid in this mode. Without the
        // generation bump, an entry captured against the previous snapshot could be marked valid for the
        // new one and skip its byte compare. The range list is already built, so this is cheap, and both
        // invalidation paths stay symmetric.
        (void)invalidate_dirty_decoded_packet_run_ranges(chunk.dirty_ranges);
        collect_memory_dependency_ranges(
            chunk.fifo.data(), chunk.fifo.size(), chunk.memory,
            chunk.memory_ranges, chunk.dependency_cache_hit);
    }
    if (trace) {
        chunk.dependency_scan_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - scan_start).count());
    }
    chunk.snapshot_range_count = chunk.memory_ranges.size();
    chunk.snapshot_range_bytes = guest_memory_range_bytes(chunk.memory_ranges);
    chunk.memory_snapshot_strict = true;
    const auto copy_start = trace ? std::chrono::steady_clock::now()
                                 : std::chrono::steady_clock::time_point{};
    {
        const cadence::ScopedPhaseTimer copy_timer{
            cadence_diagnostics_, cadence::TimingPhase::WorkerSnapshotCopy};
        capture_memory_snapshot(
            chunk.memory, chunk.memory_snapshot, chunk.memory_ranges, true, false);
    }
    if (!chunk.memory_snapshot.sealed ||
        chunk.memory_snapshot.memory.flat_guest_read_base != nullptr) {
        throw std::runtime_error(
            "[GxBackend] worker memory snapshot was not detached and sealed");
    }
    if (trace) {
        chunk.snapshot_ns = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - copy_start).count());
    }
    chunk.snapshot_bytes = memory_snapshot_used_bytes(chunk.memory_snapshot);
    chunk.snapshot_reused_bytes = chunk.memory_snapshot.reused_bytes;
    if (trace_gx_stats_enabled()) {
        stat_dependency_scan_ns_.fetch_add(
            chunk.dependency_scan_ns, std::memory_order_relaxed);
        stat_snapshot_ns_.fetch_add(chunk.snapshot_ns, std::memory_order_relaxed);
        stat_snapshot_bytes_.fetch_add(
            chunk.snapshot_bytes, std::memory_order_relaxed);
        stat_snapshot_ranges_.fetch_add(
            static_cast<std::uint64_t>(chunk.snapshot_range_count),
            std::memory_order_relaxed);
    }
    // The queue moves this chunk before sealing. From this point its view and
    // owned storage remain on the worker until synchronous command recording
    // finishes; only then are their vectors recycled under queue_mutex_. GPU
    // resources and PE completion still retire through the existing fences.
    chunk.memory = &chunk.memory_snapshot.memory;
    chunk.memory_snapshot_active = true;
    chunk.immutable_range_ownership = false;
    // A worker snapshot did not exist at producer submission. Classifying
    // live CALL_DL bytes there could publish an event-free receipt for a
    // different view. Classification and rendering consume the sealed bytes.
    if (get_gx_timing_config().pe_events_gpu_fence) {
        if (display_list_dirty_invalidate_enabled()) {
            for (const GuestWriteRange& range : chunk.dirty_ranges) {
                (void)pe_event_classifier_.invalidate_display_list_cache_range(
                    range.guest_addr, range.size);
            }
        }
        const auto classify_start = trace
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        pe_event_classifier_.classify(
            chunk.fifo.data(), chunk.fifo.size(), chunk.memory,
            chunk.preclassified_pe_events,
            chunk.memory_snapshot_active && chunk.memory_snapshot.sealed &&
                chunk.memory == &chunk.memory_snapshot.memory &&
                chunk.memory_snapshot.regions_disjoint_sorted);
        chunk.pe_events_preclassified = true;
        if (trace) {
            chunk.pe_classification_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - classify_start).count());
        }
    }
    if (chunk.worker_submission_token) {
        if (!chunk.pe_events_preclassified || chunk.pe_completion_token) {
            throw std::logic_error("worker snapshot receipt has invalid ownership");
        }
        const std::lock_guard<std::mutex> lock(queue_mutex_);
        chunk.pe_event_free_receipt = chunk.preclassified_pe_events.empty();
        chunk.pe_completion_token = chunk.pe_event_free_receipt
            ? frame_pe_completions_.issue_event_free()
            : frame_pe_completions_.issue();
        chunk.pe_completion_requires_resolution = !chunk.pe_event_free_receipt;
        if (chunk.pe_completion_requires_resolution) {
            ++render_fifo_effects_in_flight_;
            chunk.fifo_effects_pending = true;
        }
        *chunk.worker_submission_token = chunk.pe_completion_token;
        chunk.worker_submission_token.reset();
    }
    mark_frame_cpu_reads_done(chunk);
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
        // No guest-visible PE signal exists for this capture. This runs after close, ordered queue
        // submission and queue signal succeed, and before optional presentation. Resolve at that
        // submission boundary. Later work is queue-ordered, so no GPU-idle wait is needed.
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
    const NativeServicesV1* services,
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
    const std::uint64_t present_requests =
        stat_xfb_present_requests_.load(std::memory_order_relaxed);
    const bool display_xfb_unknown =
        displayed_xfb_addr == kUnknownDisplayedXfbAddr;
    const bool display_xfb_none =
        displayed_xfb_addr == kNoDisplayedXfbAddr;
    const bool requested_specific_xfb =
        displayed_xfb_addr != 0u && !display_xfb_unknown &&
        !display_xfb_none;
    const XfbTexture* latest = efb_copies_.latest();
    const std::uint32_t last_presented_addr =
        last_presented_xfb_addr_.load(std::memory_order_relaxed);
    const XfbTexture* last_valid = last_presented_addr != 0u
        ? efb_copies_.find_xfb(last_presented_addr)
        : nullptr;
    const XfbTexture* selected = display_xfb_none
        ? nullptr
        : (requested_specific_xfb
            ? efb_copies_.find_xfb(displayed_xfb_addr)
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
            max_xfb_preserve_age(),
            last_presented_xfb_stamp_.load(std::memory_order_relaxed));
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
        if (count_game_xfb_present && requested_specific_xfb &&
            trace_xfb_present_enabled()) {
            std::cerr << "[xfb-present] frame=" << frame_index_
                      << " request=" << present_requests
                      << " missing=1"
                      << " selected=0x" << std::hex << displayed_xfb_addr
                      << " latest=0x"
                      << (latest != nullptr ? latest->guest_addr : 0u)
                      << std::dec
                      << " misses="
                      << stat_xfb_present_misses_.load(
                             std::memory_order_relaxed)
                      << " stale-fallbacks="
                      << stat_xfb_present_stale_fallbacks_.load(
                             std::memory_order_relaxed)
                      << " missing-fallbacks="
                      << stat_xfb_present_missing_fallbacks_.load(
                             std::memory_order_relaxed)
                      << " missing-preserved="
                      << stat_xfb_present_missing_preserved_.load(
                             std::memory_order_relaxed)
                       << " repeated-or-stale-serial-requests="
                       << stat_xfb_repeated_or_stale_serial_requests_.load(
                             std::memory_order_relaxed)
                      << " stale-expired="
                      << (selection.stale_expired ? 1 : 0)
                      << " missing-expired="
                      << (selection.missing_expired ? 1 : 0)
                      << '\n';
        }
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
        skip_duplicate_xfb_presents_enabled() &&
        !native_settings_overlay_visible();
    if (count_game_xfb_present && !skip_duplicate_present &&
        !native_settings_overlay_visible() && skip_for_present_rate()) {
        return;
    }
    if (count_game_xfb_present && trace_xfb_present_enabled() &&
        (present_requests <= 8u ||
         present_requests % trace_xfb_present_interval() == 0u)) {
        std::cerr << "[xfb-present] frame=" << frame_index_
                  << " request=" << present_requests
                  << " selected=0x" << std::hex << displayed_xfb_addr
                  << " registry=0x" << selected->guest_addr
                  << " latest=0x"
                  << (latest != nullptr ? latest->guest_addr : 0u)
                  << std::dec
                  << " requested-stale="
                  << (selection.requested_stale ? 1 : 0)
                  << " stale-fallback=0"
                  << " stale-preserved="
                  << (selection.stale_preserved ? 1 : 0)
                  << " missing-fallback=0"
                  << " missing-preserved="
                  << (selection.missing_preserved ? 1 : 0)
                  << " stamp=" << selected->frame_stamp
                  << " serial=" << selected->copy_serial
                  << " duplicate=" << (duplicate_stamp ? 1 : 0)
                  << " duplicate-skip="
                  << (skip_duplicate_present ? 1 : 0)
                  << " misses="
                  << stat_xfb_present_misses_.load(
                         std::memory_order_relaxed)
                  << " stale-fallbacks="
                  << stat_xfb_present_stale_fallbacks_.load(
                         std::memory_order_relaxed)
                  << " missing-fallbacks="
                  << stat_xfb_present_missing_fallbacks_.load(
                         std::memory_order_relaxed)
                  << " stale-preserved-total="
                  << stat_xfb_present_stale_preserved_.load(
                         std::memory_order_relaxed)
                  << " missing-preserved-total="
                  << stat_xfb_present_missing_preserved_.load(
                         std::memory_order_relaxed)
                   << " repeated-or-stale-serial-requests="
                   << stat_xfb_repeated_or_stale_serial_requests_.load(
                         std::memory_order_relaxed)
                  << '\n';
    }
    if (skip_duplicate_present) {
        record_skipped_duplicate_xfb_present(*selected);
        if (trace_xfb_present_enabled()) {
            std::cerr << "[xfb-present] frame=" << frame_index_
                      << " request=" << present_requests
                      << " duplicate-skip=1"
                      << " selected=0x" << std::hex << displayed_xfb_addr
                      << " registry=0x" << selected->guest_addr
                      << std::dec
                      << " stamp=" << selected->frame_stamp
                      << " serial=" << selected->copy_serial
                      << " duplicate=1"
                       << " repeated-or-stale-serial-requests="
                       << stat_xfb_repeated_or_stale_serial_requests_.load(
                             std::memory_order_relaxed)
                      << " misses="
                      << stat_xfb_present_misses_.load(
                             std::memory_order_relaxed)
                      << " skipped-duplicates="
                      << stat_xfb_present_skipped_duplicate_presents_.load(
                             std::memory_order_relaxed)
                      << '\n';
        }
        return;
    }
    // Capture an already selected, normally presented XFB. Render-chunk EFB
    // diagnostics can show intermediate work and must not serve as proof of
    // what the VI presentation actually contained. This opt-in readback does
    // not add a presentation or change framebuffer selection/ownership.
    const bool capture_present = count_game_xfb_present &&
        capture_present_requested(present_requests);
    const auto capture_begin = capture_present
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    if (capture_present) {
        const std::string path = numbered_capture_path(
            "GALAXY_GX_CAPTURE_PRESENT_PPM", present_requests);
        renderer_.debug_log_backbuffer_readback();
        renderer_.set_debug_capture_paths({}, path);
    }
    const std::string snapshot_path = !interp_snapshot_path_.empty() ? interp_snapshot_path_
        : (capture_present || !count_game_xfb_present) ? std::string{} : present_snapshot_path();
    if (!snapshot_path.empty()) {
        renderer_.set_debug_capture_paths({}, snapshot_path);
    }
    if (interp_recording_ && count_game_xfb_present && !interp_presenting_real_ &&
        !duplicate_stamp) {
        note_interp_real_present();
    }
    bool capture_recorded = false;
    bool replayed = false;
    if (interp_presenting_real_ && renderer_.temporal_interpolation()) {
        // The temporal upscaler takes every presented frame from the replays:
        // the sequence's frame at t = 1 too (one history).
        const bool capture = capture_present || !snapshot_path.empty();
        renderer_.set_subframe_capture(capture);
        replayed = renderer_.replay_playing() && render_interp_subframe(1.0f);
        renderer_.set_subframe_capture(false);
        capture_recorded = replayed && capture;
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
        // Present-only frames do not acquire or resize XFB textures. Keep
        // EfbCopyManager retirement/recycling tied to render frames that can
        // actually produce GXCopyDisp resources; otherwise the split
        // render-then-present cadence advances XFB lifetime twice per VI.
        capture_recorded = renderer_.present(
            selected, capture_present || !snapshot_path.empty());
    }
    if (!snapshot_path.empty()) {
        renderer_.set_debug_capture_paths({}, {});
    }
    if (!replayed) {
        renderer_.end_frame();
    }
    if (capture_present) {
        renderer_.set_debug_capture_paths({}, {});
        std::cerr << "[gx-present-capture] request=" << present_requests
                  << " capture-recorded=" << capture_recorded
                  << " xfb=0x" << std::hex << selected->guest_addr << std::dec
                  << " serial=" << selected->copy_serial
                  << " stamp=" << selected->frame_stamp
                  << " begin-steady-ns=" <<
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                capture_begin.time_since_epoch()).count()
                  << " present-return-steady-ns=" <<
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count()
                  << " boundary=normal-present-only physical-display-time=unmeasured\n";
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
    // GALAXY_DIAG_RENDER_BURN_US: busy CPU time per render chunk (diagnostic,
    // emulates a slower render thread), starting GALAXY_DIAG_RENDER_BURN_AFTER_MS
    // after the first render chunk (default 0).
    {
        const auto read_u64 = [](const char* name) {
            char value[24]{};
            std::size_t length = 0;
            if (getenv_s(&length, value, sizeof(value), name) != 0 || length <= 1u) {
                return std::uint64_t{0};
            }
            return static_cast<std::uint64_t>(std::strtoull(value, nullptr, 10));
        };
        static const std::uint64_t burn_us = read_u64("GALAXY_DIAG_RENDER_BURN_US");
        static const std::uint64_t burn_after_ms = read_u64("GALAXY_DIAG_RENDER_BURN_AFTER_MS");
        static const auto first_chunk = std::chrono::steady_clock::now();
        if (burn_us != 0u &&
            std::chrono::steady_clock::now() - first_chunk >= std::chrono::milliseconds(burn_after_ms)) {
            const auto until = std::chrono::steady_clock::now() + std::chrono::microseconds(burn_us);
            while (std::chrono::steady_clock::now() < until) {
                _mm_pause();
            }
        }
    }
    render_pointer_response_=chunk.pointer_response;
    render_movie_content_=chunk.prerecorded_movie;
    render_safety_surround_=chunk.safety_surround;
    const bool trace_gx_stalls = trace_gx_stalls_enabled();
    const bool trace_gx_microprofile = trace_gx_microprofile_enabled();
    const bool trace_gx_stats = trace_gx_stats_enabled();
    const bool defer_pe_events_to_gpu_fence =
        get_gx_timing_config().pe_events_gpu_fence;
    frame_microprofile_enabled_ = trace_gx_microprofile &&
        ((frame_index_ + 1u) % trace_gx_microprofile_interval() == 0u);
    const bool frame_profile_enabled =
        frame_microprofile_enabled_ || trace_gx_stalls;
    // One decision per frame for the per-draw clock reads. Draw-path sites read frame_time_detail_
    // instead of re-evaluating `frame_microprofile_enabled_ || trace_gx_stalls_enabled()` for every
    // draw.
    //
    // Default behavior: fully on under an intrusive run, fully off otherwise. A requested sample
    // interval also enables it for one frame in N, so a routine recording contains some fully
    // attributed frames while the rest run at production speed.
    frame_time_sample_ = frame_gx_timing_sample_enabled(frame_index_);
    frame_time_detail_ =
        frame_microprofile_enabled_ || trace_gx_stalls || frame_time_sample_;
    // CPU-cycle witness for the `flush-pso` window. It is independent of `frame_time_detail_`:
    // enabling wall-clock per-draw instrumentation to get cycle data would distort the wall clock the
    // cycles are meant to interpret. Off unless requested. `GALAXY_GX_PSO_CYCLES=1` turns it on, and
    // so does detailed mode, since a run already paying for trace_gx_stalls may as well carry the
    // witness.
    pso_cycles_enabled = pso_cycles_requested() || trace_gx_stalls;
    // Routine monitoring must not need the instrumentation it measures. Per-frame phase timing is
    // about 14 steady_clock reads; per-draw instrumentation is thousands. Sampling is therefore
    // always on, while the line that prints it stays bounded by the deferred stats ring or the
    // explicit stats flag.
    constexpr bool frame_timing_enabled = true;
    // Kernel queries are intrusive diagnostics, not routine frame monitoring.
    // Use the existing explicit cycle/stall opt-in for both counters.
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
    // frame_flush_timed_calls_ is not reset here. It is the sampling counter for `ScopedFlushTiming`,
    // whose stride is 64, and a frame makes fewer flush calls than that. Resetting it per frame would
    // leave `call % 64 == 0` true only for call 0: every frame would time exactly one flush, always
    // the first, and `flush-mean-us` would describe the first flush of each frame rather than a
    // typical batch. Kept monotonic, the counter walks every residue mod 64 across the session, so
    // samples spread over call positions regardless of frame size.
    frame_pso_memo_hits_ = 0;
    frame_pso_get_calls_ = 0;
    frame_pso_get_us_ = 0;
    frame_pso_get_max_us_ = 0;
    frame_flush_calls_ = 0;
    frame_pso_resolutions_ = 0;
    frame_pso_route_early_ = 0;
    // Baseline for the per-frame pipeline-compile deltas published on the
    // `[gx-frame-timing]` line. `compile_stats()` is clock-free and reads
    // monotonic counters (two of them atomics, because prewarm workers write
    // them too), so this is one copy in the frame reset and no per-draw cost.
    frame_compile_stats_baseline_ = pipeline_cache_.compile_stats();
    frame_replay_prepared_batches_ = 0;
    frame_replay_packet_batches_ = 0;
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
    frame_decoded_vertex_cache_hits_ = 0;
    frame_decoded_vertex_cache_misses_ = 0;
    frame_resident_vertex_hits_ = 0;
    frame_resident_vertex_bytes_ = 0;
    frame_decoded_vertex_cache_evictions_ = 0;
    frame_decoded_vertex_cache_stale_rejects_ = 0;
    pending_draw_batch_ = {};
    frame_fifo_profile_.reset();
    const std::byte* fifo_data =
        chunk.fifo.empty() ? nullptr : chunk.fifo.data();
    const std::size_t fifo_size = chunk.fifo.size();
    GuestMemoryV1* memory = chunk.memory_snapshot_active
        ? &chunk.memory_snapshot.memory
        : chunk.memory;
    const NativeServicesV1* services = chunk.services;
    coalesce_dirty_ranges(chunk.dirty_ranges);
    chunk.dirty_range_count = chunk.dirty_ranges.size();
    chunk.dirty_bytes = dirty_range_bytes(chunk.dirty_ranges);

    // begin_frame() drains PSO completions and rewinds ring segments.
    {
        const cadence::ScopedPhaseTimer render_begin_timer{
            cadence_diagnostics_, cadence::TimingPhase::RenderBegin};
        {
            const RenderConfig render = get_render_config();
            // A lighting-mode change re-keys every lit draw's pipeline.
            if (render.per_pixel_lighting != per_pixel_lighting_) {
                per_pixel_lighting_ = render.per_pixel_lighting;
                shader_key_hashes_valid_ = false;
                current_pipeline_ = nullptr;
            }
        }
        renderer_.begin_frame(chunk.present_swap_chain);
        {
            // Interpolated redraw: present rate above 60 with the temporal
            // upscaler off. Decided per render chunk.
            const RenderConfig cfg = get_render_config();
            const double rate = present_frame_rate(
                cfg.max_fps, cfg.vsync_interval, renderer_.display_refresh_hz());
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
            // GALAXY_DRAW_IDENTITY=0 never asks the game for draw identities,
            // =1 always does (tests at 60).
            static const std::uint64_t identity_env = read_env_u64("GALAXY_DRAW_IDENTITY", 2u);
            const bool wanted = identity_env == 2u
                ? (interp_recording_ || renderer_.temporal_mode() || renderer_.rt_active())
                : identity_env != 0u;
            if (draw_identity_wanted_.load(std::memory_order_relaxed) != wanted) {
                draw_identity_wanted_.store(wanted, std::memory_order_relaxed);
            }
            interp_scenes_in_chunk_ = 0u;
        }
        // begin_frame() applies a pending internal-resolution change, so copies must use the scale the
        // EFB actually has. Reading the setting separately could race a change made in between and size
        // one frame's copies for the other scale.
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
        texture_cache_.set_upload_list(renderer_.command_list());
    }
    // Workers may be filling this frame's texture staging from the chunk's
    // memory snapshot; no exit from this function may leave them running.
    struct TextureDecodeJoin {
        TextureCache& cache;
        ~TextureDecodeJoin() { cache.finish_pending_decodes_noexcept(); }
    } texture_decode_join{texture_cache_};
    if (diagnostic_texture_invalidate_requested(frame_index_ + 1u)) {
        texture_cache_.invalidate_all();
        texture_handle_cache_.clear();
        clear_texture_binding_table_cache();
        texture_bindings_dirty_ = true;
        std::cerr << "[gx-diagnostic] invalidated decoded texture cache at frame "
                  << (frame_index_ + 1u) << '\n';
    }
    std::size_t dirty_texture_evictions = 0u;
    // Include the immediately preceding chunk: a selected XFB can have been
    // copied before the requested readback chunk. Keep the existing markers
    // bounded to this small neighborhood, without per-primitive scene logs.
    poll_live_capture_request(frame_index_ + 1u, fifo_size);
    capture_composites_ = capture_composites_only_enabled() &&
        (capture_frame_requested(frame_index_ + 1u) ||
         capture_frame_requested(frame_index_ + 2u));
    texture_cache_.set_debug_capture_frame(
        capture_composites_ ||
            (capture_draw_log_enabled() && capture_frame_requested(frame_index_ + 1u))
            ? frame_index_ + 1u : 0u);
    std::size_t dirty_display_list_invalidations = 0u;
    std::size_t dirty_decoded_vertex_invalidations = 0u;
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
        {
            const cadence::ScopedPhaseTimer timer{
                cadence_diagnostics_, cadence::TimingPhase::RenderDisplayListInvalidation};
            dirty_display_list_invalidations =
                invalidate_dirty_display_list_ranges(parser_, chunk.dirty_ranges);
        }
        {
            const cadence::ScopedPhaseTimer timer{
                cadence_diagnostics_, cadence::TimingPhase::RenderDecodedVertexInvalidation};
            dirty_decoded_vertex_invalidations =
                invalidate_dirty_decoded_packet_run_ranges(chunk.dirty_ranges);
        }
    }
    if (trace_gx_stats) {
        stat_dirty_ranges_.fetch_add(
            static_cast<std::uint64_t>(chunk.dirty_range_count),
            std::memory_order_relaxed);
        stat_dirty_bytes_.fetch_add(
            chunk.dirty_bytes,
            std::memory_order_relaxed);
        stat_dirty_texture_evictions_.fetch_add(
            dirty_texture_evictions,
            std::memory_order_relaxed);
        stat_dirty_display_list_invalidations_.fetch_add(
            dirty_display_list_invalidations,
            std::memory_order_relaxed);
    }
    if (dirty_texture_evictions != 0u && trace_gx_stats) {
        std::cerr << "[gx-texture-dirty] frame=" << (frame_index_ + 1u)
                  << " ranges=" << chunk.dirty_ranges.size()
                  << " bytes=" << chunk.dirty_bytes
                  << " evicted=" << dirty_texture_evictions << '\n';
    }
    if (dirty_display_list_invalidations != 0u && trace_gx_stats) {
        std::cerr << "[gx-display-list-dirty] frame=" << (frame_index_ + 1u)
                  << " ranges=" << chunk.dirty_ranges.size()
                  << " bytes=" << chunk.dirty_bytes
                  << " invalidated=" << dirty_display_list_invalidations
                  << '\n';
    }
    // Every transient GPU address/table below belongs to the ring segment
    // selected by begin_frame(). The persistent GX register state may be
    // unchanged, but those bindings must be materialized in this frame slot.
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

    // GX semantics: the EFB persists across frames and is cleared only by an EFB copy with the clear
    // bit set (handled in on_efb_copy with the latched copy-clear registers). Clearing every frame
    // with stale latched colors caused purple, blue and white frame flicker. One initial clear
    // (depth = FAR) seeds the targets.
    if (!efb_seeded_) {
        efb_seeded_ = true;
        renderer_.clear_efb(0u, 0u, 0x00FFFFFFu, true, true, true);
    }

    // Opt-in GPU capture. The default path performs no readback or
    // synchronization. GALAXY_GX_CAPTURE_FRAME=N records one frame; diagnostic
    // runs can use GALAXY_GX_CAPTURE_FRAMES=A,B,C to write numbered PPMs.
    const bool capture_this_frame =
        capture_pixel_frame_requested(frame_index_ + 1u);
    if (capture_this_frame) {
        renderer_.set_debug_capture_paths(
            capture_output_path("GALAXY_GX_CAPTURE_PPM", frame_index_ + 1u),
            capture_output_path(
                "GALAXY_GX_CAPTURE_BACKBUFFER_PPM", frame_index_ + 1u));
    } else {
        renderer_.set_debug_capture_paths({}, {});
    }
    capture_draws_ = capture_this_frame && capture_draw_log_enabled() &&
        !capture_composites_only_enabled();
    capture_all_textures_ = capture_this_frame && capture_all_textures_enabled();
    capture_draw_idx_ = 0;
    capture_readback_recorded_ = false;
    // This is a process environment value, like the other capture and trace selectors in this file.
    // Without `static` it re-entered the CRT environment lock on every frame for a value that cannot
    // change.
    static const unsigned capture_readback_draw_selection = [] {
        char value[32]{};
        std::size_t length = 0;
        if (getenv_s(
                &length, value, sizeof(value),
                "GALAXY_GX_CAPTURE_DRAW") != 0 ||
            length == 0) {
            return ~0u;
        }
        return static_cast<unsigned>(std::strtoul(value, nullptr, 10));
    }();
    capture_readback_draw_ = capture_readback_draw_selection;

    std::optional<OwnedDependencyEvents> ordered_dependency_events;
    std::optional<RenderDependencyReadRecorder> ordered_read_recorder;
    std::optional<ScopedRenderDependencyCapture> ordered_capture_scope;
    const bool packet_replay_requested = ordered_packet_replay_enabled();
    std::vector<std::byte> packet_parser_input;
    bool packet_parser_input_over_limit = false;
    if (packet_replay_requested && fifo_data != nullptr && fifo_size != 0u) {
        const OwnedFifoPacket::Limits limits{};
        packet_parser_input_over_limit =
            pending_fifo_.size() > limits.fifo_bytes ||
            fifo_size > limits.fifo_bytes -
                            std::min(pending_fifo_.size(), limits.fifo_bytes);
        if (!packet_parser_input_over_limit) {
            packet_parser_input.reserve(pending_fifo_.size() + fifo_size);
            packet_parser_input.insert(
                packet_parser_input.end(),
                pending_fifo_.begin(), pending_fifo_.end());
            packet_parser_input.insert(
                packet_parser_input.end(), fifo_data, fifo_data + fifo_size);
        }
    }
    if ((ordered_dependency_capture_enabled() || packet_replay_requested) &&
        fifo_data != nullptr && fifo_size != 0u) {
        ordered_dependency_events.emplace();
        ordered_read_recorder.emplace(*ordered_dependency_events);
        ordered_capture_scope.emplace(
            parser_, vertex_loader_, texture_cache_,
            frame_dependency_event_sink_, *ordered_read_recorder,
            *ordered_dependency_events);
    }
    parser_.set_profile(
        frame_profile_enabled ? &frame_fifo_profile_ : nullptr);
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
        // A sealed snapshot keeps every texture's guest bytes immutable until
        // this chunk is submitted, so first-use decodes may run on workers.
        // Live-memory reads already sample guest RAM after the game moved on
        // (as the GPU does); a worker decoding the same bytes before this
        // chunk is submitted reads them within the same window.
        texture_cache_.set_async_decode_allowed(
            async_texture_decode_enabled() &&
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
                struct Split {
                    std::size_t offset;
                    int marker;  // -1: pointer depth field
                    int view;    // marker index (its camera), -1 none
                };
                std::array<Split, 1u + kFifoMarkersPerChunk> splits{};
                std::size_t split_count = 0;
                if (capture_in_frame) {
                    splits[split_count++] = {
                        prefix + chunk.pointer_depth_capture_fifo_offset, -1, -1};
                }
                for (std::uint8_t i = 0; i < chunk.fifo_marker_count; ++i) {
                    splits[split_count++] = {
                        prefix + chunk.fifo_markers[i].first,
                        static_cast<int>(chunk.fifo_markers[i].second),
                        static_cast<int>(i)};
                }
                std::stable_sort(splits.begin(), splits.begin() + split_count,
                    [](const Split& a, const Split& b) { return a.offset < b.offset; });
                std::size_t consumed = 0;
                for (std::size_t i = 0; i < split_count; ++i) {
                    const Split split = splits[i];
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
                    if (split.marker < 0) {
                        record_pointer_depth_capture_in_frame(chunk);
                    } else if (split.marker == static_cast<int>(FifoMarker::Scene2DBegin) ||
                               split.marker == static_cast<int>(FifoMarker::Scene2DEnd)) {
                        on_scene_2d_marker(split.marker == static_cast<int>(FifoMarker::Scene2DBegin),
                                           split.offset - prefix);
                    } else {
                        const auto index = static_cast<std::size_t>(split.view);
                        on_fifo_marker(static_cast<FifoMarker>(split.marker),
                            split.view >= 0 &&
                                    (chunk.fifo_marker_view_mask & (1u << index)) != 0u
                                ? &chunk.fifo_marker_views[index]
                                : nullptr);
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
    ordered_capture_scope.reset();
    if (ordered_dependency_events.has_value()) {
        if (packet_replay_requested) {
            if (packet_parser_input_over_limit) {
                std::cerr << "[gx-ordered-packet] frame="
                          << (frame_index_ + 1u)
                          << " skipped=parser-input-limit\n";
            } else {
                try {
                    const auto packet = capture_owned_fifo_events(
                        packet_parser_input, *ordered_dependency_events,
                        {chunk.present_swap_chain, chunk.displayed_xfb_addr});
                    const auto replayed = replay_owned_fifo_events(packet);
                    const bool equal = owned_fifo_events_equal(
                        *ordered_dependency_events, replayed);
                    std::cerr << "[gx-ordered-packet] frame="
                              << (frame_index_ + 1u)
                              << " fifo-bytes=" << packet.fifo().size()
                              << " ops=" << packet.ordered_ops().size()
                              << " replay-equal=" << (equal ? 1 : 0)
                              << " external-resources="
                              << (ordered_dependency_events->has_external_resources()
                                      ? 1 : 0)
                              << '\n';
                    if (!equal) {
                        throw std::logic_error(
                            "GX ordered packet replay changed event stream");
                    }
                } catch (const std::length_error& error) {
                    // Opt-in capture may exceed its byte/op budget; the live
                    // render parse and guest-visible PE/XFB order continue.
                    std::cerr << "[gx-ordered-packet] frame="
                              << (frame_index_ + 1u)
                              << " skipped=" << error.what() << '\n';
                }
            }
        }
        std::size_t read_count = 0u;
        std::size_t alias_copies = 0u;
        std::size_t alias_binds = 0u;
        std::size_t alias_retires = 0u;
        for (const auto& event : ordered_dependency_events->events()) {
            switch (event.kind) {
            case OwnedDependencyEvents::Kind::GuestRead: ++read_count; break;
            case OwnedDependencyEvents::Kind::AliasCopy: ++alias_copies; break;
            case OwnedDependencyEvents::Kind::AliasBind: ++alias_binds; break;
            case OwnedDependencyEvents::Kind::AliasRetire: ++alias_retires; break;
            default: break;
            }
        }
        std::cerr << "[gx-ordered-dependencies] frame=" << (frame_index_ + 1u)
                  << " events=" << ordered_dependency_events->events().size()
                  << " reads=" << read_count
                  << " bytes=" << ordered_dependency_events->captured_bytes()
                  << " alias-copies=" << alias_copies
                  << " alias-binds=" << alias_binds
                  << " alias-retires=" << alias_retires
                  << " external-resources="
                  << (ordered_dependency_events->has_external_resources() ? 1 : 0)
                  << '\n';
    }
    if (chunk.pe_events_preclassified) {
        if (!defer_pe_events_to_gpu_fence) {
            throw std::logic_error(
                "GX PE preclassification used without GPU-fence delivery");
        }
        if (chunk.pe_events.size() !=
            chunk.preclassified_pe_events.size()) {
            throw std::runtime_error(
                "GX PE preclassifier/render parser event-count mismatch: "
                "classified=" +
                std::to_string(chunk.preclassified_pe_events.size()) +
                " rendered=" + std::to_string(chunk.pe_events.size()));
        }
        for (std::size_t index = 0; index < chunk.pe_events.size(); ++index) {
            const PeEvent& rendered = chunk.pe_events[index];
            const FramePeEventSignature actual{
                rendered.kind == PeEvent::Kind::Finish
                    ? FramePeEventSignature::Kind::Finish
                    : FramePeEventSignature::Kind::Token,
                rendered.token,
                rendered.interrupt};
            if (actual != chunk.preclassified_pe_events[index]) {
                throw std::runtime_error(
                    "GX PE preclassifier/render parser event mismatch at "
                    "index=" + std::to_string(index));
            }
        }
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

    if (capture_this_frame && !capture_readback_recorded_) {
        if (capture_readback_draw_ != ~0u) {
            std::cerr << "[efb-dump] requested draw "
                      << capture_readback_draw_
                      << " was not issued; capturing frame end\n";
        }
        capture_readback_recorded_ = renderer_.debug_copy_efb_to_readback();
    }

    // Present every VI after the first valid XFB. Some VI retraces only update EFB/texture side
    // effects or queue no FIFO at all. If those retraces skip the swap chain, flip-model cadence
    // collapses and the frame shows gaps even though the game keeps advancing. Before the first XFB
    // exists, keep the window hidden unless a diagnostic capture asks for a backbuffer.
    if (chunk.present_swap_chain) {
        ++stat_xfb_present_requests_;
    }
    const std::uint64_t current_present_epoch =
        chunk.present_swap_chain ? present_epoch_ + 1u : present_epoch_;
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
            ? efb_copies_.find_xfb(chunk.displayed_xfb_addr)
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
            max_xfb_preserve_age(),
            last_presented_xfb_stamp_.load(std::memory_order_relaxed));
    selected_xfb = selection.selected;
    bool stale_fallback = false;
    bool missing_fallback = false;
    if (chunk.present_swap_chain && selection.stale_preserved) {
        ++stat_xfb_present_stale_preserved_;
        if (trace_xfb_present_enabled()) {
            std::cerr << "[xfb-present] frame=" << frame_index_
                      << " render-stale=1"
                      << " selected=0x" << std::hex
                      << chunk.displayed_xfb_addr
                      << " stale-registry=0x" << selected_xfb->guest_addr
                      << " latest=0x" << latest_xfb->guest_addr
                      << std::dec
                      << " stale-stamp=" << selected_xfb->frame_stamp
                      << " latest-stamp=" << latest_xfb->frame_stamp
                      << " stale-fallback=0"
                      << " stale-preserved=1"
                      << " stale-fallbacks="
                      << stat_xfb_present_stale_fallbacks_.load(
                             std::memory_order_relaxed)
                      << " stale-preserved-total="
                      << stat_xfb_present_stale_preserved_.load(
                             std::memory_order_relaxed)
                      << '\n';
        }
    }
    if (chunk.present_swap_chain && selection.stale_expired) {
        stale_fallback = true;
        ++stat_xfb_present_stale_fallbacks_;
        if (trace_xfb_present_enabled()) {
            std::cerr << "[xfb-present] frame=" << frame_index_
                      << " render-stale-expired=1"
                      << " selected=0x" << std::hex
                      << chunk.displayed_xfb_addr
                      << " latest=0x"
                      << (latest_xfb != nullptr ? latest_xfb->guest_addr : 0u)
                      << " fallback-selected=0x"
                      << (selected_xfb != nullptr ? selected_xfb->guest_addr : 0u)
                      << std::dec
                      << " stale-fallbacks="
                      << stat_xfb_present_stale_fallbacks_.load(
                             std::memory_order_relaxed)
                      << '\n';
        }
    }
    if (chunk.present_swap_chain && selection.missing) {
        ++stat_xfb_present_misses_;
    }
    if (chunk.present_swap_chain && selection.missing_preserved) {
        ++stat_xfb_present_missing_preserved_;
    }
    if (chunk.present_swap_chain && selection.missing_expired) {
        missing_fallback = true;
        ++stat_xfb_present_missing_fallbacks_;
    }
    if (selection.missing) {
        if (trace_xfb_present_enabled()) {
            std::cerr << "[xfb-present] frame=" << frame_index_
                      << " render-missing=1"
                      << " selected=0x" << std::hex
                      << chunk.displayed_xfb_addr
                      << " latest=0x"
                      << (latest_xfb != nullptr ? latest_xfb->guest_addr : 0u)
                      << std::dec
                      << " misses="
                      << stat_xfb_present_misses_.load(
                             std::memory_order_relaxed)
                      << " missing-fallback=0"
                      << " missing-preserved="
                      << (selection.missing_preserved ? 1 : 0)
                      << " missing-fallbacks="
                      << stat_xfb_present_missing_fallbacks_.load(
                             std::memory_order_relaxed)
                      << " missing-preserved-total="
                      << stat_xfb_present_missing_preserved_.load(
                             std::memory_order_relaxed)
                      << " stale-expired="
                      << (selection.stale_expired ? 1 : 0)
                      << " missing-expired="
                      << (selection.missing_expired ? 1 : 0)
                      << '\n';
        }
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
        chunk.present_swap_chain &&
        duplicate_xfb_present &&
        skip_duplicate_xfb_presents_enabled() &&
        !capture_this_frame &&
        !native_settings_overlay_visible();
    const bool skip_rate_present =
        chunk.present_swap_chain &&
        selected_xfb != nullptr &&
        !skip_duplicate_present &&
        !capture_this_frame &&
        !native_settings_overlay_visible() &&
        skip_for_present_rate();
    const bool capture_backbuffer =
        capture_this_frame && selected_xfb != nullptr;
    bool present_swap_chain =
        capture_backbuffer ||
        (chunk.present_swap_chain && selected_xfb != nullptr &&
         !skip_duplicate_present && !skip_rate_present);
    if (interp_recording_) {
        note_interp_log_completion();
        if (chunk.present_swap_chain) {
            interp_event("chunk-decision", selected_xfb != nullptr ? selected_xfb->copy_serial : 0u,
                         1u | (duplicate_xfb_present ? 2u : 0u) | (skip_duplicate_present ? 4u : 0u) |
                             (skip_rate_present ? 8u : 0u) | (present_swap_chain ? 16u : 0u) |
                             (interp_log_waiting_ ? 32u : 0u) | (capture_backbuffer ? 64u : 0u));
        }
        static const bool trace_interp = read_env_u64("GALAXY_INTERP_TRACE", 0u) != 0u;
        static std::uint64_t traced = 0u;
        if (trace_interp && (traced++ % 240u) == 0u) {
            std::cerr << "[interp-chunk] frames=" << interp_frames_
                      << " scenes=" << interp_scenes_in_chunk_
                      << " log-serial=" << renderer_.replay_serial()
                      << " log=" << (renderer_.replay_available() ? 1 : 0)
                      << " waiting=" << (interp_log_waiting_ ? 1 : 0)
                      << " sequence=" << (interp_sequence_.active ? 1 : 0)
                      << " present=" << (present_swap_chain ? 1 : 0) << '\n';
        }
        // The ready frame's own present: its sub-frames come first, the real
        // frame after them (started by the render loop after this chunk).
        if (present_swap_chain) {
            interp_event("chunk-present", selected_xfb != nullptr ? selected_xfb->guest_addr : 0u,
                         chunk.displayed_xfb_addr);
        }
        // As on the VI-present path, a present while a completed log waits
        // starts that log's sequence: the VI may still name the previous
        // XFB (the guest flips at the next retrace), so a present the
        // duplicate or rate checks would drop still starts it. Dropping it
        // skipped the waiting frame when the next one closed first.
        if (interp_log_waiting_ && chunk.present_swap_chain && selected_xfb != nullptr &&
            !capture_backbuffer && renderer_.replay_available()) {
            interp_start_pending_ = true;
            present_swap_chain = false;
        } else if (interp_sequence_.active && present_swap_chain) {
            // Another frame presents now: the playing one is superseded.
            supersede_interp_sequence();
        }
        if (present_swap_chain && !capture_backbuffer && !duplicate_xfb_present) {
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
        if (trace_xfb_present_enabled() &&
            (stale_fallback || missing_fallback ||
             selection.stale_preserved || selection.missing_preserved) &&
            selected_xfb != nullptr) {
            std::cerr << "[xfb-present] frame=" << frame_index_
                      << " render-selected=0x" << std::hex
                      << selected_xfb->guest_addr
                      << std::dec
                      << " stamp=" << selected_xfb->frame_stamp
                      << " serial=" << selected_xfb->copy_serial
                      << " stale-fallback=" << (stale_fallback ? 1 : 0)
                      << " stale-preserved="
                      << (selection.stale_preserved ? 1 : 0)
                      << " missing-fallback="
                      << (missing_fallback ? 1 : 0)
                      << " missing-preserved="
                      << (selection.missing_preserved ? 1 : 0)
                      << '\n';
        }
        const auto present_record_start = frame_timing_enabled
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        const std::string snapshot_path =
            capture_backbuffer ? std::string{} : present_snapshot_path();
        if (!snapshot_path.empty()) {
            renderer_.set_debug_capture_paths({}, snapshot_path);
        }
        renderer_.present(selected_xfb, capture_backbuffer || !snapshot_path.empty());
        if (!snapshot_path.empty()) {
            renderer_.set_debug_capture_paths({}, {});
        }
        if (frame_timing_enabled) {
            present_record_us =
                elapsed_us(present_record_start, std::chrono::steady_clock::now());
        }
    }
    if (skip_duplicate_present) {
        record_skipped_duplicate_xfb_present(*selected_xfb);
        if (trace_xfb_present_enabled()) {
            std::cerr << "[xfb-present] frame=" << frame_index_
                      << " render-duplicate-skip=1"
                      << " selected=0x" << std::hex
                      << chunk.displayed_xfb_addr
                      << " registry=0x" << selected_xfb->guest_addr
                      << std::dec
                      << " stamp=" << selected_xfb->frame_stamp
                      << " serial=" << selected_xfb->copy_serial
                      << " duplicate=1"
                      << " duplicate-skip=1"
                       << " repeated-or-stale-serial-requests="
                       << stat_xfb_repeated_or_stale_serial_requests_.load(
                             std::memory_order_relaxed)
                      << " misses="
                      << stat_xfb_present_misses_.load(
                             std::memory_order_relaxed)
                      << " skipped-duplicates="
                      << stat_xfb_present_skipped_duplicate_presents_.load(
                             std::memory_order_relaxed)
                      << '\n';
        }
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
    if (chunk.present_swap_chain && present_swap_chain &&
        selected_xfb != nullptr) {
        present_epoch_ = current_present_epoch;
        record_presented_xfb_copy(*selected_xfb);
        last_presented_xfb_addr_.store(
            selected_xfb->guest_addr, std::memory_order_relaxed);
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
    // Both helpers are nonblocking polls. A capture's immutable paths were
    // bound before recording, so a later render frame can safely retire its
    // completed GPU copy without stalling VI cadence or retargeting the file.
    renderer_.debug_log_readback();
    renderer_.debug_log_backbuffer_readback();

    ++frame_index_;
    timing_frame_index_.store(frame_index_, std::memory_order_relaxed);
    if (trace_xfb_causal_enabled() || trace_xfb_causal_deferred_enabled()) {
        causal_trace_frame_index_.store(
            frame_index_, std::memory_order_relaxed);
    }
    const auto render_end = frame_timing_enabled
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    // Per-frame pipeline-compile attribution. Every counter in CompileStats is
    // monotonic within a session, so the subtraction cannot underflow
    // (PipelineCache::shutdown() is the only resetter and it ends the session).
    // Published on the `[gx-frame-timing]` line so a tail frame can be tied to a
    // compile count directly instead of inferred from `flush-pso-us` alone.
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
    if (trace_gx_stats) {
        stat_render_ns_.fetch_add(
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    render_end - render_start).count()),
            std::memory_order_relaxed);
    }

    if (trace_xfb_causal_frame(frame_index_)) {
        const std::uint64_t host_steady_ns =
            static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch())
                    .count());
        std::cerr << "[xfb-causal-frame] frame=" << frame_index_
                  << " host-steady-ns=" << host_steady_ns
                  << " fifo-bytes=" << fifo_size
                  << " fifo-complete=" << (pending_fifo_.empty() ? 1 : 0)
                  << " pending-fifo=" << pending_fifo_.size()
                  << " parsed-copy-exec="
                  << (frame_xfb_copies_ + frame_tex_copies_)
                  << " parsed-xfb-copy-exec=" << frame_xfb_copies_
                  << " parsed-texture-copy-exec=" << frame_tex_copies_
                  << " present-only=" << (chunk.present_only ? 1 : 0)
                  << " present-request="
                  << (chunk.present_swap_chain ? 1 : 0)
                  << " present-submitted=" << (present_swap_chain ? 1 : 0)
                  << " requested-xfb=0x" << std::hex
                  << chunk.displayed_xfb_addr
                  << " selected-xfb=0x"
                  << (selected_xfb != nullptr ? selected_xfb->guest_addr : 0u)
                  << std::dec
                  << " selected-serial="
                  << (selected_xfb != nullptr ? selected_xfb->copy_serial : 0u)
                  << " global-xfb-copies="
                  << stat_xfb_copies_.load(std::memory_order_relaxed)
                  << " producer-wait-us=" << (chunk.producer_wait_ns / 1000u)
                  << " pe-classified-events="
                  << chunk.preclassified_pe_events.size()
                  << " pe-rendered-events=" << chunk.pe_events.size()
                  << '\n';
    }

    if (trace_xfb_causal_deferred_frame(frame_index_)) {
        if (deferred_xfb_causal_count_ < deferred_xfb_causal_.size()) {
            DeferredXfbCausalRecord& record =
                deferred_xfb_causal_[deferred_xfb_causal_count_++];
            record.frame_index = frame_index_;
            record.fifo_bytes = fifo_size;
            record.fifo_complete = pending_fifo_.empty();
            record.copy_exec_count = frame_xfb_copies_ + frame_tex_copies_;
            record.xfb_copy_count = frame_xfb_copies_;
            record.texture_copy_count = frame_tex_copies_;
            record.present_only = chunk.present_only;
            record.present_request = chunk.present_swap_chain;
            record.present_submitted = present_swap_chain;
            record.selected_xfb =
                selected_xfb != nullptr ? selected_xfb->guest_addr : 0u;
            record.selected_serial =
                selected_xfb != nullptr ? selected_xfb->copy_serial : 0u;
            record.global_xfb_copies =
                stat_xfb_copies_.load(std::memory_order_relaxed);
            record.pe_classified_events = chunk.preclassified_pe_events.size();
            record.pe_rendered_events = chunk.pe_events.size();
        } else {
            ++deferred_xfb_causal_dropped_;
        }
    }

    // frame_time_sample_ is included so a bounded sample reports what it measured. Without it the
    // sampled frame accumulates every per-draw field and prints nothing, because frame_profile_enabled
    // covers only the two intrusive flags. Nothing changes unless GALAXY_GX_FRAME_TIMING_SAMPLE >= 2.
    //
    // Field trustworthiness on a sampled frame:
    //   - Always valid: total-us, parse-us, submit-us, begin-*-us (per-frame reads are
    //     unconditional), and the clock-free counters: flush-calls, pso-resolutions,
    //     pso-route-early, pso-route-memo, pso-route-get.
    //   - Valid because frame_time_detail_ is true on this frame: flush-us, flush-pso-us,
    //     pso-get-us, pso-get-max-us, flush-*-us, vertex-load-us, draw-record-us, cached-vertex-*-us.
    //   - Valid on a sampled frame: producer-wait-us, snapshot-us and dependency-scan-us. Their
    //     start clock and measurement are both gated on `producer_time_detail`, which includes
    //     `frame_gx_timing_sample_enabled`. On an ordinary frame these three are gated off and read 0.
    //   - Exactly 0 on a sampled frame, by design and not as a measurement: the fifo-* `*-us` fields.
    //     Per-command FIFO timing has its own gate, GALAXY_TRACE_GX_MICROPROFILE, because per-command
    //     clock reads would charge the parser they measure. The fifo-* count fields stay valid, so a
    //     0 in `fifo-command-us` here means "not timed", not "free".
    if (frame_profile_enabled || frame_time_sample_) {
        const std::uint64_t total_us = elapsed_us(render_start, render_end);
        // Wall-clock distance since the previously emitted frame line. `total-us` covers only the span
        // this function owns (render_start to render_end), so alone it cannot tell a render thread busy
        // for 22 ms inside a 155 ms frame from one busy for 22 ms inside a 22 ms frame. On the low-end
        // machine every wait field on this line reads 0, while `[gx-queue-wait]` records the simulation
        // thread blocked for long intervals against a small queue limit. Those cannot both describe a
        // merely backlogged render thread, so its real period must be measured rather than inferred.
        // Emitted frames only: on a sampled run the value averages over the sample interval, not a
        // single frame.
        std::uint64_t frame_interval_us = 0u;
        if (last_frame_timing_emit_valid_) {
            frame_interval_us = elapsed_us(last_frame_timing_emit_, render_start);
        }
        last_frame_timing_emit_ = render_start;
        last_frame_timing_emit_valid_ = true;
        const std::uint64_t parse_us = elapsed_us(parse_start, parse_end);
        const std::uint64_t submit_us = elapsed_us(parse_end, gpu_submit_end);
        const std::uint64_t producer_wait_us = chunk.producer_wait_ns / 1000u;
        // Sampling is always on, so the printing side stays bounded. Otherwise every over-threshold
        // frame would add a line to a routine recording. Explicit stats and stall runs, and microprofile
        // windows, print as before. Routine runs get the same sample through the deferred stats ring.
        if (trace_gx_stats || frame_microprofile_enabled_ ||
            frame_time_sample_ ||
            total_us >= trace_gx_stall_threshold_us() ||
            producer_wait_us >= trace_gx_stall_threshold_us()) {
            std::ostringstream timing_message;
            timing_message << "[gx-frame-timing] frame=" << frame_index_
                  // Why this frame printed. The line has two independent reasons, and they select different frames:
                  //   selected=1 - total_us or producer-wait crossed GALAXY_TRACE_GX_STALL_US, a stall. These are
                  //                the only frames a default run prints, so statistics over them describe the worst
                  //                frames, not the session.
                  //   sample=1   - a fixed-stride sample chosen by GALAXY_GX_FRAME_TIMING_SAMPLE. Periodic work may
                  //                alias this stride, so stall rows are kept too.
                  // A frame can be both. Filter on selected=0 for the unbiased set, and never mix the two when
                  // averaging. This field comes first, because every later field is interpretable only once the
                  // reader knows which population the row came from.
                  << " selected="
                  << ((total_us >= trace_gx_stall_threshold_us() ||
                       producer_wait_us >= trace_gx_stall_threshold_us())
                          ? 1
                          : 0)
                  << " sample=" << (frame_time_sample_ ? 1 : 0)
                  << " profile=" << (frame_profile_enabled ? 1 : 0)
                  << " detail=" << (frame_time_detail_ ? 1 : 0)
                  << " fifo-bytes=" << fifo_size
                  << " total-us=" << total_us
                  // Time since the previous emitted row, which can span many
                  // rendered frames. It is not an exclusive idle interval and
                  // cannot be compared with one frame's total to infer saturation.
                  << " frame-interval-us=" << frame_interval_us
                  // Optional CPU duration beside wall time. Their gap includes
                  // blocking and descheduling; it cannot attribute either cause
                  // alone. Interpret only available samples at matched settings.
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
                  << " pe-classify-us="
                  << (chunk.pe_classification_ns / 1000u)
                  << " pe-classified-events="
                  << chunk.preclassified_pe_events.size()
                  << " pe-event-free-receipt="
                  << (chunk.pe_event_free_receipt ? 1 : 0)
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
                  << " dirty-dl-invalidations="
                  << dirty_display_list_invalidations
                  << " dirty-decoded-vtx-invalidations="
                  << dirty_decoded_vertex_invalidations
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
                  // No "early out" field here. frame_draws_issued_ counts only draws that entered on_draw or the
                  // generic on_cached_draw_run_draw loop. The packet and simple paths count their draws through
                  // pending_draw_batch_.draw_count and never touch frame_draws_issued_, so subtracting these
                  // counters from it gives a meaningless number. The flush-level split is pso-route-early,
                  // pso-route-memo and pso-route-get, which are counted per flush_draw_state call.
                  << " pso-get-us=" << frame_pso_get_us_
                  << " pso-get-max-us=" << frame_pso_get_max_us_
                  // Clock-free attribution. flush-calls is the denominator for
                  // every *_us field. Within a frame, draws partition exactly
                  // into early-out + memo + get(), and that sum must equal
                  // pso-resolutions. All three are counted unconditionally, so
                  // the partition holds whether or not timing was instrumented.
                  << " flush-calls=" << frame_flush_calls_
                  // Three distinct measurements of the same function; do not
                  // divide one by another's denominator.
                  //
                  // `flush-us` times every body path only when detail=1. It
                  // excludes early reuse and is zero/unavailable when detail=0.
                  // For a detailed frame its body-path mean is flush-us divided
                  // by (flush-calls - pso-route-early), if that count is nonzero.
                  //
                  // `flush-total-us` / `flush-mean-us` come from the sampler:
                  // ScopedFlushTiming wraps the *call site* and times 1 call in
                  // every kFlushTimingSampleStride, so they DO include early-out
                  // calls and are the per-call cost to compare across
                  // builds with the same sampling policy. The body sum and
                  // sampled whole-call estimate overlap; do not add them or
                  // infer a unique cause from their difference.
                  << " flush-total-us=" << frame_flush_total_us_
                  << " flush-timed-samples=" << frame_flush_timed_samples_
                  << " flush-mean-us="
                  << (frame_flush_timed_samples_ != 0u
                          ? frame_flush_total_us_ /
                                frame_flush_timed_samples_
                          : 0u)
                  << " pso-resolutions=" << frame_pso_resolutions_
                  << " pso-route-early=" << frame_pso_route_early_
                  // Per-frame pipeline work actually performed. `pso-resolutions`
                  // above counts *requests*; these count builds. Their difference
                  // is requests served from memory, which is the partition that
                  // separates "this frame is slow because it is compiling" from
                  // "this frame is slow because it is waiting on a compile a
                  // worker started". `pso-compiles = 0` on a frame with a large
                  // `flush-pso-us` falsifies the wait reading and moves the cause
                  // to the library-load or memo path.
                  << " pso-compiles=" << frame_pso_compiles_
                  << " pso-library-loads=" << frame_pso_library_loads_
                  << " pso-shader-pairs=" << frame_shader_pair_compiles_
                  // FXC microseconds this frame, beside `flush-pso-us` and
                  // `pso-compiles`. This is the pair that settles whether a slow
                  // frame is compiled work or waiting: a large `flush-pso-us`
                  // with `pso-blob-us` near 0 is not FXC, and one with a large
                  // `pso-blob-us` is. Counts alone cannot separate them.
                  << " pso-blob-us=" << frame_blob_compile_us_
                  << " pso-blobs-from-cache=" << frame_blobs_from_cache_
                  // Derive the other two routes rather than storing them: the
                  // memo and get() routes are already counted by
                  // frame_pso_memo_hits_ and frame_pso_get_calls_, and a second
                  // pair of members incremented at the same two sites would be
                  // a duplicate field, not an independent measurement.
                  << " pso-route-memo=" << frame_pso_memo_hits_
                  << " pso-route-get=" << frame_pso_get_calls_
                  // Batched replay path split. replay-generic-draws counts
                  // individual draws that went through the per-packet fallback,
                  // i.e. draws the two batched paths refused. The packet counter
                  // is OFFERS, not acceptances: the parser's own
                  // fifo-call-dl-replay-packet-run-count is the acceptance count.
                  << " replay-prepared-batches="
                  << frame_replay_prepared_batches_
                  << " replay-packet-offers=" << frame_replay_packet_batches_
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
                  // Entered-count for the immutable path. Read the two byte counters above only relative to this:
                  // both are 0 both when the path was never entered and when every byte was a copy.
                  << " immutable-vertex-uploads=" << renderer_.vertex_ring().immutable_upload_calls()
                  << " draw-record-us=" << frame_draw_record_us_
                  << " efb-copy-us=" << frame_efb_copy_us_
                  << " decoded-vtx-cache-hits="
                  << frame_decoded_vertex_cache_hits_
                  << " decoded-vtx-cache-misses="
                  << frame_decoded_vertex_cache_misses_
                  << " decoded-vtx-cache-evictions="
                  << frame_decoded_vertex_cache_evictions_
                  << " decoded-vtx-cache-stale-rejects="
                  << frame_decoded_vertex_cache_stale_rejects_
                  << " resident-vtx-hits=" << frame_resident_vertex_hits_
                  << " resident-vtx-bytes=" << frame_resident_vertex_bytes_
                  << " resident-vtx-used-bytes="
                  << static_cast<std::uint64_t>(resident_vertices_.used_units()) *
                         sizeof(GxVertexOut)
                  << " decoded-vtx-cache-bytes="
                  << decoded_packet_run_cache_bytes_
                  // Decoded GPU texture residency against its bound. Reported on the sampled frame line rather than
                  // only at shutdown, so it is visible during a run.
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
                  // EFB-copy destination cache, a resolution-scaled GPU pool. Its bytes are SCALED: each destination
                  // is allocated at the internal EFB extent, so the size grows with efb_scale^2 while the budget
                  // stays fixed. Reported next to the field before it, so one run covers both budgeted GPU pools.
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
                  << " fifo-indx-count=" << frame_fifo_profile_.indx_count
                  << " fifo-indx-us=" << frame_fifo_profile_.indx_us
                  << " fifo-call-dl-count="
                  << frame_fifo_profile_.call_dl_count
                  << " fifo-call-dl-us=" << frame_fifo_profile_.call_dl_us
                  << " fifo-call-dl-bytes="
                  << frame_fifo_profile_.call_dl_bytes
                  << " fifo-call-dl-cache-hits="
                  << frame_fifo_profile_.call_dl_cache_hits
                  << " fifo-call-dl-cache-misses="
                  << frame_fifo_profile_.call_dl_cache_misses
                  << " fifo-call-dl-cache-stores="
                  << frame_fifo_profile_.call_dl_cache_stores
                  << " fifo-call-dl-cache-uncacheable="
                  << frame_fifo_profile_.call_dl_cache_uncacheable
                  << " fifo-call-dl-miss-parse-us="
                  << frame_fifo_profile_.call_dl_miss_parse_us
                  << " fifo-call-dl-store-us="
                  << frame_fifo_profile_.call_dl_store_us
                  << " fifo-call-dl-replay-command-count="
                  << frame_fifo_profile_.call_dl_replay_command_count
                  << " fifo-call-dl-replay-command-us="
                  << frame_fifo_profile_.call_dl_replay_command_us
                  << " fifo-call-dl-replay-prepared-run-count="
                  << frame_fifo_profile_.call_dl_replay_prepared_run_count
                  << " fifo-call-dl-replay-prepared-source-draw-count="
                  << frame_fifo_profile_
                         .call_dl_replay_prepared_source_draw_count
                  << " fifo-call-dl-replay-prepared-payload-bytes="
                  << frame_fifo_profile_
                         .call_dl_replay_prepared_payload_bytes
                  << " fifo-call-dl-replay-packet-run-count="
                  << frame_fifo_profile_.call_dl_replay_packet_run_count
                  << " fifo-call-dl-replay-packet-source-draw-count="
                  << frame_fifo_profile_
                         .call_dl_replay_packet_source_draw_count
                  << " fifo-call-dl-replay-state-count="
                  << frame_fifo_profile_.call_dl_replay_state_count
                  << " fifo-call-dl-replay-state-us="
                  << frame_fifo_profile_.call_dl_replay_state_us
                  << " fifo-call-dl-replay-indx-count="
                  << frame_fifo_profile_.call_dl_replay_indx_count
                  << " fifo-call-dl-replay-indx-us="
                  << frame_fifo_profile_.call_dl_replay_indx_us
                  << " fifo-call-dl-replay-draw-count="
                  << frame_fifo_profile_.call_dl_replay_draw_count
                  << " fifo-call-dl-replay-draw-us="
                  << frame_fifo_profile_.call_dl_replay_draw_us
                  << " fifo-call-dl-replay-misc-count="
                  << frame_fifo_profile_.call_dl_replay_misc_count
                  << " fifo-call-dl-replay-misc-us="
                  << frame_fifo_profile_.call_dl_replay_misc_us
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
            if (frame_microprofile_enabled_ &&
                frame_fifo_profile_.call_dl_count != 0) {
                std::array<
                    const FifoParserProfile::CallDisplayListEntry*,
                    FifoParserProfile::kCallDisplayListEntryCapacity>
                    call_dl_entries{};
                std::size_t call_dl_entry_count = 0;
                for (const auto& entry : frame_fifo_profile_.call_dl_entries) {
                    if (entry.count != 0) {
                        call_dl_entries[call_dl_entry_count++] = &entry;
                    }
                }
                std::sort(
                    call_dl_entries.begin(),
                    call_dl_entries.begin() + call_dl_entry_count,
                    [](const auto* lhs, const auto* rhs) {
                        return lhs->us > rhs->us;
                    });

                const std::size_t limit =
                    std::min<std::size_t>(call_dl_entry_count, 8);
                for (std::size_t i = 0; i < limit; ++i) {
                    const auto& entry = *call_dl_entries[i];
                    std::cerr << std::dec
                              << "[gx-dl-profile] frame=" << frame_index_
                              << " rank=" << (i + 1u)
                              << " addr=0x" << std::hex << entry.guest_addr
                              << std::dec
                              << " size=" << entry.byte_size
                              << " calls=" << entry.count
                              << " us=" << entry.us
                              << " bytes=" << entry.bytes
                              << " overflow="
                              << frame_fifo_profile_.call_dl_profile_overflow
                              << '\n';
                }
            }
        }
    }

    // Two independent arms, not intersected:
    //   * `trace_gx_stats` keeps its once-per-600-frames cadence.
    //   * `frame_time_sample_` records every sampled frame. Gating it on the 600-frame cadence too
    //     would make a short sample period emit too few rows to build a distribution.
    // Output stays bounded: the sample period is set by the operator (period <= 1 disables sampling),
    // the ring holds kDeferredGxStatsCapacity rows and reports drops, and nothing prints per frame.
    // The rows are written once at shutdown by emit_deferred_gx_stats().
    if ((trace_gx_stats && frame_index_ % 600 == 0) || frame_time_sample_) {
        const std::uint64_t render_ns =
            stat_render_ns_.exchange(0, std::memory_order_relaxed);
        const std::uint64_t queue_wait_ns =
            stat_queue_wait_ns_.exchange(0, std::memory_order_relaxed);
        const std::uint64_t dependency_scan_ns =
            stat_dependency_scan_ns_.exchange(0, std::memory_order_relaxed);
        const std::uint64_t dependency_cache_hits =
            stat_dependency_cache_hits_.exchange(
                0,
                std::memory_order_relaxed);
        const std::uint64_t dependency_cache_misses =
            stat_dependency_cache_misses_.exchange(
                0,
                std::memory_order_relaxed);
        const std::uint64_t dependency_cache_invalidations =
            stat_dependency_cache_invalidations_.exchange(
                0,
                std::memory_order_relaxed);
        const std::uint64_t dependency_draw_run_cache_hits =
            stat_dependency_draw_run_cache_hits_.exchange(
                0,
                std::memory_order_relaxed);
        const std::uint64_t dependency_draw_run_cache_misses =
            stat_dependency_draw_run_cache_misses_.exchange(
                0,
                std::memory_order_relaxed);
        const std::uint64_t dependency_draw_run_cache_evictions =
            stat_dependency_draw_run_cache_evictions_.exchange(
                0,
                std::memory_order_relaxed);
        const std::uint64_t snapshot_ns =
            stat_snapshot_ns_.exchange(0, std::memory_order_relaxed);
        const std::uint64_t snapshot_bytes =
            stat_snapshot_bytes_.exchange(0, std::memory_order_relaxed);
        const std::uint64_t snapshot_ranges =
            stat_snapshot_ranges_.exchange(0, std::memory_order_relaxed);
        const std::uint64_t dirty_ranges =
            stat_dirty_ranges_.exchange(0, std::memory_order_relaxed);
        const std::uint64_t dirty_bytes =
            stat_dirty_bytes_.exchange(0, std::memory_order_relaxed);
        const std::uint64_t dirty_texture_evictions_total =
            stat_dirty_texture_evictions_.exchange(
                0,
                std::memory_order_relaxed);
        const std::uint64_t dirty_display_list_invalidations_total =
            stat_dirty_display_list_invalidations_.exchange(
                0,
                std::memory_order_relaxed);
        DeferredGxStatsSample sample{};
        sample.frame_index = frame_index_;
        sample.total_draws = stat_draws_issued_;
        sample.total_no_pso = stat_draws_no_pso_;
        sample.frame_draws = frame_draws_issued_;
        sample.frame_no_pso = frame_draws_no_pso_;
        sample.xfb_copies = stat_xfb_copies_.load(std::memory_order_relaxed);
        sample.tex_copies = stat_tex_copies_;
        sample.specialized_hits = pipeline_cache_.specialized_hits();
        sample.uber_fallbacks = pipeline_cache_.uber_fallbacks();
        sample.has_latest_xfb = efb_copies_.latest() != nullptr;
        sample.present = xfb_present_stats();
        sample.stale_fallbacks = stat_xfb_present_stale_fallbacks_.load(
            std::memory_order_relaxed);
        sample.missing_fallbacks = stat_xfb_present_missing_fallbacks_.load(
            std::memory_order_relaxed);
        sample.stale_preserved = stat_xfb_present_stale_preserved_.load(
            std::memory_order_relaxed);
        sample.missing_preserved = stat_xfb_present_missing_preserved_.load(
            std::memory_order_relaxed);
        sample.render_ns = render_ns;
        sample.queue_wait_ns = queue_wait_ns;
        sample.dependency_scan_ns = dependency_scan_ns;
        sample.dependency_cache_hits = dependency_cache_hits;
        sample.dependency_cache_misses = dependency_cache_misses;
        sample.dependency_cache_invalidations = dependency_cache_invalidations;
        sample.dependency_draw_run_cache_hits =
            dependency_draw_run_cache_hits;
        sample.dependency_draw_run_cache_misses =
            dependency_draw_run_cache_misses;
        sample.dependency_draw_run_cache_evictions =
            dependency_draw_run_cache_evictions;
        sample.snapshot_ns = snapshot_ns;
        sample.snapshot_bytes = snapshot_bytes;
        sample.snapshot_ranges = snapshot_ranges;
        sample.dirty_ranges = dirty_ranges;
        sample.dirty_bytes = dirty_bytes;
        sample.dirty_texture_evictions = dirty_texture_evictions_total;
        sample.dirty_display_list_invalidations =
            dirty_display_list_invalidations_total;
        sample.palette_uploads = stat_palette_uploads_;
        sample.inline_matrix_uploads = stat_inline_matrix_uploads_;
        // Clock-free attribution for this sampled frame. These are the per-frame
        // counters `flush_draw_state` already maintains unconditionally, so a
        // routine sample carries them without any instrumentation cost and
        // without the sampling bias that makes `[gx-frame-timing]` unusable for
        // per-frame questions (it only prints frames that crossed the stall
        // threshold). See DeferredGxStatsSample for the two ratios this enables.
        sample.draws = frame_draws_issued_;
        sample.draw_batches = frame_draw_batches_;
        sample.flush_calls = frame_flush_calls_;
        sample.pso_resolutions = frame_pso_resolutions_;
        sample.pso_route_early = frame_pso_route_early_;
        // The `get()` route is `frame_pso_get_calls_` and the memo route is
        // `frame_pso_memo_hits_`; the `pso-route-memo` / `pso-route-get` names
        // exist only in the `[gx-frame-timing]` printer.
        sample.pso_route_memo = frame_pso_memo_hits_;
        sample.pso_route_get = frame_pso_get_calls_;
        // The *_ns fields above are exchanged from atomics that only accumulate
        // under trace_gx_stats/stalls; they legitimately read 0 on a routine
        // sample. Carry them anyway so an explicitly traced run gets the same
        // row shape and the two runs stay directly comparable.
        sample.flush_pso_ns = frame_flush_pso_us_ * 1000u;
        sample.vertex_load_ns = frame_vertex_load_us_ * 1000u;
        sample.cached_vertex_upload_ns = frame_cached_vertex_upload_us_ * 1000u;
        sample.draw_record_ns = frame_draw_record_us_ * 1000u;
        // Store whenever this frame was chosen as a sample frame, and let the ring decide how to fit it.
        //
        // `frame_time_sample_` is true once per GALAXY_GX_FRAME_TIMING_SAMPLE period, not on every frame.
        // It is `frame_time_detail_` that is true on every frame, so the per-draw clocks do not alias.
        // Do not add a period-derived stride here: it would compose with the period instead of replacing
        // it. The period already bounds the row rate.
        //
        // The period does not bound the ring. Capacity is kDeferredGxStatsCapacity (128) rows, so a short
        // period overflows within a long session. The reservoir path below keeps the retained rows a
        // uniform draw over the whole session at any period.
        if (frame_time_sample_) {
            ++deferred_gx_stats_seen_;
            if (deferred_gx_stats_count_ < deferred_gx_stats_.size()) {
                deferred_gx_stats_[deferred_gx_stats_count_++] = sample;
            } else {
                // Reservoir sampling: replace a uniformly chosen existing row instead of discarding this one.
                //
                // Keeping the first capacity rows would describe only the opening of a session, because slow
                // windows arrive after scene changes and a front-loaded ring never sees them.
                //
                // Algorithm R over a fixed-size array: for the N-th sample (N 1-based), keep it with probability
                // capacity/N by choosing a uniform index in [0, N). If that index is inside the ring, it
                // overwrites that row; otherwise the sample is dropped. Each retained row is then a uniform draw
                // from the whole session, at fixed memory and two integer ops per stored sample.
                //
                // The generator is a plain xorshift64*, held per backend: this runs on the render thread, so
                // there is no shared state and no libc rand (which would lock). Determinism is also useful: the
                // same session shape yields the same rows.
                std::uint64_t rng = deferred_gx_stats_rng_;
                rng ^= rng >> 12;
                rng ^= rng << 25;
                rng ^= rng >> 27;
                deferred_gx_stats_rng_ = rng;
                const std::uint64_t index =
                    (rng * 2685821657736338717ull) % deferred_gx_stats_seen_;
                if (index < deferred_gx_stats_.size()) {
                    deferred_gx_stats_[static_cast<std::size_t>(index)] = sample;
                }
                // No stderr write on this thread: render_frame_on_thread is the
                // critical path and redirected stderr can block. That rule is why
                // emit_deferred_gx_stats() exists as a separate shutdown-time dump.
                if (deferred_gx_stats_dropped_ == 0u) {
                    deferred_gx_stats_first_drop_frame_ = frame_index_;
                }
                ++deferred_gx_stats_dropped_;
            }
        }
        stat_palette_uploads_ = 0;
        stat_inline_matrix_uploads_ = 0;
    }

    // Retire completed CPU-only persistence without waiting on disk I/O.
    // Explicit diagnostic flushes and shutdown retain their synchronous policy.
    pipeline_cache_.poll_disk_flush();
    constexpr std::uint64_t kFlushInterval = 600;
    if (shader_cache_periodic_flush_enabled() &&
        frame_index_ % kFlushInterval == 0) {
        pipeline_cache_.flush_disk();
    } else if (pipeline_cache_.has_unsaved_records()) {
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

// Routine, clock-free cache-coverage report. The per-miss `[gx-pso-miss]` line is gated on
// GALAXY_TRACE_GX_STALL_US, so a session that built many pipelines below that threshold logged
// almost nothing. This prints only when the on-disk coverage changes, so it costs one comparison
// per frame. If `shader-records-on-disk` keeps tracking `shader-records`, the cache has reached
// full coverage and the remaining misses are new configurations.
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

// flush_draw_state: build shader/render keys, upload constants, return PSO.
// Returns nullptr when no pipeline is ready (draw is skipped).

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
        // `TextureHandleKey::generated_mips` is std::uint8_t, and every sibling field above is explicitly
        // cast. This one was a bare `1u : 0u` ternary, which MSVC rejects as a narrowing conversion in an
        // initializer list (C2397). The runtime target builds at /W4 /WX. The value is 0 or 1 either way,
        // so the cast changes only the diagnostic.
        static_cast<std::uint8_t>(generated_mips ? 1u : 0u),
    };
}

std::array<std::uint32_t, kMaxTextureMaps> GxBackend::capture_sampler_keys(
    std::uint8_t map_mask,
    const std::array<TextureHandle, kMaxTextureMaps>& handles) const {
    std::array<std::uint32_t, kMaxTextureMaps> keys{};
    for (unsigned map = 0u; map < kMaxTextureMaps; ++map) {
        if ((map_mask & (1u << map)) == 0u) {
            continue;
        }
        const TextureHandle& handle = handles[map];
        keys[map] = pack_sampler_key(
            state_.tex_mode(map), handle.generated_mips, handle.mip_levels);
    }
    // Unused slots still get valid zero-key descriptors, but cannot fragment
    // the table cache. map_mask includes both direct and indirect sampling.
    return keys;
}

void GxBackend::clear_frame_texture_binding_table_cache() {
    frame_texture_tables_.clear();
    frame_texture_binding_tables_.clear();
    frame_texture_handle_cache_.clear();
    // Any code that drops these tables also invalidates the remembered binding
    // key. Keeping the two together is what lets flush_draw_state trust a still
    // valid key without re-deriving it from the BP register file on every draw:
    // a key whose table no longer exists can never be answered from the cache.
    current_texture_binding_key_valid_ = false;
}

void GxBackend::clear_texture_binding_table_cache() {
    clear_frame_texture_binding_table_cache();
    // The cross-frame cache is texture_handle_cache_, which the handle lookup below reads. Frame-local
    // tables in frame_texture_binding_tables_ own CPU descriptors valid only for the current frame, so
    // they cannot persist across frames.
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

ID3D12PipelineState* GxBackend::flush_draw_state(
    D3D12_PRIMITIVE_TOPOLOGY_TYPE primitive_topology_type) {
    // Per-frame decision, hoisted out of the per-draw path.
    const bool time_detail = frame_time_detail_;
    // Always-on, clock-free. This is the denominator for every *_us field below. Without it, a
    // flush-us total cannot be attributed to a few slow flushes or many cheap ones, and those two
    // readings call for different fixes. One increment on a per-frame member.
    ++frame_flush_calls_;
    const auto topology_key =
        static_cast<std::uint8_t>(primitive_topology_type);
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
    if (!dirty_needs_body &&
        !texture_bindings_dirty_ &&
        !frame_bindings_dirty_ &&
        (!matrix_palette_stale_ || !current_palette_required_) &&
        !inline_matrices_stale_ &&
        rt_follow_pending_ == 0u &&
        current_pipeline_ != nullptr &&
        !current_pipeline_is_uber_ &&
        current_vs_constants_ != 0 &&
        current_ps_constants_ != 0 &&
        (current_texture_map_mask_ == 0u ||
         current_texture_table_.ptr != 0) &&
        render_state_key_.primitive_topology == topology_key) {
        ++frame_pso_route_early_;
        return current_pipeline_;
    }

    // Intrusive/sample-frame body timing excludes the reuse decision. Ordinary
    // play must not pay two diagnostic clock reads for every body path.
    // Sampled whole-call timing remains separate and includes the preflight.
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

    // sampled_texture_map_mask walks every TEV stage (a tev_order decode plus an IND_CMD read per
    // stage). Per-object LOAD_INDX matrix uploads set kDirtyXfMatrices and nothing that can move the
    // mask, so recompute it only when a flush carries kDirtyTev, or when the state reset dropped the
    // memo.
    if (!current_texture_map_mask_valid_ || tev_dirty) {
        current_texture_map_mask_ = sampled_texture_map_mask(state_);
        current_texture_map_mask_valid_ = true;
    }
    const std::uint8_t texture_map_mask = current_texture_map_mask_;
    // TextureBindingKey is a pure function of (map_mask, the sampled maps'
    // TexMode0/1, TexImage0/3 and TexTlut words). All of those are BP registers
    // that set GxState::kDirtyTex, and every path that drops the frame-local
    // table cache also clears current_texture_binding_key_valid_ (see
    // clear_frame_texture_binding_table_cache). Reuse the last capture unless
    // one of those inputs is known to have moved; a busy replayed display list
    // rebinds the same materials for thousands of consecutive draws.
    const bool texture_binding_key_fresh =
        current_texture_binding_key_valid_ &&
        dirty != ~0u &&
        (dirty & GxState::kDirtyTex) == 0u &&
        !texture_bindings_dirty_ &&
        texture_map_mask == current_texture_binding_key_mask_;
    const TextureBindingKey texture_binding_key = texture_binding_key_fresh
        ? current_texture_binding_key_
        : capture_texture_binding_key(texture_map_mask);
    // A dirty BP input requires a fresh capture, but writes to unused maps,
    // image1/2 or restored register values can leave the effective key equal.
    // Explicit frame/resource invalidation still forces work through tex_dirty.
    const bool texture_binding_changed =
        !current_texture_binding_key_valid_ ||
        (!texture_binding_key_fresh &&
         !(texture_binding_key == current_texture_binding_key_));
    const bool tex_dirty = frame_bindings_dirty_ ||
        texture_bindings_dirty_ || texture_binding_changed;

    if (tev_dirty || !shader_key_hashes_valid_) {
        const auto next = build_pixel_shader_key(state_, renderer_.efb_scale());
        if (!shader_key_hashes_valid_ || !(next == ps_key_)) {
            ps_key_ = next;
            current_ps_hash_ = ps_key_.hash();
        }
    }
    if (xf_dirty || vcd_dirty || !shader_key_hashes_valid_) {
        const auto next = build_vertex_shader_key(state_);
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
                          renderer_.rt_glow_exposure() > 1.0f;
        const auto flags = static_cast<std::uint8_t>(glow ? (ps_key_.output_flags | 0x20u)
                                                          : (ps_key_.output_flags & ~0x20u));
        if (flags != ps_key_.output_flags) {
            ps_key_.output_flags = flags;
            current_ps_hash_ = ps_key_.hash();
            glow_changed = true;
        }
    }
    // Per-pixel lighting needs the extra position/normal interpolants, which
    // the line and point geometry shaders do not carry.
    {
        const bool ppl = per_pixel_lighting_ && (vs_key_.flags & 0x02u) != 0u &&
            topology_key == static_cast<std::uint8_t>(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        if (((vs_key_.flags & kVsKeyPerPixelLighting) != 0u) != ppl) {
            vs_key_.flags ^= kVsKeyPerPixelLighting;
            current_vs_hash_ = vs_key_.hash();
        }
        if (((ps_key_.reserved & kPsKeyPerPixelLighting) != 0u) != ppl) {
            ps_key_.reserved ^= kPsKeyPerPixelLighting;
            current_ps_hash_ = ps_key_.hash();
        }
    }
    shader_key_hashes_valid_ = true;
    if (rs_dirty) {
        render_state_key_ = build_render_state_key(state_);
    }
    const bool topology_dirty =
        render_state_key_.primitive_topology != topology_key;
    render_state_key_.primitive_topology = topology_key;
    {
        const BlendMode marks = state_.blend_mode();
        alpha_marks_drawn_ |= !marks.color_update && marks.alpha_update;
        // GALAXY_TRACE_DRAW_ORDER=N: every draw of chunk frames N to N+7 with its
        // scene phase, layer, blend, depth and projection (diagnostics).
        static const std::uint64_t trace_from = read_env_u64("GALAXY_TRACE_DRAW_ORDER", 0u);
        if (trace_from != 0u && frame_index_ >= trace_from && (frame_index_ - trace_from) < 8u &&
            frame_index_ < trace_from + 6000u) [[unlikely]] {
            const ZMode z = state_.z_mode();
            const auto xf_float = [this](std::uint16_t address) {
                return std::bit_cast<float>(state_.xf(address));
            };
            std::fprintf(stderr,
                         "[draw-order] frame=%llu scene=%d phase=%u layer=%u blend=%d src=%u dst=%u cu=%d au=%d "
                         "z=%d/%d proj=%c vp=%.0f,%.0f,%.0f,%.0f stages=%u chan=%08x\n",
                         static_cast<unsigned long long>(frame_index_), scene_phase_ ? 1 : 0,
                         static_cast<unsigned>(rt_phase_), static_cast<unsigned>(side_layer_),
                         marks.blend_enable ? 1 : 0, static_cast<unsigned>(marks.src_factor),
                         static_cast<unsigned>(marks.dst_factor), marks.color_update ? 1 : 0,
                         marks.alpha_update ? 1 : 0, z.test_enable ? 1 : 0, z.update_enable ? 1 : 0,
                         state_.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 6u)) == 0u ? 'P' : 'O',
                         static_cast<double>(xf_float(xf::kViewportBase)),
                         static_cast<double>(xf_float(static_cast<std::uint16_t>(xf::kViewportBase + 1u))),
                         static_cast<double>(xf_float(static_cast<std::uint16_t>(xf::kViewportBase + 3u))),
                         static_cast<double>(xf_float(static_cast<std::uint16_t>(xf::kViewportBase + 4u))),
                         static_cast<unsigned>(state_.gen_mode().num_tev_stages), state_.xf(xf::kChannelCtrlBase));
        }
    }
    if (renderer_.rt_active()) {
        // Traced geometry: unblended triangles (no logic op) that write colour and depth under a
        // perspective projection. Shadow volumes, z-only passes and screen quads stay out, and so
        // do alpha-tested cut-outs (foliage, fences) and z-textured surfaces, whose depth is not
        // their triangles'.
        const std::uint32_t bb = render_state_key_.blend_bits;
        rt_state_eligible_ =
            topology_key == static_cast<std::uint8_t>(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE) &&
            (bb & ((1u << 0u) | (1u << 8u))) == 0u && (bb & (1u << 25u)) != 0u &&
            (render_state_key_.zmode_bits & (1u << 4u)) != 0u &&
            render_state_key_.cull != static_cast<std::uint8_t>(CullMode::All) &&
            state_.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 6u)) == 0u &&
            (ps_key_.flags & 0x02u) == 0u && alpha_test_keeps_all(ps_key_);
        const BlendMode blend = state_.blend_mode();
        rt_glow_eligible_ =
            topology_key == static_cast<std::uint8_t>(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE) && blend.blend_enable &&
            !blend.logic_op_enable && !blend.subtract && blend.color_update && blend.dst_factor == BlendFactor::One &&
            render_state_key_.cull != static_cast<std::uint8_t>(CullMode::All) &&
            state_.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 6u)) == 0u;
    }
    // Motion writers: opaque triangles with color writes in a temporal scene
    // (blending, logic ops and dual-source alpha cannot share a second
    // target). Keys only change while a motion scene is (or was) active.
    // Blended triangles (no dual-source alpha) write the reactive mask.
    bool motion_key_changed = false;
    if (motion_scene_ || motion_bits_set_) [[unlikely]] {
        const std::uint32_t bb = render_state_key_.blend_bits;
        const bool candidate = motion_scene_ &&
            topology_key == static_cast<std::uint8_t>(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE) &&
            (bb & ((1u << 8) | (1u << 27))) == 0u && (bb & (1u << 25)) != 0u;
        const bool writer = candidate && (bb & 1u) == 0u;
        const bool reactive = candidate && (bb & 1u) != 0u;
        if (((vs_key_.flags & kVsKeyMotion) != 0u) != writer) {
            vs_key_.flags ^= kVsKeyMotion;
            current_vs_hash_ = vs_key_.hash();
            motion_key_changed = true;
        }
        const std::uint8_t ps_bits =
            (writer ? kPsKeyMotion : 0u) | (reactive ? kPsKeyReactive : 0u);
        if ((ps_key_.reserved & (kPsKeyMotion | kPsKeyReactive)) != ps_bits) {
            ps_key_.reserved = static_cast<std::uint8_t>(
                (ps_key_.reserved & ~(kPsKeyMotion | kPsKeyReactive)) | ps_bits);
            current_ps_hash_ = ps_key_.hash();
            motion_key_changed = true;
        }
        const std::uint32_t rs_bits = (writer ? kRenderStateMotionTarget : 0u) |
                                      (reactive ? kRenderStateReactiveTarget : 0u);
        if ((bb & (kRenderStateMotionTarget | kRenderStateReactiveTarget)) != rs_bits) {
            render_state_key_.blend_bits =
                (bb & ~(kRenderStateMotionTarget | kRenderStateReactiveTarget)) | rs_bits;
            motion_key_changed = true;
        }
        motion_bits_set_ = writer || reactive;
    }

    if (current_pipeline_ == nullptr || current_pipeline_is_uber_ ||
        tev_dirty || xf_dirty || vcd_dirty || rs_dirty || topology_dirty ||
        motion_key_changed) {
        const PsoKey pso_key{
            current_vs_hash_,
            current_ps_hash_,
            render_state_key_,
        };
        const std::uint64_t pso_key_hash = pso_key.hash();
        const auto pso_start = time_detail
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        // Explicit raw-cycle diagnostics for this PSO-resolution interval.
        // Includes instrumentation executed inside the interval. Compare only
        // matched operating points; cycle share alone does not identify waits.
        const ThreadCounterSample pso_cycles_start = pso_cycles_enabled
            ? thread_cycle_count() : ThreadCounterSample{};
        // Reject an unchanged key before touching the memo or the published
        // map. A matrix-only flush uploads owed snapshots below without
        // entering this key-resolution block. For other dirt with an unchanged
        // key, the memo probe and the map probe below could only ever
        // return the pipeline already in `current_pipeline_`: current_vs_hash_,
        // current_ps_hash_, render_state_key_ and the topology are all
        // matrix-independent, and the palette is uploaded below by its own
        // stale flag rather than through the PSO.
        //
        // The memoized hash is tested first, because a differing hash proves
        // the keys differ: that rejects the common "different material" case
        // with one 64-bit compare instead of the full 32-byte key comparison.
        // An equal hash still confirms the key exactly, so this cannot change
        // which pipeline is resolved.
        const bool pso_key_changed =
            current_pipeline_ == nullptr ||
            current_pipeline_is_uber_ ||
            !current_pso_key_valid_ ||
            current_pso_key_hash_ != pso_key_hash ||
            !(current_pso_key_ == pso_key);
        if (pso_key_changed) {
            // Reached the resolution decision at all. Counted unconditionally
            // so the three routes below always sum to this and nothing is
            // inferred from a possibly-uninstrumented sibling counter.
            ++frame_pso_resolutions_;
            // Resolve through the frame-scoped memo first. The same PsoKey
            // recurs many times per frame (one material alternating with a
            // few others), and the published map cannot change between
            // begin_frame()'s drain_completions() and the last draw of the
            // frame, so a hit here returns exactly what get() would return.
            ID3D12PipelineState* resolved = nullptr;
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
                // Always-on, not gated by time_detail: the memo hit ratio and
                // the get() call count are the two numbers that decide whether
                // this block is the frame's critical path, and reading them
                // must not require the instrumentation that perturbs it. One
                // increment on a per-frame member, no branch added.
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
        // Required pipeline creation failed. Stop instead of dropping the draw.
        throw std::runtime_error(
            "[GxBackend] failed to create required graphics pipeline");
    }
    current_pipeline_aux_ = current_pipeline_is_uber_ ? 0u
        : (render_state_key_.blend_bits & kRenderStateMotionTarget) != 0u ? 1u
        : (render_state_key_.blend_bits & kRenderStateReactiveTarget) != 0u ? 2u : 0u;

    // Snapshot the XF matrix palette only when the matrices changed (kDirtyXfMatrices). Re-uploading
    // the full palette on every TEV or render-state change exhausts the per-frame matrix ring, which
    // fails with "UploadRing 'matrix' exhausted". Draws whose matrices are unchanged reuse the last
    // snapshot's GPU VA, which stays valid until begin_frame() rewinds the ring at the next frame.
    // The in-shader palette base is 0 because the root SRV is bound at the snapshot's own VA.
    //
    // The snapshot is kMatrixPaletteWords * 4 = 6656 bytes (XF 0x0000-0x067F) and stays a whole-palette
    // copy: a fresh ring allocation has no inherited contents, so its head and tail must be written
    // even when a single 4-row matrix moved. `xf_palette_valid_` saves the allocation on draws where
    // nothing moved; it does not shrink the copy.
    const unsigned active_texgen_mask =
        vs_key_.num_texgens >= 8u
            ? 0xFFu
            : ((1u << vs_key_.num_texgens) - 1u);
    bool emboss_palette_required = false;
    for (unsigned texgen = 0; texgen < vs_key_.num_texgens; ++texgen) {
        emboss_palette_required |= state_.tex_gen(texgen).type == TexGenType::EmbossMap;
    }
    const bool palette_required =
        emboss_palette_required ||
        (vs_key_.flags & 0x03u) != 0u ||
        (vs_key_.texmtx_idx_mask & active_texgen_mask) != 0u;
    current_palette_required_ = palette_required;
    // Actor lights (actor_lights.h): a lit scene draw's follow-camera lights are moved in its
    // snapshot; one moved otherwise (other lights, another scene's aim) is not reused for it. A
    // light load's positions are taken at the first draw after it (rt_follow_pending_ needs an aim).
    if (rt_light_aim_.valid || rt_palette_lights_ != 0u) [[unlikely]] {
        const std::uint64_t start = trace_actor_lights_enabled() ? __rdtsc() : 0u;
        for (unsigned n = 0; n < 2u; ++n) {
            if ((rt_follow_pending_ & (1u << n)) != 0u) {
                std::memcpy(rt_follow_words_[n].data(),
                            state_.xf_raw() + xf::kLightsBase + kXfLightWords * n + kXfLightPosition,
                            sizeof(rt_follow_words_[n]));
            }
        }
        rt_follow_pending_ = 0u;
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
            rt_light_cycles_ += __rdtsc() - start;
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
        // This block can run per draw. Keep clock sampling opt-in; routine
        // upload counts/bytes below remain independent of detailed timing.
        const auto matrix_start = time_detail
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        const std::size_t palette_bytes =
            kMatrixPaletteWords * sizeof(std::uint32_t);
        UploadRing::Allocation mtx_alloc{};
        // Dirty chunks decide whether the existing allocation is reusable. If not, the new immutable
        // allocation needs the complete XF palette: its unwritten head and tail have no inherited valid
        // contents, and patching a prior allocation could change already recorded draws. One complete
        // copy replaces the former head, middle and tail copies.
        const std::uint64_t palette_todo =
            (state_.xf_palette_dirty() | ~xf_palette_valid_) &
            xf_palette_chunk_mask;
        if (palette_todo != 0u) {
            const std::uint64_t snapshot_start = trace_actor_lights_enabled() ? __rdtsc() : 0u;
            mtx_alloc =
                renderer_.matrix_ring().allocate(palette_bytes, /*alignment=*/256u);
            std::memcpy(mtx_alloc.cpu, state_.xf_raw(), palette_bytes);
            if (snapshot_start != 0u) [[unlikely]] {
                rt_snapshot_cycles_ += __rdtsc() - snapshot_start;
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
            ++stat_palette_uploads_;
            frame_matrix_upload_bytes_ += palette_bytes;
        }
        // `palette_todo == 0` means every chunk is already valid in this frame
        // slot, which also means a previous upload set `current_matrix_palette_`
        // to a GPU address inside this frame's segment. A fresh allocation would
        // burn 6.6 KB of the matrix ring only to store bytes that are already
        // there, so reuse the address and skip it.
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
    if (vs_c_dirty || xf_dirty || vcd_dirty || inline_matrices_stale_ ||
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
        ++stat_inline_matrix_uploads_;
        if (time_detail) {
            frame_flush_constants_us_ +=
                elapsed_us(constants_start, std::chrono::steady_clock::now());
        }
    }

    // Upload PS constants when TEV stage config / fog / alpha / tex dims change.
    if (tev_dirty || tev_c_dirty || tex_dirty || frame_bindings_dirty_ || glow_changed ||
        dirty == ~0u || (ps_record_restart_ && history_scene_)) {
        const auto constants_start = time_detail
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        GxPsConstants ps_c{};
        fill_ps_constants(state_, ps_c);
        ps_c.ztex_params[1] = 1.0f / static_cast<float>(renderer_.efb_scale());
        ps_c.ztex_params[2] = (ps_key_.output_flags & 0x20u) != 0u ? renderer_.rt_glow_exposure() : 0.0f;
        ps_c.ztex_params[3] = (ps_key_.output_flags & 0x20u) != 0u ? renderer_.rt_glow_spill() : 0.0f;
        apply_uber_ps_key(ps_key_, ps_c);

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

    // Refresh the texture descriptor table when any texture register changed.
    //
    // `tex_dirty` is the complete set of inputs this block consumes:
    //   - `frame_bindings_dirty_` and `texture_bindings_dirty_` are its own two terms, and
    //   - `texture_binding_changed` is true whenever the captured `TextureBindingKey` differs from
    //     the remembered one. A capture is taken only when one of those two flags is set or
    //     `kDirtyTex` moved, and `current_texture_binding_key_valid_` is false while those flags are
    //     set. So "the key changed" can only be observed under `tex_dirty`. With `tex_dirty == false`,
    //     the capture this flush would take is byte-identical to the applied one, and the probe below
    //     can only return `current_texture_table_` and `current_sampler_table_` unchanged.
    //
    // `dirty_texture_aliases_` is the one piece of state that outlives a single flush.
    // `prune_dirty_texture_binding_tables()` clears it, and the tables it prunes are keyed by the
    // previous flush's binding key. It is still consulted when non-empty, even with `tex_dirty` false.
    // Otherwise an EFB-alias copy followed by matrix-only flushes would accumulate entries for the rest
    // of the frame.
    //
    // This guard is the steady state during gameplay, not an edge case. SMG re-uploads XF matrices per
    // object via LOAD_INDX, so after the frame's first draw nearly every flush carries
    // `kDirtyXfMatrices` alone. Without this guard, each draw would pay a binding-key hash and a map
    // probe to re-derive a result that cannot have moved. `dirty == ~0u` is unreachable here: a
    // full-dirty flush always leaves `dirty_needs_body` true, and the early-out above requires all
    // three binding flags to be false.
    if (tex_dirty || !dirty_texture_aliases_.empty()) {
        const auto texture_start = time_detail
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        std::uint64_t texture_get_us = 0;
        std::uint64_t texture_table_us = 0;
        std::uint64_t sampler_table_us = 0;
        ID3D12Device* dev = renderer_.device();
        const UINT srv_stride = texture_cache_.srv_descriptor_stride();
        if (texture_bindings_dirty_) {
            prune_dirty_texture_binding_tables();
        }

        // A draw that samples no map still gets real tables (null SRVs and
        // the default samplers): the renderer skips null tables, which leaves
        // the root tables undefined after a command-list reset, and an uber
        // pixel shader samples every enabled map even when the combiner
        // ignores it (an AMD iGPU page-faulted on exactly that).
        {
            bool binding_cache_hit = false;
            // Texture tables point into the frame-local shader-visible SRV
            // segment, so only the per-frame binding cache may own them.
            if (const auto frame_binding_it =
                    frame_texture_binding_tables_.find(texture_binding_key);
                frame_binding_it != frame_texture_binding_tables_.end()) {
                current_texture_table_ =
                    frame_binding_it->second.texture_table;
                current_sampler_table_ =
                    frame_binding_it->second.sampler_table;
                for (const auto& handle : frame_binding_it->second.texture_handles) {
                    texture_cache_.touch_cached_handle(handle);
                }
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
                    const unsigned num_maps = kMaxTextureMaps;
                    DescriptorRing::Table tbl =
                        renderer_.srv_ring().allocate(num_maps);

                    D3D12_CPU_DESCRIPTOR_HANDLE sources[kMaxTextureMaps]{};
                    const auto heap_start =
                        texture_cache_.srv_cpu_base();
                    for (unsigned m = 0; m < num_maps; ++m) {
                        const TextureHandle& th = texture_handles[m];
                        if (th.resource != nullptr) {
                            sources[m] = D3D12_CPU_DESCRIPTOR_HANDLE{
                                heap_start.ptr
                                + static_cast<SIZE_T>(th.srv_index) *
                                    srv_stride
                            };
                        } else {
                            sources[m] = texture_cache_.null_srv();
                        }
                    }
                    // CPU-only sources, one contiguous frame-owned table.
                    const UINT destination_size = num_maps;
                    dev->CopyDescriptors(1, &tbl.cpu, &destination_size,
                        num_maps, sources, nullptr,
                        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                    // Interpolation logs keep tables that name EFB-copy
                    // textures: sub-frames repoint those slots.
                    if (renderer_.replay_recording()) [[unlikely]] {
                        static_assert(kMaxTextureMaps == RendererD3D12::kReplayTableSlots);
                        ID3D12Resource* copies[kMaxTextureMaps]{};
                        bool names_copy = false;
                        for (unsigned m = 0; m < num_maps; ++m) {
                            if (texture_handles[m].resource != nullptr &&
                                texture_handles[m].from_efb_copy) {
                                copies[m] = texture_handles[m].resource;
                                names_copy = true;
                            }
                        }
                        if (names_copy) {
                            renderer_.note_replay_texture_table(tbl.gpu, sources, copies);
                        }
                    }
                    current_texture_table_ = tbl.gpu;
                    frame_texture_tables_.emplace(table_key, tbl.gpu);
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
               lhs.xf_viewport,
               rhs.xf_viewport,
               sizeof(lhs.xf_viewport)) == 0;
}

void GxBackend::flush_pending_draw_batch() {
    if (!pending_draw_batch_.active) {
        return;
    }

    // Per-frame decision, hoisted out of the per-draw path.
    const bool time_detail = frame_time_detail_;
    const auto draw_record_start = time_detail
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    if (!pending_stripe_vertices_.empty()) [[unlikely]] {
        renderer_.note_stripe_vertices(pending_stripe_vertices_);
        pending_stripe_vertices_.clear();
    }
    if (pending_draw_batch_.rt) {
        renderer_.rt_note_draw(pending_draw_batch_.call, pending_draw_batch_.rt_info);
    }
    renderer_.draw(pending_draw_batch_.call);
    if (time_detail) {
        frame_draw_record_us_ +=
            elapsed_us(draw_record_start, std::chrono::steady_clock::now());
    }
    ++frame_draw_batches_;
    pending_draw_batch_ = {};
}

// FifoSink callbacks

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

bool GxBackend::begin_cached_draw_run(
    PrimitiveClass primitive,
    std::uint8_t vtxfmt,
    std::size_t draw_count) {
    (void)draw_count;
    if (cached_draw_run_active_) {
        throw std::runtime_error(
            "GxBackend: nested cached display-list draw run");
    }
    if (frame_dependency_event_sink_ != nullptr ||
        capture_draws_ || capture_texaddr_target() != 0u ||
        trace_thp_draw_frame_enabled(frame_index_ + 1u)) {
        return false;
    }

    D3D12_PRIMITIVE_TOPOLOGY topology{};
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topology_type{};
    gx_topology_for_primitive(primitive, topology, topology_type);

    const EfbScissor efb_sc = state_.efb_scissor();
    const ScissorRect sc = efb_sc.rect;
    // The legacy rejection counter also covers fully culled polygons.
    // Lines and points remain visible under GX_CULL_ALL.
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

    ID3D12PipelineState* pso = nullptr;
    {
        ScopedFlushTiming flush_timing{
            frame_flush_timed_calls_,
            frame_flush_total_us_,
            frame_flush_timed_samples_};
        pso = flush_draw_state(topology_type);
    }
    if (pso == nullptr) {
        flush_pending_draw_batch();
        cached_draw_run_no_pso_ = true;
        return true;
    }

    D3D12_RECT scissor_rect{};
    scissor_rect.left   = static_cast<LONG>(sc.x0 < 0 ? 0 : sc.x0);
    scissor_rect.top    = static_cast<LONG>(sc.y0 < 0 ? 0 : sc.y0);
    scissor_rect.right  = static_cast<LONG>(sc.x1 + 1);
    scissor_rect.bottom = static_cast<LONG>(sc.y1 + 1);

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
    cached_draw_run_state_call_.topology       = topology;
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

bool GxBackend::on_cached_simple_draw_run(
    PrimitiveClass primitive,
    std::uint8_t vtxfmt,
    std::span<const std::byte> bytes,
    std::size_t base_offset,
    std::span<const CachedDrawPacket> packets) {
    ++frame_replay_simple_batches_;
    if (frame_dependency_event_sink_ != nullptr ||
        packets.size() <= 1u || cached_draw_run_active_) {
        return false;
    }

    std::uint32_t total_vertices = 0;
    std::size_t total_payload_bytes = 0;
    for (const CachedDrawPacket& packet : packets) {
        if (packet.vertex_count == 0u) {
            continue;
        }
        if (total_vertices >
            static_cast<std::uint32_t>(
                std::numeric_limits<std::uint16_t>::max()) -
                packet.vertex_count) {
            return false;
        }
        const std::size_t expected_payload =
            draw_payload_size(vtxfmt, packet.vertex_count);
        if (packet.draw_payload_size != expected_payload ||
            packet.draw_cursor_offset + 2u + packet.draw_payload_size >
                bytes.size()) {
            throw GxFatalError(
                "GX FIFO: cached triangle draw packet payload mismatch",
                base_offset + packet.local_opcode_offset,
                packet.opcode);
        }
        total_vertices += packet.vertex_count;
        total_payload_bytes += packet.draw_payload_size;
    }
    if (total_vertices == 0u) {
        return true;
    }

    cached_simple_draw_scratch_.resize(2u + total_payload_bytes);
    cached_simple_draw_scratch_[0] = static_cast<std::byte>(
        (total_vertices >> 8) & 0xFFu);
    cached_simple_draw_scratch_[1] = static_cast<std::byte>(
        total_vertices & 0xFFu);
    std::size_t dst = 2u;
    for (const CachedDrawPacket& packet : packets) {
        if (packet.vertex_count == 0u) {
            continue;
        }
        const std::size_t src = packet.draw_cursor_offset + 2u;
        std::memcpy(
            cached_simple_draw_scratch_.data() + dst,
            bytes.data() + src,
            packet.draw_payload_size);
        dst += packet.draw_payload_size;
    }

    struct CachedRunScope {
        GxBackend* backend = nullptr;
        bool active = false;
        ~CachedRunScope() {
            if (active && backend != nullptr) {
                backend->end_cached_draw_run();
            }
        }
    };

    if (!begin_cached_draw_run(primitive, vtxfmt, packets.size())) {
        return false;
    }
    CachedRunScope scope{this, true};

    if (cached_draw_run_scissor_reject_) {
        frame_scissor_skips_ += packets.size();
        return true;
    }
    if (cached_draw_run_no_pso_) {
        stat_draws_no_pso_ += packets.size();
        frame_draws_no_pso_ += packets.size();
        return true;
    }

    FifoCursor cursor;
    cursor.data = std::span<const std::byte>(
        cached_simple_draw_scratch_.data(),
        cached_simple_draw_scratch_.size());
    cursor.offset = 0;
    cursor.base_offset = base_offset;
    cursor.command_offset = base_offset + packets.front().local_opcode_offset;
    cursor.recover_truncation = false;

    const std::uint64_t issued_before = frame_draws_issued_;
    on_cached_draw_run_draw(primitive, vtxfmt, cursor);
    if (cursor.offset != cached_simple_draw_scratch_.size()) {
        throw GxFatalError(
            "GX FIFO: cached draw run consumed wrong size",
            base_offset + packets.front().local_opcode_offset,
            packets.front().opcode);
    }
    if (frame_draws_issued_ > issued_before && packets.size() > 1u) {
        const std::uint64_t extra = packets.size() - 1u;
        stat_draws_issued_ += extra;
        frame_draws_issued_ += extra;
    }
    return true;
}

bool GxBackend::on_cached_prepared_draw_run(
    PrimitiveClass primitive,
    std::uint8_t vtxfmt,
    std::span<const std::byte> prepared_payload,
    std::size_t base_offset,
    std::size_t local_opcode_offset,
    std::uint8_t opcode,
    std::size_t source_draw_count) {
    ++frame_replay_prepared_batches_;
    if (source_draw_count <= 1u || cached_draw_run_active_) {
        return false;
    }
    if (prepared_payload.size() < 2u) {
        throw GxFatalError(
            "GX FIFO: prepared cached draw run missing vertex count",
            base_offset + local_opcode_offset,
            opcode);
    }

    struct CachedRunScope {
        GxBackend* backend = nullptr;
        bool active = false;
        ~CachedRunScope() {
            if (active && backend != nullptr) {
                backend->end_cached_draw_run();
            }
        }
    };

    if (!begin_cached_draw_run(primitive, vtxfmt, source_draw_count)) {
        return false;
    }
    CachedRunScope scope{this, true};

    if (cached_draw_run_scissor_reject_) {
        frame_scissor_skips_ += source_draw_count;
        return true;
    }
    if (cached_draw_run_no_pso_) {
        stat_draws_no_pso_ += source_draw_count;
        frame_draws_no_pso_ += source_draw_count;
        return true;
    }

    FifoCursor cursor;
    cursor.data = prepared_payload;
    cursor.offset = 0;
    cursor.base_offset = base_offset;
    cursor.command_offset = base_offset + local_opcode_offset;
    cursor.recover_truncation = false;

    const std::uint64_t issued_before = frame_draws_issued_;
    on_cached_draw_run_draw(primitive, vtxfmt, cursor);
    if (cursor.offset != prepared_payload.size()) {
        throw GxFatalError(
            "GX FIFO: prepared cached draw run consumed wrong size",
            base_offset + local_opcode_offset,
            opcode);
    }
    if (frame_draws_issued_ > issued_before && source_draw_count > 1u) {
        const std::uint64_t extra = source_draw_count - 1u;
        stat_draws_issued_ += extra;
        frame_draws_issued_ += extra;
    }
    return true;
}

bool GxBackend::on_cached_packet_draw_run(
    std::uint64_t cache_token,
    std::size_t packet_run_index,
    PrimitiveClass primitive,
    std::uint8_t vtxfmt,
    std::span<const std::byte> bytes,
    std::size_t base_offset,
    std::span<const CachedDrawPacket> packets,
    std::uint32_t total_vertices,
    std::uint32_t total_indices,
    std::span<const std::uint16_t> precomputed_indices) {
    ++frame_replay_packet_batches_;
    if (packets.size() <= 1u || cached_draw_run_active_) {
        return false;
    }

    struct CachedRunScope {
        GxBackend* backend = nullptr;
        bool active = false;
        ~CachedRunScope() {
            if (active && backend != nullptr) {
                backend->end_cached_draw_run();
            }
        }
    };

    if (!begin_cached_draw_run(primitive, vtxfmt, packets.size())) {
        return false;
    }
    CachedRunScope scope{this, true};

    if (cached_draw_run_scissor_reject_) {
        frame_scissor_skips_ += packets.size();
        return true;
    }
    if (cached_draw_run_no_pso_) {
        stat_draws_no_pso_ += packets.size();
        frame_draws_no_pso_ += packets.size();
        return true;
    }

    auto replay_packet_by_packet = [&]() -> bool {
        for (const CachedDrawPacket& packet : packets) {
            if (packet.draw_cursor_offset + 2u + packet.draw_payload_size >
                bytes.size()) {
                throw GxFatalError(
                    "GX FIFO: cached packet draw run packet out of range",
                    base_offset + packet.local_opcode_offset,
                    packet.opcode);
            }

            FifoCursor cursor;
            cursor.data = bytes;
            cursor.offset = packet.draw_cursor_offset;
            cursor.base_offset = base_offset;
            cursor.command_offset = base_offset + packet.local_opcode_offset;
            cursor.recover_truncation = false;

            on_cached_draw_run_draw(primitive, vtxfmt, cursor);
            const std::size_t expected_offset =
                packet.draw_cursor_offset + 2u + packet.draw_payload_size;
            if (cursor.offset != expected_offset) {
                throw GxFatalError(
                    "GX FIFO: cached packet draw run consumed wrong size",
                    base_offset + packet.local_opcode_offset,
                    packet.opcode);
            }
        }
        return true;
    };

    const VertexLayoutCacheEntry& layout = cached_vertex_layout(vtxfmt);
    const bool parser_provided_totals =
        total_vertices != 0u || total_indices != 0u;
    if (total_vertices == 0u && total_indices == 0u) {
        for (const CachedDrawPacket& packet : packets) {
            const bool packet_out_of_range =
                packet.draw_cursor_offset > bytes.size() ||
                bytes.size() - packet.draw_cursor_offset < 2u ||
                bytes.size() - packet.draw_cursor_offset - 2u <
                    packet.draw_payload_size;
            const std::size_t expected_payload =
                layout.source_size *
                static_cast<std::size_t>(packet.vertex_count);
            if (packet_out_of_range ||
                packet.draw_payload_size != expected_payload) {
                throw GxFatalError(
                    "GX FIFO: cached packet draw run packet out of range",
                    base_offset + packet.local_opcode_offset,
                    packet.opcode);
            }
            if (packet.vertex_count == 0u) {
                continue;
            }
            if (total_vertices >
                static_cast<std::uint32_t>(
                    std::numeric_limits<std::uint16_t>::max()) -
                    packet.vertex_count) {
                total_vertices =
                    static_cast<std::uint32_t>(
                        std::numeric_limits<std::uint16_t>::max()) + 1u;
                break;
            }
            total_vertices += packet.vertex_count;
        }
    }
    if (total_vertices > static_cast<std::uint32_t>(
                             std::numeric_limits<std::uint16_t>::max())) {
        return replay_packet_by_packet();
    }
    if (total_vertices == 0u) {
        return true;
    }
    // Incomplete triangle packets require per-packet indices to exclude each
    // packet's tail. Source vertex counts still describe allocation extents.
    cached_draw_run_primitive_indexed_ = primitive != PrimitiveClass::Triangles ||
        std::any_of(packets.begin(), packets.end(), [](const CachedDrawPacket& packet) {
            return packet.vertex_count % 3u != 0u;
        });

    bool append_to_pending = false;
    if (pending_draw_batch_.active) {
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
        pending_draw_batch_.vertex_count % 3u != 0u) {
        append_to_pending = false;
        cached_draw_run_batch_compatible_known_ = false;
    }
    if (append_to_pending && cached_draw_run_primitive_indexed_ &&
        pending_draw_batch_.vertex_count + total_vertices >
            std::numeric_limits<std::uint16_t>::max()) {
        flush_pending_draw_batch();
        append_to_pending = false;
        cached_draw_run_batch_compatible_known_ = false;
    }
    if (pending_draw_batch_.active && !append_to_pending) {
        flush_pending_draw_batch();
        cached_draw_run_batch_compatible_known_ = false;
    }

    // Per-frame decision, hoisted out of the per-draw path.
    const bool time_detail = frame_time_detail_;
    const auto vertex_start = time_detail
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    const bool decoded_cache_enabled =
        decoded_vertex_cache_enabled() &&
        cache_token != 0u &&
        total_vertices != 0u &&
        static_cast<std::size_t>(total_vertices) * sizeof(GxVertexOut) <=
            kDecodedPacketRunCacheMaxEntryBytes;
    const DecodedPacketRunCacheKey decoded_cache_key =
        decoded_cache_enabled
            ? decoded_packet_run_cache_key(
                  cache_token,
                  packet_run_index,
                  primitive,
                  vtxfmt,
                  layout.desc)
            : DecodedPacketRunCacheKey{};
    LoadedPrimitive prim{};
    bool decoded_cache_hit = false;
    bool batch_resident = false;
    if (decoded_cache_enabled) {
        if (auto it = decoded_packet_run_cache_.find(decoded_cache_key);
            it != decoded_packet_run_cache_.end()) {
            const bool shape_matches =
                it->second.vertices.size() == total_vertices &&
                (!parser_provided_totals ||
                 it->second.total_indices == total_indices);
            const auto validation_start = time_detail
                ? std::chrono::steady_clock::now()
                : std::chrono::steady_clock::time_point{};
            std::uint64_t validated_generation = 0u;
            const bool dependencies_match =
                shape_matches && decoded_packet_run_dependencies_match(
                    frame_memory_,
                    it->second.validated_generation,
                    decoded_packet_run_generation_,
                    it->second.guest_array_snapshots,
                    validated_generation,
                    frame_memory_regions_disjoint_sorted_);
            if (time_detail) {
                frame_cached_vertex_validation_us_ +=
                    static_cast<std::uint64_t>(std::chrono::duration_cast<
                        std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                  validation_start).count());
            }
            if (shape_matches && !dependencies_match) {
                ++frame_decoded_vertex_cache_stale_rejects_;
            }
            if (dependencies_match) {
                // Record the generation this entry is now valid for, so
                // every later replay of the same key in this frame skips the
                // snapshot compare. 0 is the "never validated" sentinel and is
                // never a live generation (see the invalidation path).
                it->second.validated_generation =
                    validated_generation != 0u ? validated_generation
                                               : decoded_packet_run_generation_;
                it->second.last_used = ++decoded_packet_run_cache_tick_;
                ++frame_decoded_vertex_cache_hits_;
                decoded_cache_hit = true;
                const auto upload_start = time_detail
                    ? std::chrono::steady_clock::now()
                    : std::chrono::steady_clock::time_point{};
                // A validated entry draws straight from its resident block:
                // no vertex bytes move. Only another resident run that ends
                // exactly where this block starts can share its batch.
                DecodedPacketRunCacheEntry& entry = it->second;
                const bool resident = make_decoded_entry_resident(entry);
                const std::size_t resident_byte_offset =
                    static_cast<std::size_t>(entry.resident_offset) *
                    sizeof(GxVertexOut);
                if (append_to_pending &&
                    (pending_draw_batch_.resident != resident ||
                     (resident &&
                      pending_draw_batch_.vertex_byte_end != resident_byte_offset))) {
                    flush_pending_draw_batch();
                    append_to_pending = false;
                    cached_draw_run_batch_compatible_known_ = false;
                }
                const std::uint32_t index_batch_base =
                    append_to_pending && cached_draw_run_primitive_indexed_
                        ? pending_draw_batch_.base_vertex
                        : 0u;
                if (resident) {
                    prim = vertex_loader_.upload_cached_packet_run_indices(
                        resident_byte_offset,
                        static_cast<std::uint32_t>(entry.vertices.size()),
                        base_offset,
                        packets,
                        primitive,
                        renderer_.index_ring(),
                        index_batch_base,
                        append_to_pending && cached_draw_run_primitive_indexed_,
                        entry.total_indices,
                        precomputed_indices);
                    batch_resident = true;
                    ++frame_resident_vertex_hits_;
                    frame_resident_vertex_bytes_ +=
                        entry.vertices.size() * sizeof(GxVertexOut);
                } else {
                    prim = vertex_loader_.upload_cached_packet_run_vertices(
                        entry.vertices,
                        base_offset,
                        packets,
                        primitive,
                        renderer_.vertex_ring(),
                        renderer_.index_ring(),
                        index_batch_base,
                        append_to_pending && cached_draw_run_primitive_indexed_,
                        entry.total_indices,
                        precomputed_indices,
                        immutable_vertex_upload_reuse_enabled()
                            ? &entry.upload_token : nullptr);
                }
                if (time_detail) {
                    frame_cached_vertex_upload_us_ +=
                        static_cast<std::uint64_t>(std::chrono::duration_cast<
                            std::chrono::microseconds>(std::chrono::steady_clock::now() -
                                                      upload_start).count());
                }
            }
        }
    }
    if (!decoded_cache_hit) {
        if (decoded_cache_enabled) {
            ++frame_decoded_vertex_cache_misses_;
        }
        const bool reuse_packet_scratch = !decoded_cache_enabled &&
            static_cast<std::size_t>(total_vertices) * sizeof(GxVertexOut) <=
                kDecodedPacketRunCacheMaxEntryBytes;
        DecodedPacketRunVertices decoded =
            vertex_loader_.decode_cached_packet_run_vertices_with_layout(
                bytes,
                base_offset,
                packets,
                primitive,
                vtxfmt,
                state_,
                layout.desc,
                layout.source_size,
                frame_memory_,
                parser_provided_totals ? total_vertices : 0u,
                parser_provided_totals ? total_indices : 0u,
                // guest_array_reads feeds only the decoded-vertex cache below.
                // With that cache off (the default) the ranges would be
                // accumulated per indexed attribute per vertex and then dropped,
                // so do not ask for them at all.
                decoded_cache_enabled,
                reuse_packet_scratch ? &decoded_packet_vertex_scratch_ : nullptr);
        if (append_to_pending && pending_draw_batch_.resident) {
            // Ring vertices cannot extend a batch drawn from the resident buffer.
            flush_pending_draw_batch();
            append_to_pending = false;
            cached_draw_run_batch_compatible_known_ = false;
        }
        prim = vertex_loader_.upload_cached_packet_run_vertices(
            decoded.vertices,
            base_offset,
            packets,
            primitive,
            renderer_.vertex_ring(),
            renderer_.index_ring(),
            append_to_pending && cached_draw_run_primitive_indexed_
                ? pending_draw_batch_.base_vertex
                : 0u,
            append_to_pending && cached_draw_run_primitive_indexed_,
            decoded.total_indices,
            precomputed_indices);
        const std::size_t decoded_vertex_bytes =
            decoded.vertices.size() * sizeof(GxVertexOut);
        if (reuse_packet_scratch &&
            decoded.vertices.capacity() <=
                kDecodedPacketRunCacheMaxEntryBytes / sizeof(GxVertexOut)) {
            // Bound retained capacity as well as requested size: vector growth
            // may allocate more than the requested vertex count.
            // Upload completed synchronously; the mapped GPU ring owns its copy.
            // No cached entry borrows this vector and the next decode zeroes it.
            decoded.vertices.swap(decoded_packet_vertex_scratch_);
        }
        DecodedPacketRunCacheEntry::GuestDependencySnapshots
            dependency_snapshots;
        std::size_t dependency_snapshot_bytes = 0u;
        const bool dependencies_captured =
            decoded_cache_enabled && !decoded.vertices.empty() &&
            capture_decoded_packet_run_dependencies(
                frame_memory_,
                decoded.guest_array_reads,
                kDecodedPacketRunCacheMaxEntryBytes - decoded_vertex_bytes,
                dependency_snapshots,
                dependency_snapshot_bytes,
                frame_memory_regions_disjoint_sorted_);
        if (dependencies_captured) {
            DecodedPacketRunCacheEntry entry{};
            entry.key = decoded_cache_key;
            entry.total_indices = decoded.total_indices;
            entry.last_used = ++decoded_packet_run_cache_tick_;
            entry.byte_size = decoded_vertex_bytes + dependency_snapshot_bytes;
            // The snapshots were just captured from live guest memory, so this entry is valid for the current
            // generation. The first replay this frame must not re-compare the bytes just copied.
            entry.validated_generation = decoded_packet_run_generation_;
            entry.vertices = std::move(decoded.vertices);
            // Eager invalidation tests overlap against the merged snapshot
            // ranges: the same byte set as the raw per-attribute reads, with
            // the duplicate chunks removed.
            entry.guest_array_reads.clear();
            entry.guest_array_reads.reserve(dependency_snapshots.ranges.size());
            for (const auto& range : dependency_snapshots.ranges) {
                entry.guest_array_reads.push_back(
                    VertexDecodeGuestRange{range.guest_base, range.size});
            }
            entry.guest_array_page_mask = 0u;
            for (const VertexDecodeGuestRange& dependency : entry.guest_array_reads) {
                entry.guest_array_page_mask |= dependency_page_mask(
                    normalize_dependency_guest_addr(dependency.guest_base), dependency.size);
                if (entry.guest_array_page_mask == ~std::uint64_t{0}) break;
            }
            entry.guest_array_snapshots = std::move(dependency_snapshots);
            if (auto existing =
                    decoded_packet_run_cache_.find(decoded_cache_key);
                existing != decoded_packet_run_cache_.end()) {
                decoded_packet_run_cache_bytes_ -= existing->second.byte_size;
                release_decoded_entry_residency(existing->second);
                existing->second = std::move(entry);
                decoded_packet_run_cache_bytes_ += existing->second.byte_size;
            } else {
                decoded_packet_run_cache_bytes_ += entry.byte_size;
                decoded_packet_run_cache_.emplace(
                    decoded_cache_key,
                    std::move(entry));
            }
            prune_decoded_packet_run_cache();
        } else if (decoded_cache_enabled) {
            // A cache entry without a complete immutable dependency snapshot
            // could become a false hit later. Do not retain an older entry for
            // this key when the replacement cannot be validated.
            if (auto existing =
                    decoded_packet_run_cache_.find(decoded_cache_key);
                existing != decoded_packet_run_cache_.end()) {
                decoded_packet_run_cache_bytes_ -= existing->second.byte_size;
                release_decoded_entry_residency(existing->second);
                decoded_packet_run_cache_.erase(existing);
                ++frame_decoded_vertex_cache_evictions_;
            }
        }
    }
    if (time_detail) {
        frame_vertex_load_us_ +=
            elapsed_us(vertex_start, std::chrono::steady_clock::now());
    }

    if (prim.index_count == 0u) {
        if (pending_draw_batch_.active) {
            flush_pending_draw_batch();
            cached_draw_run_batch_compatible_known_ = false;
        }
        return true;
    }
    stat_draws_issued_ += packets.size();
    frame_draws_issued_ += packets.size();

    if (append_to_pending) {
        if (prim.indexed != pending_draw_batch_.indexed) {
            throw GxFatalError(
                "GxBackend: cached packet batch indexing changed inside a run",
                base_offset + packets.front().local_opcode_offset,
                packets.front().opcode);
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
                "GxBackend: cached packet batch upload rings were not contiguous",
                base_offset + packets.front().local_opcode_offset,
                packets.front().opcode);
        }
        pending_draw_batch_.call.index_count += prim.index_count;
        pending_draw_batch_.call.vertex_view.SizeInBytes +=
            prim.vertex_count * static_cast<UINT>(sizeof(GxVertexOut));
        if (prim.indexed) {
            pending_draw_batch_.call.index_view.SizeInBytes +=
                prim.index_count * static_cast<UINT>(sizeof(std::uint16_t));
        }
        pending_draw_batch_.vertex_count += prim.vertex_count;
        pending_draw_batch_.vertex_byte_end = vertex_byte_end;
        pending_draw_batch_.index_count += prim.index_count;
        pending_draw_batch_.draw_count +=
            static_cast<std::uint32_t>(packets.size());
        return true;
    }

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation =
        (batch_resident ? resident_vertex_gpu_
                        : renderer_.vertex_ring().gpu_base())
        + prim.vertex_byte_offset;
    vbv.SizeInBytes =
        prim.vertex_count * static_cast<UINT>(sizeof(GxVertexOut));
    vbv.StrideInBytes = static_cast<UINT>(sizeof(GxVertexOut));

    D3D12_INDEX_BUFFER_VIEW ibv{};
    if (prim.indexed) {
        ibv.BufferLocation =
            renderer_.index_ring().gpu_base()
            + static_cast<std::size_t>(prim.first_index) *
                  sizeof(std::uint16_t);
        ibv.SizeInBytes =
            prim.index_count * static_cast<UINT>(sizeof(std::uint16_t));
        ibv.Format = DXGI_FORMAT_R16_UINT;
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
    pending_draw_batch_.indexed = prim.indexed;
    pending_draw_batch_.base_vertex = prim.base_vertex;
    pending_draw_batch_.vertex_count = prim.vertex_count;
    pending_draw_batch_.vertex_byte_end = vertex_byte_end;
    pending_draw_batch_.first_index = prim.first_index;
    pending_draw_batch_.index_count = prim.index_count;
    pending_draw_batch_.draw_count =
        static_cast<std::uint32_t>(packets.size());
    pending_draw_batch_.resident = batch_resident;
    return true;
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
            cursor, primitive, vtxfmt, state_, layout.desc,
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

    // Per-frame decision, hoisted out of the per-draw path.
    const bool time_detail = frame_time_detail_;
    const auto vertex_start = time_detail
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    const VertexLayoutCacheEntry& layout = cached_vertex_layout(vtxfmt);
    const LoadedPrimitive prim = vertex_loader_.load_with_layout(
        cursor, primitive, vtxfmt, state_, layout.desc, layout.source_size,
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
            prim.vertex_count * static_cast<UINT>(sizeof(GxVertexOut));
        if (prim.indexed) {
            pending_draw_batch_.call.index_view.SizeInBytes +=
                prim.index_count * static_cast<UINT>(sizeof(std::uint16_t));
        }
        pending_draw_batch_.vertex_count += prim.vertex_count;
        pending_draw_batch_.vertex_byte_end = vertex_byte_end;
        pending_draw_batch_.index_count += prim.index_count;
        ++pending_draw_batch_.draw_count;
        return;
    }

    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation =
        renderer_.vertex_ring().gpu_base()
        + prim.vertex_byte_offset;
    vbv.SizeInBytes =
        prim.vertex_count * static_cast<UINT>(sizeof(GxVertexOut));
    vbv.StrideInBytes = static_cast<UINT>(sizeof(GxVertexOut));

    D3D12_INDEX_BUFFER_VIEW ibv{};
    if (prim.indexed) {
        ibv.BufferLocation =
            renderer_.index_ring().gpu_base()
            + static_cast<std::size_t>(prim.first_index) *
                  sizeof(std::uint16_t);
        ibv.SizeInBytes =
            prim.index_count * static_cast<UINT>(sizeof(std::uint16_t));
        ibv.Format = DXGI_FORMAT_R16_UINT;
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
    pending_draw_batch_.indexed = prim.indexed;
    pending_draw_batch_.base_vertex = prim.base_vertex;
    pending_draw_batch_.vertex_count = prim.vertex_count;
    pending_draw_batch_.vertex_byte_end = vertex_byte_end;
    pending_draw_batch_.first_index = prim.first_index;
    pending_draw_batch_.index_count = prim.index_count;
    pending_draw_batch_.draw_count = 1;
    cached_draw_run_batch_compatible_known_ = true;
}

void GxBackend::on_draw(
    PrimitiveClass primitive,
    std::uint8_t vtxfmt,
    FifoCursor& cursor)
{
    static const std::uint64_t abort_after_draws =
        read_env_u64("GALAXY_GX_ABORT_AFTER_DRAWS", 0u);
    if (abort_after_draws != 0u &&
        frame_draws_issued_ + frame_draws_no_pso_ >= abort_after_draws) {
        throw GxFatalError(
            "GX FIFO: excessive draw count in one frame",
            cursor.command_offset,
            0xFFu);
    }
    if (cached_draw_run_active_) {
        on_cached_draw_run_draw(primitive, vtxfmt, cursor);
        return;
    }

    const unsigned draw_index = capture_draw_idx_;
    // Per-frame decision, hoisted out of the per-draw path.
    const bool time_detail = frame_time_detail_;
    D3D12_PRIMITIVE_TOPOLOGY topology{};
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topology_type{};
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
    if (cull_reject || (scissor_reject && !capture_draws_ &&
        capture_texaddr_target() == 0u &&
        !trace_thp_draw_frame_enabled(frame_index_ + 1u))) {
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

    ID3D12PipelineState* pso = nullptr;
    {
        ScopedFlushTiming flush_timing{
            frame_flush_timed_calls_,
            frame_flush_total_us_,
            frame_flush_timed_samples_};
        pso = flush_draw_state(topology_type);
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
            cursor, primitive, vtxfmt, state_, layout.desc,
            layout.source_size, frame_memory_, renderer_.vertex_ring(),
            renderer_.index_ring());
        if (time_detail) {
            frame_vertex_load_us_ +=
                elapsed_us(vertex_start, std::chrono::steady_clock::now());
        }
        return;
    }

    // Map scissor from GX state.
    D3D12_RECT scissor_rect{};
    scissor_rect.left   = static_cast<LONG>(sc.x0 < 0 ? 0 : sc.x0);
    scissor_rect.top    = static_cast<LONG>(sc.y0 < 0 ? 0 : sc.y0);
    scissor_rect.right  = static_cast<LONG>(sc.x1 + 1);
    scissor_rect.bottom = static_cast<LONG>(sc.y1 + 1);

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
    state_call.topology       = topology;
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
        cursor, primitive, vtxfmt, state_, layout.desc, layout.source_size,
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

    const std::uint64_t current_trace_frame = frame_index_ + 1u;
    if (trace_thp_draw_frame_enabled(current_trace_frame)) {
        const std::uint64_t ordinal = frame_draws_issued_;
        const std::uint64_t first_n = trace_thp_draw_first_n();
        const bool force_log =
            trace_thp_draw_all_enabled() && first_n != 0u &&
            ordinal <= first_n;
        const bool exact = looks_like_thp_yuv_draw(state_, primitive, prim);
        if (exact || force_log ||
            (trace_thp_draw_all_enabled() && first_n == 0u)) {
            const unsigned trace_draw_index =
                static_cast<unsigned>(
                    std::min<std::uint64_t>(
                        ordinal,
                        static_cast<std::uint64_t>(
                            std::numeric_limits<unsigned>::max())));
            trace_thp_draw_state(
                current_trace_frame,
                trace_draw_index,
                primitive,
                prim,
                state_,
                ps_key_,
                frame_memory_,
                renderer_.vertex_ring(),
                force_log);
        }
    }

    // Build the vertex / index buffer views from the loaded primitive's ring
    // offsets.  Both rings are persistently mapped upload buffers.
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation =
        renderer_.vertex_ring().gpu_base()
        + prim.vertex_byte_offset;
    vbv.SizeInBytes    =
        prim.vertex_count * static_cast<UINT>(sizeof(GxVertexOut));
    vbv.StrideInBytes  = static_cast<UINT>(sizeof(GxVertexOut));

    D3D12_INDEX_BUFFER_VIEW ibv{};
    if (prim.indexed) {
        ibv.BufferLocation =
            renderer_.index_ring().gpu_base()
            + static_cast<std::size_t>(prim.first_index) *
                  sizeof(std::uint16_t);
        ibv.SizeInBytes =
            prim.index_count * static_cast<UINT>(sizeof(std::uint16_t));
        ibv.Format = DXGI_FORMAT_R16_UINT;
    }

    DrawCall call = state_call;
    call.vertex_view    = vbv;
    call.index_view     = ibv;
    call.index_count    = prim.index_count;

    TexImage tex0{};
    TevOrder ord0{};
    bool capture_texaddr_detail = false;
    const bool texaddr_capture_enabled = capture_texaddr_target() != 0u;
    if (capture_draws_ || texaddr_capture_enabled) {
        ord0 = state_.tev_order(0);
        tex0 = state_.tex_image(ord0.texmap);
        capture_texaddr_detail =
            capture_texaddr_log_enabled(tex0.guest_addr);
    }
    if (!capture_texaddr_detail && texaddr_capture_enabled) {
        for (unsigned m = 0; m < kMaxTextureMaps; ++m) {
            if (capture_texaddr_log_enabled(state_.tex_image(m).guest_addr)) {
                capture_texaddr_detail = true;
                break;
            }
        }
    }

    if (capture_composites_) {
        bool effect_quad = false;
        if (prim.vertex_count <= 8u) {
            for (unsigned map = 0; map < kMaxTextureMaps; ++map) {
                if ((current_texture_map_mask_ & (1u << map)) == 0u) continue;
                const TexImage image = state_.tex_image(map);
                effect_quad |=
                    (image.width == 640u && image.height == 456u) ||
                    (image.width == 320u && image.height == 228u) ||
                    (image.width == 160u && image.height == 114u) ||
                    (image.width == 80u && image.height == 57u) ||
                    (image.width == 160u && image.height == 160u);
            }
        }
        if (effect_quad) {
            std::fprintf(stderr,
                "[cap-composite] frame=%llu draw=%u pipeline=%p table=0x%llX "
                "blend=%06X alpha=%06X depth=%02X scissor=%ld,%ld,%ld,%ld "
                "viewport=%g,%g,%g,%g,%g,%g\n",
                static_cast<unsigned long long>(frame_index_ + 1u), draw_index,
                static_cast<void*>(call.pipeline),
                static_cast<unsigned long long>(call.texture_table.ptr),
                state_.bp(0x41u), state_.bp(bp::kAlphaCompare), state_.bp(0x40u),
                call.scissor.left, call.scissor.top, call.scissor.right, call.scissor.bottom,
                call.xf_viewport[0], call.xf_viewport[1], call.xf_viewport[2],
                call.xf_viewport[3], call.xf_viewport[4], call.xf_viewport[5]);
            for (const auto& [key, table] : frame_texture_tables_) {
                if (table.ptr != call.texture_table.ptr) continue;
                for (unsigned map = 0; map < kMaxTextureMaps; ++map) {
                    if ((key.map_mask & (1u << map)) == 0u) continue;
                    const TexImage image = state_.tex_image(map);
                    std::fprintf(stderr,
                        "[cap-composite-map] frame=%llu draw=%u map=%u "
                        "addr=%08X resource=0x%llX srv=%u wh=%ux%u fmt=%u\n",
                        static_cast<unsigned long long>(frame_index_ + 1u), draw_index,
                        map, image.guest_addr,
                        static_cast<unsigned long long>(key.resources[map]),
                        key.srv_indices[map], key.widths[map], key.heights[map],
                        static_cast<unsigned>(image.format));
                }
                break;
            }
        }
    }

    // Per-draw state line for the captured frame (GALAXY_GX_CAPTURE_FRAME),
    // or for a specific texture address when diagnosing unstable frame slices.
    if (capture_draws_ || capture_texaddr_detail) {
        const unsigned log_draw_index =
            capture_draws_ ? capture_draw_idx_++ : draw_index;
        // Inspect the table key used to copy the actual descriptors. Do not
        // call TextureCache::get here: that could change the binding under
        // investigation. This reverse lookup runs only in requested captures.
        if (capture_draws_) {
            std::fprintf(stderr,
                "[cap-binding] frame=%llu draw=%u table=0x%llX mask=0x%02X\n",
                static_cast<unsigned long long>(frame_index_ + 1u),
                log_draw_index,
                static_cast<unsigned long long>(current_texture_table_.ptr),
                static_cast<unsigned>(current_texture_map_mask_));
            for (const auto& [key, table] : frame_texture_tables_) {
                if (table.ptr != current_texture_table_.ptr) continue;
                for (unsigned map = 0; map < kMaxTextureMaps; ++map) {
                    if ((key.map_mask & (1u << map)) == 0u) continue;
                    const TexImage image = state_.tex_image(map);
                    std::fprintf(stderr,
                        "[cap-binding-map] frame=%llu draw=%u map=%u "
                        "addr=%08X resource=0x%llX srv=%u wh=%ux%u fmt=%u\n",
                        static_cast<unsigned long long>(frame_index_ + 1u),
                        log_draw_index, map, image.guest_addr,
                        static_cast<unsigned long long>(key.resources[map]),
                        key.srv_indices[map], key.widths[map], key.heights[map],
                        static_cast<unsigned>(image.format));
                }
                break;
            }
        }
        std::fprintf(stderr,
            "[capdraw] %u prim=%u n=%u cmode=0x%06X/0x%06X acmp=0x%06X "
            "zmode=0x%02X stages=%u psflags=0x%02X rs=0x%08X "
            "tex0=%08X/%ux%u/f%u ten=%d\n",
            log_draw_index,
            static_cast<unsigned>(primitive), prim.vertex_count,
            state_.bp(static_cast<std::uint8_t>(0x41)),
            state_.bp(static_cast<std::uint8_t>(0x42)),
            state_.bp(bp::kAlphaCompare),
            state_.bp(static_cast<std::uint8_t>(0x40)),
            static_cast<unsigned>(state_.gen_mode().num_tev_stages),
            static_cast<unsigned>(ps_key_.flags),
            render_state_key_.blend_bits,
            tex0.guest_addr, tex0.width, tex0.height,
            static_cast<unsigned>(tex0.format),
            ord0.tex_enable ? 1 : 0);
        {
            const auto xf_float = [this](std::uint16_t address) {
                return std::bit_cast<float>(state_.xf(address));
            };
            const ScissorRect scissor = state_.scissor();
            const EfbScissor efb_scissor = state_.efb_scissor();
            std::fprintf(stderr,
                "[capdraw]   proj=%c vp=%.1f,%.1f,%.1f,%.1f,%.0f,%.0f "
                "sc=%d,%d,%d,%d off=%d,%d pnmtx=%u\n",
                state_.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 6u)) == 0u ? 'P' : 'O',
                static_cast<double>(xf_float(xf::kViewportBase)),
                static_cast<double>(xf_float(static_cast<std::uint16_t>(xf::kViewportBase + 1u))),
                static_cast<double>(xf_float(static_cast<std::uint16_t>(xf::kViewportBase + 3u))),
                static_cast<double>(xf_float(static_cast<std::uint16_t>(xf::kViewportBase + 4u))),
                static_cast<double>(xf_float(static_cast<std::uint16_t>(xf::kViewportBase + 2u))),
                static_cast<double>(xf_float(static_cast<std::uint16_t>(xf::kViewportBase + 5u))),
                scissor.x0, scissor.y0, scissor.x1, scissor.y1,
                efb_scissor.offset.x, efb_scissor.offset.y,
                static_cast<unsigned>(state_.cp(cp::kMatrixIndexA) & 0x3Fu));
        }
        const auto regs = state_.tev_register_colors();
        std::fprintf(stderr,
            "[capdraw]   tev0 cenv=0x%06X aenv=0x%06X ord=0x%03X "
            "ksel0=0x%08X chan0=0x%08X mat0=0x%08X amb0=0x%08X "
            "prev=%08X r0=%08X r1=%08X r2=%08X k0=%08X\n",
            ps_key_.stages[0].color_env,
            ps_key_.stages[0].alpha_env,
            ps_key_.stages[0].order,
            ps_key_.stages[0].ksel,
            state_.xf(static_cast<std::uint16_t>(0x100Eu)),
            state_.xf(static_cast<std::uint16_t>(xf::kMaterialColorBase)),
            state_.xf(static_cast<std::uint16_t>(xf::kAmbientColorBase)),
            regs[0], regs[1], regs[2], regs[3],
            state_.tev_konst_colors()[0]);
        const bool capture_detail =
            capture_tev_log_enabled(log_draw_index) ||
            capture_texaddr_detail;
        const bool capture_textures =
            capture_detail || (capture_draws_ && capture_all_textures_);
        if (capture_detail) {
            const unsigned stages = state_.gen_mode().num_tev_stages;
            for (unsigned s = 0; s < stages; ++s) {
                std::fprintf(stderr,
                    "[captev] draw=%u stage=%u cenv=0x%06X "
                    "aenv=0x%06X order=0x%03X ksel=0x%08X\n",
                    draw_index, s,
                    ps_key_.stages[s].color_env,
                    ps_key_.stages[s].alpha_env,
                    ps_key_.stages[s].order,
                    ps_key_.stages[s].ksel);
            }
            std::fprintf(stderr,
                "[captev] channels=%08X,%08X,%08X,%08X "
                "material=%08X,%08X ambient=%08X,%08X\n",
                state_.xf(xf::kChannelCtrlBase + 0u),
                state_.xf(xf::kChannelCtrlBase + 1u),
                state_.xf(xf::kChannelCtrlBase + 2u),
                state_.xf(xf::kChannelCtrlBase + 3u),
                state_.xf(xf::kMaterialColorBase + 0u),
                state_.xf(xf::kMaterialColorBase + 1u),
                state_.xf(xf::kAmbientColorBase + 0u),
                state_.xf(xf::kAmbientColorBase + 1u));
            std::fprintf(stderr,
                "[capxf] draw=%u numtex=%u numchan=%u vcdlo=%08X "
                "mtxA=%08X mtxB=%08X dual=%08X\n",
                draw_index,
                static_cast<unsigned>(state_.gen_mode().num_texgens),
                static_cast<unsigned>(state_.gen_mode().num_color_chans),
                state_.cp(cp::kVcdLo),
                state_.cp(cp::kMatrixIndexA),
                state_.cp(cp::kMatrixIndexB),
                state_.xf(xf::kDualTexTrans));
            for (unsigned tg = 0; tg < state_.gen_mode().num_texgens &&
                                  tg < kMaxTexGens; ++tg) {
                std::fprintf(stderr,
                    "[capxf] draw=%u texgen%u=%08X post%u=%08X\n",
                    draw_index, tg,
                    state_.xf(static_cast<std::uint16_t>(
                        xf::kTexGenBase + tg)),
                    tg,
                    state_.xf(static_cast<std::uint16_t>(
                        xf::kPostTexGenBase + tg)));
                const unsigned shift = tg < 4u ? 6u + 6u * tg
                                                : 6u * (tg - 4u);
                const std::uint32_t indices = tg < 4u
                    ? state_.cp(cp::kMatrixIndexA)
                    : state_.cp(cp::kMatrixIndexB);
                const unsigned tex_row =
                    std::min<unsigned>((indices >> shift) & 0x3Fu, 61u);
                const XfPostTexGen post = state_.post_tex_gen(tg);
                const unsigned post_row = post.matrix_index & 0x3Fu;
                for (unsigned r = 0; r < 3; ++r) {
                    std::fprintf(stderr,
                        "[capmtx] draw=%u texgen%u texrow%u=(%g,%g,%g,%g) "
                        "postrow%u=(%g,%g,%g,%g)\n",
                        draw_index, tg, tex_row + r,
                        std::bit_cast<float>(state_.xf(
                            static_cast<std::uint16_t>(
                                (tex_row + r) * 4u + 0u))),
                        std::bit_cast<float>(state_.xf(
                            static_cast<std::uint16_t>(
                                (tex_row + r) * 4u + 1u))),
                        std::bit_cast<float>(state_.xf(
                            static_cast<std::uint16_t>(
                                (tex_row + r) * 4u + 2u))),
                        std::bit_cast<float>(state_.xf(
                            static_cast<std::uint16_t>(
                                (tex_row + r) * 4u + 3u))),
                        (post_row + r) & 0x3Fu,
                        std::bit_cast<float>(state_.xf(
                            static_cast<std::uint16_t>(
                                xf::kPostMatricesBase +
                                (((post_row + r) & 0x3Fu) * 4u) + 0u))),
                        std::bit_cast<float>(state_.xf(
                            static_cast<std::uint16_t>(
                                xf::kPostMatricesBase +
                                (((post_row + r) & 0x3Fu) * 4u) + 1u))),
                        std::bit_cast<float>(state_.xf(
                            static_cast<std::uint16_t>(
                                xf::kPostMatricesBase +
                                (((post_row + r) & 0x3Fu) * 4u) + 2u))),
                        std::bit_cast<float>(state_.xf(
                            static_cast<std::uint16_t>(
                                xf::kPostMatricesBase +
                                (((post_row + r) & 0x3Fu) * 4u) + 3u))));
                }
            }
            for (unsigned light = 0; light < 2; ++light) {
                const std::uint16_t base = static_cast<std::uint16_t>(
                    xf::kLightsBase + light * 16u);
                std::fprintf(stderr,
                    "[caplight] draw=%u light%u "
                    "%08X %08X %08X %08X  %08X %08X %08X %08X  "
                    "%08X %08X %08X %08X  %08X %08X %08X %08X\n",
                    draw_index, light,
                    state_.xf(base + 0u), state_.xf(base + 1u),
                    state_.xf(base + 2u), state_.xf(base + 3u),
                    state_.xf(base + 4u), state_.xf(base + 5u),
                    state_.xf(base + 6u), state_.xf(base + 7u),
                    state_.xf(base + 8u), state_.xf(base + 9u),
                    state_.xf(base + 10u), state_.xf(base + 11u),
                    state_.xf(base + 12u), state_.xf(base + 13u),
                    state_.xf(base + 14u), state_.xf(base + 15u));
            }
        }
        if (capture_textures) {
            for (unsigned m = 0; m < 8; ++m) {
                const TexImage image = state_.tex_image(m);
                const TexMode mode = state_.tex_mode(m);
                std::fprintf(stderr,
                    "[captex] draw=%u map=%u addr=%08X size=%ux%u fmt=%u "
                    "wrap=%u/%u min=%u mag=%u lod=%u..%u bias=%d\n",
                    draw_index, m, image.guest_addr,
                    image.width, image.height,
                    static_cast<unsigned>(image.format),
                    static_cast<unsigned>(mode.wrap_s),
                    static_cast<unsigned>(mode.wrap_t),
                    static_cast<unsigned>(mode.min_filter),
                    static_cast<unsigned>(mode.mag_filter),
                    static_cast<unsigned>(mode.min_lod_x16),
                    static_cast<unsigned>(mode.max_lod_x16),
                    static_cast<int>(mode.lod_bias_x32));
            }
        }
        if (capture_detail) {
            void* ring_cpu = nullptr;
            const D3D12_RANGE no_read{0, 0};
            if (SUCCEEDED(renderer_.vertex_ring().resource()->Map(
                    0, &no_read, &ring_cpu)) && ring_cpu != nullptr) {
                const auto* verts = reinterpret_cast<const GxVertexOut*>(
                    static_cast<const std::byte*>(ring_cpu) +
                    prim.vertex_byte_offset);
                const std::uint32_t vertex_limit =
                    std::min<std::uint32_t>(prim.vertex_count, 16u);
                const XfTexGen tg0 = state_.tex_gen(0);
                const XfPostTexGen post0 = state_.post_tex_gen(0);
                float capture_projection[4][4]{};
                fill_vs_projection(state_, capture_projection);
                const unsigned tex_row0 =
                    std::min<unsigned>(
                        (state_.cp(cp::kMatrixIndexA) >> 6u) & 0x3Fu, 61u);
                const unsigned post_row0 = post0.matrix_index & 0x3Fu;
                for (std::uint32_t v = 0; v < vertex_limit; ++v) {
                    float coord[4]{0.0f, 0.0f, 1.0f, 1.0f};
                    if (tg0.source_row == 0u) {
                        coord[0] = verts[v].position[0];
                        coord[1] = verts[v].position[1];
                        coord[2] = verts[v].position[2];
                    } else if (tg0.source_row == 1u) {
                        coord[0] = verts[v].normal[0];
                        coord[1] = verts[v].normal[1];
                        coord[2] = verts[v].normal[2];
                    } else if (tg0.source_row >= 5u &&
                               tg0.source_row <= 12u) {
                        const unsigned uv_index = tg0.source_row - 5u;
                        coord[0] = verts[v].uv[uv_index][0];
                        coord[1] = verts[v].uv[uv_index][1];
                    }
                    if (!tg0.input_form_abc1) {
                        coord[2] = 1.0f;
                    }
                    float res[3]{0.0f, 0.0f, 1.0f};
                    const unsigned tex_rows =
                        tg0.generate_stq ? 3u : 2u;
                    for (unsigned r = 0; r < tex_rows; ++r) {
                        for (unsigned c = 0; c < 4; ++c) {
                            res[r] += coord[c] *
                                std::bit_cast<float>(state_.xf(
                                    static_cast<std::uint16_t>(
                                        (tex_row0 + r) * 4u + c)));
                        }
                    }
                    if ((state_.xf(xf::kDualTexTrans) & 1u) != 0u) {
                        if (post0.normalize) {
                            const float len2 =
                                res[0] * res[0] +
                                res[1] * res[1] +
                                res[2] * res[2];
                            if (len2 > 0.0f) {
                                const float inv_len = 1.0f / std::sqrt(len2);
                                res[0] *= inv_len;
                                res[1] *= inv_len;
                                res[2] *= inv_len;
                            }
                        }
                        float post_res[3]{};
                        for (unsigned r = 0; r < 3; ++r) {
                            const unsigned row = (post_row0 + r) & 0x3Fu;
                            post_res[r] = std::bit_cast<float>(state_.xf(
                                static_cast<std::uint16_t>(
                                    xf::kPostMatricesBase +
                                    row * 4u + 3u)));
                            for (unsigned c = 0; c < 3; ++c) {
                                post_res[r] += res[c] *
                                    std::bit_cast<float>(state_.xf(
                                        static_cast<std::uint16_t>(
                                            xf::kPostMatricesBase +
                                            row * 4u + c)));
                            }
                        }
                        res[0] = post_res[0];
                        res[1] = post_res[1];
                        res[2] = post_res[2];
                    }
                    float sample_u = res[0];
                    float sample_v = res[1];
                    if (res[2] != 0.0f) {
                        sample_u /= res[2];
                        sample_v /= res[2];
                    }
                    const unsigned pos_row =
                        std::min<unsigned>(
                            verts[v].mtx_indices[0] & 0x3Fu, 61u);
                    const float in_pos[4]{
                        verts[v].position[0], verts[v].position[1],
                        verts[v].position[2], 1.0f};
                    float world_pos[4]{0.0f, 0.0f, 0.0f, 1.0f};
                    for (unsigned r = 0; r < 3; ++r) {
                        for (unsigned c = 0; c < 4; ++c) {
                            world_pos[r] +=
                                std::bit_cast<float>(state_.xf(
                                    static_cast<std::uint16_t>(
                                        (pos_row + r) * 4u + c))) *
                                in_pos[c];
                        }
                    }
                    float clip_pos[4]{};
                    for (unsigned r = 0; r < 4; ++r) {
                        for (unsigned c = 0; c < 4; ++c) {
                            clip_pos[r] += capture_projection[r][c] *
                                world_pos[c];
                        }
                    }
                    const float inv_clip_w =
                        clip_pos[3] != 0.0f ? 1.0f / clip_pos[3] : 0.0f;
                    std::fprintf(stderr,
                        "[capdraw]   v%u pos=(%g,%g,%g) uv0=(%g,%g) "
                        "tex0=(%g,%g,%g)->(%g,%g) ndc=(%g,%g,%g) "
                        "col0=%08X mi=0x%08X/%08X/%08X\n",
                        v, verts[v].position[0], verts[v].position[1],
                        verts[v].position[2], verts[v].uv[0][0],
                        verts[v].uv[0][1],
                        res[0], res[1], res[2], sample_u, sample_v,
                        clip_pos[0] * inv_clip_w,
                        clip_pos[1] * inv_clip_w,
                        clip_pos[2] * inv_clip_w,
                        verts[v].color0,
                        verts[v].mtx_indices[0],
                        verts[v].mtx_indices[1],
                        verts[v].mtx_indices[2]);
                }
                renderer_.vertex_ring().resource()->Unmap(0, &no_read);
            }
        }

        if (draw_index == capture_readback_draw_) {
            void* ring_cpu = nullptr;
            const D3D12_RANGE no_read{0, 0};
            if (SUCCEEDED(renderer_.vertex_ring().resource()->Map(
                    0, &no_read, &ring_cpu)) && ring_cpu != nullptr) {
                const auto* vertices = reinterpret_cast<const GxVertexOut*>(
                    static_cast<const std::byte*>(ring_cpu) +
                    prim.vertex_byte_offset);
                float projection[4][4]{};
                fill_vs_projection(state_, projection);
                const float proj_raw[6]{
                    std::bit_cast<float>(state_.xf(xf::kProjectionBase + 0u)),
                    std::bit_cast<float>(state_.xf(xf::kProjectionBase + 1u)),
                    std::bit_cast<float>(state_.xf(xf::kProjectionBase + 2u)),
                    std::bit_cast<float>(state_.xf(xf::kProjectionBase + 3u)),
                    std::bit_cast<float>(state_.xf(xf::kProjectionBase + 4u)),
                    std::bit_cast<float>(state_.xf(xf::kProjectionBase + 5u))};
                const ScissorRect target_scissor = state_.scissor();
                std::fprintf(stderr,
                    "[capxform] draw=%u proj=(%g,%g,%g,%g,%g,%g;t=%u) "
                    "vp=(%g,%g,%g,%g,%g,%g) scissor=(%d,%d)-(%d,%d)\n",
                    draw_index,
                    proj_raw[0], proj_raw[1], proj_raw[2],
                    proj_raw[3], proj_raw[4], proj_raw[5],
                    state_.xf(xf::kProjectionBase + 6u),
                    call.xf_viewport[0], call.xf_viewport[1],
                    call.xf_viewport[2], call.xf_viewport[3],
                    call.xf_viewport[4], call.xf_viewport[5],
                    target_scissor.x0, target_scissor.y0,
                    target_scissor.x1, target_scissor.y1);
                const std::uint32_t vertex_limit =
                    std::min<std::uint32_t>(prim.vertex_count, 16u);
                for (std::uint32_t v = 0; v < vertex_limit; ++v) {
                    const GxVertexOut& vertex = vertices[v];
                    const unsigned row = std::min(
                        vertex.mtx_indices[0] & 0x3Fu, 61u);
                    float matrix[3][4]{};
                    for (unsigned r = 0; r < 3; ++r) {
                        for (unsigned c = 0; c < 4; ++c) {
                            matrix[r][c] = std::bit_cast<float>(
                                state_.xf(static_cast<std::uint16_t>(
                                    (row + r) * 4u + c)));
                        }
                    }
                    const float in[4]{
                        vertex.position[0], vertex.position[1],
                        vertex.position[2], 1.0f};
                    float world[4]{0.0f, 0.0f, 0.0f, 1.0f};
                    for (unsigned r = 0; r < 3; ++r) {
                        for (unsigned c = 0; c < 4; ++c) {
                            world[r] += matrix[r][c] * in[c];
                        }
                    }
                    float clip[4]{};
                    for (unsigned r = 0; r < 4; ++r) {
                        for (unsigned c = 0; c < 4; ++c) {
                            clip[r] += projection[r][c] * world[c];
                        }
                    }
                    const float inv_w =
                        clip[3] != 0.0f ? 1.0f / clip[3] : 0.0f;
                    std::fprintf(stderr,
                        "[capxform] v%u row=%u in=(%g,%g,%g) "
                        "world=(%g,%g,%g) clip=(%g,%g,%g,%g) "
                        "ndc=(%g,%g,%g)\n",
                        v, row, in[0], in[1], in[2],
                        world[0], world[1], world[2],
                        clip[0], clip[1], clip[2], clip[3],
                        clip[0] * inv_w, clip[1] * inv_w,
                        clip[2] * inv_w);
                }
                renderer_.vertex_ring().resource()->Unmap(0, &no_read);
            }
        }
    }

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
            prim.vertex_count * static_cast<UINT>(sizeof(GxVertexOut));
        if (prim.indexed) {
            pending_draw_batch_.call.index_view.SizeInBytes +=
                prim.index_count * static_cast<UINT>(sizeof(std::uint16_t));
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
    if (capture_draws_ && draw_index == capture_readback_draw_) {
        flush_pending_draw_batch();
        capture_readback_recorded_ = renderer_.debug_copy_efb_to_readback();
        std::cerr << "[efb-dump] after-draw=" << draw_index
                  << " capture-recorded=" << capture_readback_recorded_ << '\n';
    }
}

void GxBackend::on_efb_copy(std::uint32_t exec_command) {
    flush_pending_draw_batch();
    const EfbCopyParams params = state_.efb_copy(exec_command);
    if (frame_dependency_event_sink_ != nullptr && params.copy_to_xfb) {
        frame_dependency_event_sink_->xfb_copy(
            params.dest_addr, exec_command);
    }
    if (params.copy_to_xfb) {
        galaxy::timeline::record(galaxy::timeline::Event::XfbCopy, params.dest_addr);
    }
    const std::uint32_t effective_exec_command =
        state_.bp(bp::kCopyExecute);
    const bool raw_copy_to_xfb = (exec_command & (1u << 14u)) != 0u;
    const bool effective_copy_to_xfb =
        (effective_exec_command & (1u << 14u)) != 0u;
    if (trace_xfb_causal_frame(frame_index_ + 1u)) {
        std::cerr << "[xfb-causal-copy] frame=" << (frame_index_ + 1u)
                  << " exec=0x" << std::hex << exec_command
                  << " effective-exec=0x" << effective_exec_command
                  << " dest=0x" << params.dest_addr << std::dec
                  << " raw-copy-to-xfb-bit="
                  << (raw_copy_to_xfb ? 1 : 0)
                  << " effective-copy-to-xfb-bit="
                  << (effective_copy_to_xfb ? 1 : 0)
                  << " decoded-copy-to-xfb="
                  << (params.copy_to_xfb ? 1 : 0)
                  << " raw-classification-match="
                  << (raw_copy_to_xfb == params.copy_to_xfb ? 1 : 0)
                  << " effective-classification-match="
                  << (effective_copy_to_xfb == params.copy_to_xfb ? 1 : 0)
                  << '\n';
    }
    const bool time_copy = frame_time_detail_;
    const auto copy_start = time_copy
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    if (trace_efb_copy_enabled() || capture_draws_ || capture_composites_) {
        std::cerr << std::dec
                  << "[efb-copy] frame=" << frame_index_
                  << " kind=" << (params.copy_to_xfb ? "xfb" : "tex")
                  << " exec=0x" << std::hex << exec_command
                  << " dst=0x" << params.dest_addr
                  << std::dec
                  << " stride=" << params.dest_stride
                  << " src=(" << params.src_x << ',' << params.src_y
                  << ' ' << params.src_width << 'x' << params.src_height
                  << ") yscale=" << params.y_scale_raw
                  << " scaley=" << (params.scale_y ? 1 : 0)
                  << " fmt=" << static_cast<unsigned>(params.target_format)
                  << " clear=" << (params.clear ? 1 : 0)
                  << " f2f=" << static_cast<unsigned>(params.frame_to_field)
                  << " yuv=" << (params.yuv ? 1 : 0)
                  << " color-mask=" << state_.blend_mode().color_update
                  << " alpha-mask=" << state_.blend_mode().alpha_update
                  << " depth-mask=" << state_.z_mode().update_enable
                  << " pixel-format=" << static_cast<unsigned>(state_.pe_control().pixel_format)
                  << " phase=" << static_cast<unsigned>(rt_phase_) << " scene=" << (scene_phase_ ? 1 : 0)
                  << '\n';
    }

    if (params.copy_to_xfb) {
        static const bool capture_before_xfb = [] {
            char value[8]{};
            std::size_t length = 0;
            return getenv_s(&length, value, sizeof(value),
                "GALAXY_GX_CAPTURE_EFB_BEFORE_XFB") == 0 && length > 1u && value[0] == '1';
        }();
        if (capture_before_xfb && capture_pixel_frame_requested(frame_index_ + 1u) &&
            !capture_readback_recorded_) {
            capture_readback_recorded_ = renderer_.debug_copy_efb_to_readback();
            std::cerr << "[efb-pre-xfb-capture] frame=" << (frame_index_ + 1u)
                      << " capture-recorded=" << capture_readback_recorded_
                      << " dest=0x" << std::hex << params.dest_addr << std::dec
                      << " src=" << params.src_x << ',' << params.src_y << ' '
                      << params.src_width << 'x' << params.src_height
                      << " stride=" << params.dest_stride << " clear=" << params.clear << '\n';
        }
        XfbTexture* xfb = efb_copies_.acquire_xfb(params, frame_index_);
        if (xfb != nullptr) {
            const bool capture_owned = capture_owned_xfb_requested(frame_index_ + 1u);
            bool owned_efb_recorded = false;
            if (capture_owned) {
                renderer_.set_debug_capture_paths(
                    capture_output_path("GALAXY_GX_CAPTURE_PPM", frame_index_ + 1u),
                    capture_output_path("GALAXY_GX_CAPTURE_BACKBUFFER_PPM", frame_index_ + 1u));
                owned_efb_recorded = renderer_.debug_copy_efb_to_readback();
            }
            end_side_layer();
            mirror_opened_scene_ = false;
            renderer_.copy_efb_to_xfb(params, *xfb);
            // The display copy ends the 2D layer: later work and the next
            // frame use the render-resolution EFB again.
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
            if (capture_owned) {
                const bool owned_xfb_recorded =
                    renderer_.debug_copy_selected_xfb_to_readback(*xfb);
                std::cerr << "[owned-xfb-capture] source-chunk=" << (frame_index_ + 1u)
                    << " efb-recorded=" << owned_efb_recorded
                    << " xfb-recorded=" << owned_xfb_recorded
                    << " matched-copy-recorded=" << (owned_efb_recorded && owned_xfb_recorded)
                    << " serial=" << xfb->copy_serial << " stamp=" << xfb->frame_stamp
                    << " addr=0x" << std::hex << params.dest_addr << std::dec
                    << " src=" << params.src_x << ',' << params.src_y << ' '
                    << params.src_width << 'x' << params.src_height
                    << " dest=" << xfb->width << 'x' << xfb->height
                    << " scope=optional-copy-capture-not-performance\n";
            }
            ++stat_xfb_copies_;
            ++frame_xfb_copies_;
            record_monotonic_cadence_sample(xfb_copy_production_cadence_);
        }
    } else {
        static const bool capture_before_texture =
            read_env_u64("GALAXY_GX_CAPTURE_EFB_BEFORE_TEXTURE_COPY", 0u) == 1u;
        static const std::uint64_t capture_texture_format =
            read_env_u64("GALAXY_GX_CAPTURE_EFB_BEFORE_TEXTURE_FORMAT", UINT64_MAX);
        static const std::uint64_t capture_texture_source_x =
            read_env_u64("GALAXY_GX_CAPTURE_EFB_BEFORE_TEXTURE_SOURCE_X", UINT64_MAX);
        static const std::uint64_t capture_texture_source_y =
            read_env_u64("GALAXY_GX_CAPTURE_EFB_BEFORE_TEXTURE_SOURCE_Y", UINT64_MAX);
        if (capture_before_texture && capture_pixel_frame_requested(frame_index_ + 1u) &&
            (capture_texture_format == UINT64_MAX ||
             capture_texture_format == static_cast<std::uint64_t>(params.target_format)) &&
            (capture_texture_source_x == UINT64_MAX || capture_texture_source_x == params.src_x) &&
            (capture_texture_source_y == UINT64_MAX || capture_texture_source_y == params.src_y) &&
            !capture_readback_recorded_) {
            capture_readback_recorded_ = renderer_.debug_copy_efb_to_readback();
            std::cerr << "[efb-pre-texture-capture] frame=" << (frame_index_ + 1u)
                      << " capture-recorded=" << capture_readback_recorded_
                      << " dest=0x" << std::hex << params.dest_addr << std::dec
                      << " src=" << params.src_x << ',' << params.src_y << ' '
                      << params.src_width << 'x' << params.src_height
                      << " stride=" << params.dest_stride
                      << " format=" << static_cast<unsigned>(params.target_format)
                      << " clear=" << params.clear << '\n';
        }
        ++stat_tex_copies_;
        ++frame_tex_copies_;
        // Depth-source copy when the PE pixel format is Z24 at copy time
        // (Dolphin BPStructs: is_depth_copy = zcontrol.pixel_format == Z24).
        const bool is_depth_copy = state_.pe_control().pixel_format == 3u;
        const bool alias_updated =
            renderer_.copy_efb_to_texture(
                params,
                texture_cache_,
                is_depth_copy);
        if (alias_updated) {
            ++frame_efb_alias_updates_;
            // The same guest address may alternate between format-keyed GPU
            // resources. BP texture registers need not be rewritten after a
            // copy, so force the next draw to copy the latest alias SRV into
            // any shader-visible frame table that actually referenced this
            // alias. Reusing the same D3D12 texture updates its contents in
            // place; an existing SRV table remains valid.
            mark_texture_binding_alias_dirty(params);
            texture_bindings_dirty_ = true;
        } else {
            ++frame_efb_alias_noops_;
        }
    }

    // GX clear-on-copy: the copy command's clear bit wipes the copy's source rect with the latched
    // copy-clear registers, honoring the PE write masks (Dolphin BPStructs BPMEM_TRIGGER_EFB_COPY:
    // ClearScreen(srcRect) with colorEnable = blendmode.colorupdate, alphaEnable =
    // blendmode.alphaupdate, zEnable = zmode.updateenable). A full-target clear here wiped EFB content
    // that later copies in the same frame still needed.
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
    if (frame_dependency_event_sink_ != nullptr) {
        frame_dependency_event_sink_->pe_finish();
    }
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
    // The required callback stays authoritative. The optional trace is capped so it can observe the
    // signal without flooding stderr, since these callbacks fire on every GXDrawDone.
    static int s_pe_done_logs = 0;
    if (frame_services_ != nullptr && frame_services_->log != nullptr &&
        s_pe_done_logs < 4) {
        ++s_pe_done_logs;
        frame_services_->log(
            frame_services_->user,
            galaxy::LogLevelV1::Trace,
            "[GxBackend] PE_DONE (GXDrawDone)");
    }
}

void GxBackend::on_pe_token(std::uint16_t token, bool interrupt) {
    flush_pending_draw_batch();
    // LensFlareDirector's draw sync follows the sun's alpha marks; its callback reads them with
    // GXPeekARGB. The colour field it reads is the EFB at this token, not at a chunk's end
    // (after the frame's copy-clear, or with an overlay's timing).
    if (std::exchange(alpha_marks_drawn_, false) && initialized_ && async_render_thread_enabled()) {
        AsyncPeekFieldState& state = async_peek_[async_peek_kind_index(EfbPeekKind::Color)];
        if (state.requested.load(std::memory_order_acquire) &&
            !renderer_.async_peek_field_pending(EfbPeekKind::Color)) {
            const int published = state.published.load(std::memory_order_relaxed);
            const int slot = published < 0 ? 0 : (published + 1) % 3;
            if (renderer_.record_async_peek_field_in_frame(EfbPeekKind::Color, static_cast<std::size_t>(slot))) {
                if (state.rgba[0] == nullptr) {
                    for (std::size_t i = 0; i < state.rgba.size(); ++i) {
                        state.rgba[i] = renderer_.async_peek_field_rgba(EfbPeekKind::Color, i);
                    }
                }
                state.requested.store(false, std::memory_order_relaxed);
                state.pending_slot = slot;
                color_field_at_tokens_ = true;
            }
        }
    }
    if (frame_dependency_event_sink_ != nullptr) {
        frame_dependency_event_sink_->pe_token(token, interrupt);
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
            galaxy::LogLevelV1::Trace,
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
    // BP 0x64 (source) is latched by GxState; BP 0x65 (dest) triggers this. Without this transfer,
    // every palettized (C4/C8/C14X2) texture decodes against a zeroed TLUT bank, which shows as black
    // or garbage UI and fonts.
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
    // GXInvalidateVtxCache invalidates the hardware vertex cache, not guest
    // array memory. Decoded host vertices use dirty-write invalidation plus an
    // exact dependency-byte check before every hit; clearing the whole cache
    // here would discard otherwise-valid display-list reuse.
}

}  // namespace galaxy::gx
