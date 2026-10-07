// gx_backend.cpp — GxBackend: top-level orchestrator for the live GX pipeline.
//
// The frozen galaxy::gx public API in gx_d3d12.h is implemented by gx_shim.cpp
// as a thin wrapper around this backend. The old monolithic/legacy D3D12
// sources have been removed from the build and source tree so audits land on
// the active render path.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#pragma warning(push, 0)
#include <Windows.h>
#include <d3d12.h>
#pragma warning(pop)

#include "galaxy/gx/gx_backend.h"
#include "galaxy/gx/dependency_event_capture.h"
#include "galaxy/gx/owned_fifo_event_replay.h"

#include "galaxy/frame_cadence_diagnostics.h"
#include "galaxy/gx/line_point_raster.h"
#include "galaxy/gx/texture_sampling.h"
#include "galaxy/gx/uber_constants.h"
#include "galaxy/gx/render_config.h"
#include "galaxy/gx/sorted_dirty_ranges.h"

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

// ---------------------------------------------------------------------------
// Helpers: build GxVsConstants from current GxState + render ring.
// ---------------------------------------------------------------------------

namespace {

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

// Render-worker-owned extension of the existing capture. Disabled runs never
// touch a file. Each decimal request selects one substantial FIFO chunk, not a
// whole game frame. Empty VI/presentation submissions and the 32-byte control
// chunks observed in the Gateway route must not consume it.
// Poll files at most 10 times/second; a pending request expires after 10 seconds.
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

// One source/destination pair from the same real XFB copy, within the existing
// eight-chunk request bound. The old substantial-chunk snapshot can belong to
// a newer EFB than the selected display XFB; never call those a matched pair.
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
    // The trace is deliberately bounded even when the caller forgets to set
    // a limit. It is a causal diagnostic, not a shipping telemetry stream.
    static const std::uint64_t max_frames = std::min<std::uint64_t>(
        read_env_u64("GALAXY_TRACE_XFB_CAUSAL_MAX_FRAMES", 600u),
        10'000u);
    return frame >= start_frame && frame - start_frame < max_frames;
}

// Unlike the older streaming causal trace above, this mode collects a small,
// fixed record set on the render thread and formats it only after that thread
// has stopped. It remains usable while AI DMA is active: redirected stderr
// writes from a render callback are not real-time-safe evidence.
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
    // When the VI-selected XFB is transiently non-presentable (it has just
    // been re-acquired as the next copy target during a double/triple-buffer
    // flip), select_xfb_for_present holds the last good frame instead of
    // jumping to a different buffer. The "missing_expired" fall-forward to
    // latest() must only fire for a *sustained* miss (a buffer that genuinely
    // never receives a copy), never for the 1-3 frame jitter of a normal flip
    // or the async render pipeline — otherwise every flip strobes a different
    // buffer for one frame, which is the visible black/flash regression.
    // 30 presents (~0.5s) comfortably clears pipeline jitter while still
    // breaking a true multi-frame freeze.
    static const std::uint64_t age =
        std::clamp<std::uint64_t>(
            read_env_u64("GALAXY_XFB_MAX_PRESERVE_AGE", 30u),
            0u,
            600u);
    return age;
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

// The decoded-packet-run vertex cache reuses CPU-decoded vertices across
// identical display-list replays. It is a CPU-side micro-optimization only:
// on capable hosts re-decoding every frame is cheap, and the cache is the
// newest/most-complex transform path, so it is the most likely source of any
// stale-or-duplicated geometry regression. Default OFF for correctness; opt in
// with GALAXY_GX_DECODED_VERTEX_CACHE=1 on CPU-bound hosts that need it.
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
            return false;
        }
        return value[0] != '0' && value[0] != 'n' && value[0] != 'N';
    }();
    return enabled;
}

bool immutable_vertex_upload_reuse_enabled() {
    static const bool enabled = [] {
        char value[16]{};
        std::size_t length = 0;
        return getenv_s(&length, value, sizeof(value),
                        "GALAXY_GX_IMMUTABLE_VERTEX_UPLOAD_REUSE") == 0 &&
            length > 1u && value[0] == '1';
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
            return false;
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
    ranges.erase(
        std::remove_if(
            ranges.begin(),
            ranges.end(),
            [](const GuestMemoryRange& range) {
                return range.size == 0u;
            }),
        ranges.end());
    std::sort(
        ranges.begin(),
        ranges.end(),
        [](const GuestMemoryRange& a, const GuestMemoryRange& b) {
            return a.guest_base < b.guest_base ||
                (a.guest_base == b.guest_base && a.size < b.size);
        });
    std::vector<GuestMemoryRange> merged;
    merged.reserve(ranges.size());
    for (const GuestMemoryRange& range : ranges) {
        const std::uint64_t begin = range.guest_base;
        const std::uint64_t end = begin + range.size;
        if (merged.empty()) {
            merged.push_back(range);
            continue;
        }
        GuestMemoryRange& tail = merged.back();
        const std::uint64_t tail_begin = tail.guest_base;
        const std::uint64_t tail_end = tail_begin + tail.size;
        if (begin <= tail_end) {
            const std::uint64_t merged_end = std::max(tail_end, end);
            tail.size = static_cast<std::uint32_t>(merged_end - tail_begin);
        } else {
            merged.push_back(range);
        }
    }
    ranges.swap(merged);
}

std::uint64_t guest_memory_range_bytes(
    const std::vector<GuestMemoryRange>& ranges) noexcept {
    std::uint64_t bytes = 0;
    for (const GuestMemoryRange& range : ranges) {
        bytes += range.size;
    }
    return bytes;
}

std::uint64_t fnv1a_mix_u8(std::uint64_t hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= 1099511628211ull;
    return hash;
}

std::uint64_t fnv1a_mix_u32(
    std::uint64_t hash,
    std::uint32_t value) noexcept {
    for (unsigned shift = 0; shift < 32u; shift += 8u) {
        hash = fnv1a_mix_u8(
            hash,
            static_cast<std::uint8_t>((value >> shift) & 0xFFu));
    }
    return hash;
}

std::uint64_t hash_bytes(std::span<const std::byte> bytes) noexcept {
    std::uint64_t hash = 1469598103934665603ull;
    for (const std::byte byte : bytes) {
        hash = fnv1a_mix_u8(hash, std::to_integer<std::uint8_t>(byte));
    }
    return hash;
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
          stat_draw_run_cache_evictions_(stat_draw_run_cache_evictions) {
        emitted_range_keys_.reserve(4096u);
        emitted_broad_draw_dependency_keys_.reserve(256u);
    }

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
        if (record_broad_cached_draw_dependencies_if_over_budget(
                vtxfmt,
                total_vertices)) {
            return true;
        }
        const GxBackend::DependencyDrawRunRangeCacheKey cache_key{
            cache_token,
            packet_run_index,
            primitive,
            vtxfmt,
            dependency_shape_state_hash(state_),
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
        if (!emitted_range_keys_.insert(key).second) {
            return;
        }
        record_guest_memory_range(ranges_, guest_base, size);
    }

    [[nodiscard]] static std::size_t draw_run_cache_entry_bytes(
        const std::vector<GuestMemoryRange>& ranges) noexcept {
        return ranges.size() * sizeof(GuestMemoryRange);
    }

    void prune_draw_run_cache() {
        constexpr std::size_t kMaxDrawRunCacheBytes =
            8u * 1024u * 1024u;
        if (draw_run_cache_bytes_ <= kMaxDrawRunCacheBytes ||
            draw_run_cache_.empty()) {
            return;
        }

        struct PruneCandidate {
            std::uint64_t last_used = 0;
            GxBackend::DependencyDrawRunRangeCacheKey key{};
        };
        std::vector<PruneCandidate> candidates;
        candidates.reserve(draw_run_cache_.size());
        for (const auto& [key, entry] : draw_run_cache_) {
            candidates.push_back(PruneCandidate{entry.last_used, key});
        }
        std::sort(
            candidates.begin(),
            candidates.end(),
            [](const PruneCandidate& lhs, const PruneCandidate& rhs) noexcept {
                return lhs.last_used < rhs.last_used;
            });

        for (const PruneCandidate& candidate : candidates) {
            if (draw_run_cache_bytes_ <= kMaxDrawRunCacheBytes) {
                break;
            }
            const auto it = draw_run_cache_.find(candidate.key);
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
        const std::uint64_t key = fnv1a_mix_u32(
            dependency_shape_state_hash(state_),
            vtxfmt);
        if (emitted_broad_draw_dependency_keys_.find(key) !=
            emitted_broad_draw_dependency_keys_.end()) {
            return true;
        }
        const VertexDescriptor desc = state_.vertex_desc(vtxfmt);
        if (!record_broad_draw_dependencies(desc)) {
            return false;
        }
        record_texture_dependency_ranges_once();
        emitted_broad_draw_dependency_keys_.insert(key);
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
                    ? snapshot_normal_vector_size(desc.normal)
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
                    for (std::uint32_t index : indices) {
                        record_indexed_array_access(
                            array_ranges,
                            1u,
                            state_.array_base(1u) +
                                index * state_.array_stride(1u),
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
    std::unordered_set<std::uint64_t> emitted_range_keys_;
    std::unordered_set<std::uint64_t> emitted_broad_draw_dependency_keys_;
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
    std::uint32_t size) {
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
    const std::uint64_t end =
        static_cast<std::uint64_t>(guest_addr) + size;
    for (std::uint32_t i = 0; i < memory->region_count; ++i) {
        const GuestMemoryRegionV1& r = memory->regions[i];
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

// Converts the raw XF projection registers to a column-major 4×4 matrix
// stored row-major (HLSL row_major float4x4 == stored row-by-row).
// GX perspective (type=0): maps to a left-hand clip space with z in [0,1].
// GX orthographic (type=1): same re-mapping.
void fill_projection(const GxState& state, float out[4][4]) {
    std::memset(out, 0, sizeof(float) * 16);

    const float A = std::bit_cast<float>(state.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 0u)));
    const float B = std::bit_cast<float>(state.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 1u)));
    const float C = std::bit_cast<float>(state.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 2u)));
    const float D = std::bit_cast<float>(state.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 3u)));
    const float E = std::bit_cast<float>(state.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 4u)));
    const float F = std::bit_cast<float>(state.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 5u)));
    const std::uint32_t type = state.xf(static_cast<std::uint16_t>(xf::kProjectionBase + 6u));

    // GX NDC z range is [-1 (near), 0 (far)].  We map it to D3D [0, 1]
    // exactly the way Dolphin does (VertexShaderGen.cpp): NEGATE the GX
    // clip z → ndc_d3d = -ndc_gx ∈ [0 (far), 1 (near)] — a REVERSED-Z
    // buffer.  Two reasons this is the only robust scheme:
    //   1. The GX far plane lands at ndc 0, AWAY from the clip boundary.
    //      (The boot-134 z_gx+1 mapping put far at exactly ndc 1.0, where
    //      float error clipped SMG's far-plane depth-clear quad and the
    //      skybox — [vtx-clip] showed ndc 1.00033..1.00078.)
    //   2. It composes with the XF viewport zRange/farZ depth range and
    //      the FLIPPED depth compare funcs (see pipeline_cache) to give
    //      hardware-exact pass/fail behavior.
    // Dolphin's (1 - 1e-7) epsilon keeps exactly-on-far geometry inside
    // the clip volume (their "Sonic" hack; we keep DepthClipEnable=TRUE).
    constexpr float kFarEps = 1.0f - 1e-7f;
    if (type == 0) {
        // Perspective.  GX: clip z = z*E + F, clip w = -z.
        // z_d3d_clip = -(z*E + F) * (1-1e-7).
        out[0][0] = A;
        out[0][2] = B;    // column 2 of row 0 (pre-divide shear)
        out[1][1] = C;
        out[1][2] = D;
        out[2][2] = -E * kFarEps;
        out[2][3] = -F * kFarEps;
        out[3][2] = -1.0f;
    } else {
        // Orthographic.  GX: clip z = z*E + F, clip w = 1.  Same negation.
        out[0][0] = A;
        out[0][3] = B;
        out[1][1] = C;
        out[1][3] = D;
        out[2][2] = -E * kFarEps;
        out[2][3] = -F * kFarEps;
        out[3][3] = 1.0f;
    }
}

// Fill GxVsConstants for the current GxState.
// `palette_snapshot_index` is the index of this frame's XF matrix upload
// within the matrix ring (written separately by GxBackend).
void fill_vs_constants(
    const GxState& state,
    std::uint32_t palette_snapshot_index,
    unsigned efb_scale,
    GxVsConstants& out) {
    std::memset(&out, 0, sizeof(out));

    fill_projection(state, out.projection);

    const GenMode gm = state.gen_mode();
    out.num_texgens = gm.num_texgens;
    out.num_chans   = gm.num_color_chans;

    // Per-vertex position matrix index flag: if VCD_LO bit 0 is set.
    const std::uint32_t vcd_lo = state.cp(cp::kVcdLo);
    const bool per_vertex_posmtx = (vcd_lo & 1u) != 0;

    // Lighting: any channel has lighting_enable.
    bool lighting = false;
    for (unsigned i = 0; i < gm.num_color_chans; ++i) {
        if (state.channel_ctrl(i).lighting_enable ||
            state.channel_ctrl(2u + i).lighting_enable) {
            lighting = true;
            break;
        }
    }
    out.flags  = (per_vertex_posmtx ? 1u : 0u)
               | (lighting           ? 2u : 0u);

    // Texgen config words (pairs of raw XF registers per gen).
    for (unsigned i = 0; i < kMaxTexGens; ++i) {
        out.texgen_config[i][0] = state.xf(
            static_cast<std::uint16_t>(xf::kTexGenBase + i));
        out.texgen_config[i][1] = state.xf(
            static_cast<std::uint16_t>(xf::kPostTexGenBase + i));
    }

    // Ambient and material colors (RGBA8 packed → 0..1 float[4]).
    for (unsigned i = 0; i < kMaxColorChannels; ++i) {
        // XF 0x100A / 0x100B = ambient color0 / color1 (RGBA8 packed).
        const std::uint32_t amb_raw = state.xf(
            static_cast<std::uint16_t>(xf::kAmbientColorBase + i));
        out.chan_ambient[i][0] = ((amb_raw >> 24) & 0xFFu) / 255.0f;
        out.chan_ambient[i][1] = ((amb_raw >> 16) & 0xFFu) / 255.0f;
        out.chan_ambient[i][2] = ((amb_raw >>  8) & 0xFFu) / 255.0f;
        out.chan_ambient[i][3] = ((amb_raw      ) & 0xFFu) / 255.0f;

        const std::uint32_t mat_raw = state.xf(
            static_cast<std::uint16_t>(xf::kMaterialColorBase + i));
        out.chan_material[i][0] = ((mat_raw >> 24) & 0xFFu) / 255.0f;
        out.chan_material[i][1] = ((mat_raw >> 16) & 0xFFu) / 255.0f;
        out.chan_material[i][2] = ((mat_raw >>  8) & 0xFFu) / 255.0f;
        out.chan_material[i][3] = ((mat_raw      ) & 0xFFu) / 255.0f;
    }

    out.mtx_palette_base = palette_snapshot_index;

    // Default matrix indices (CP MatrixIndexA/B) — resolved per texgen in the
    // VS when the vertex stream carries no per-vertex index (Dolphin resolves
    // the same registers into I_TEXMATRICES on the CPU instead).
    out.matrix_index_a = state.cp(cp::kMatrixIndexA);
    out.matrix_index_b = state.cp(cp::kMatrixIndexB);

    const auto copy_matrix = [&state](
                                 unsigned row,
                                 float destination[3][4]) {
        row = std::min(row, 61u);
        for (unsigned r = 0; r < 3; ++r) {
            for (unsigned c = 0; c < 4; ++c) {
                destination[r][c] = std::bit_cast<float>(
                    state.xf(static_cast<std::uint16_t>(
                        (row + r) * 4u + c)));
            }
        }
    };

    copy_matrix(out.matrix_index_a & 0x3Fu, out.inline_pos_matrix);
    for (unsigned i = 0; i < kMaxTexGens; ++i) {
        const unsigned shift = i < 4u ? 6u + 6u * i : 6u * (i - 4u);
        const std::uint32_t indices =
            i < 4u ? out.matrix_index_a : out.matrix_index_b;
        copy_matrix(
            (indices >> shift) & 0x3Fu,
            out.inline_tex_matrices[i]);

        const XfPostTexGen post = state.post_tex_gen(i);
        const unsigned post_row = post.matrix_index & 0x3Fu;
        for (unsigned r = 0; r < 3; ++r) {
            for (unsigned c = 0; c < 4; ++c) {
                out.inline_post_matrices[i][r][c] =
                    std::bit_cast<float>(state.xf(
                        static_cast<std::uint16_t>(
                            xf::kPostMatricesBase +
                            (((post_row + r) & 0x3Fu) * 4u) + c)));
            }
        }
    }

    std::array<std::uint32_t, kMaxTexGens> texcoord_s_registers{};
    for (unsigned i = 0; i < kMaxTexGens; ++i) {
        texcoord_s_registers[i] = state.bp(static_cast<std::uint8_t>(
            bp::kTexCoordSizeBase + i * 2u));
    }
    const float viewport_half_width = std::bit_cast<float>(
        state.xf(xf::kViewportBase));
    const float viewport_half_height = std::bit_cast<float>(
        state.xf(static_cast<std::uint16_t>(xf::kViewportBase + 1u)));
    const LinePointRasterParams line_point = make_line_point_raster_params(
        state.bp(bp::kSuLpSize),
        texcoord_s_registers,
        viewport_half_width,
        viewport_half_height,
        efb_scale,
        kEfbWidth,
        kEfbHeight);
    out.line_point_raster[0] = line_point.viewport_width_pixels;
    out.line_point_raster[1] = line_point.viewport_height_pixels;
    out.line_point_raster[2] = line_point.line_width_pixels;
    out.line_point_raster[3] = line_point.point_size_pixels;
    out.line_point_tex_offsets[0] = line_point.line_texcoord_mask;
    out.line_point_tex_offsets[1] = line_point.point_texcoord_mask;
    out.line_point_tex_offsets[2] = line_point.line_texcoord_divisor;
    out.line_point_tex_offsets[3] = line_point.point_texcoord_divisor;
}

// ---------------------------------------------------------------------------
// Build GxPsConstants from current GxState.
// ---------------------------------------------------------------------------
void fill_ps_constants(const GxState& state, GxPsConstants& out) {
    std::memset(&out, 0, sizeof(out));

    const GenMode gm = state.gen_mode();

    // TEV stage_config words.  TevStagePacked is 4 × u32, laid out identically
    // to uber_constants GxPsConstants::stage_config[stage][0..3].
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

    // Konst K0-K3 — same packing convention from tev_konst_colors().
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

    // Fog.  A (BP 0xEE) and C (BP 0xF1 bits 0-19) are 20-bit packed floats —
    // unpacked here on the CPU (Dolphin PixelShaderManager SetFogParamChanged
    // stores GetA()/GetC() the same way).  B magnitude (u24) and B shift (u5)
    // stay integral; both are exactly representable in float, and the PS
    // recovers them with uint() casts.
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
        // Projection flag (1.0 = orthographic — FogProjection enum).
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

}  // anonymous namespace

void FramePeEventClassifier::reset() {
    if (active_events_ != nullptr) {
        throw std::logic_error(
            "GX PE event classifier reset during an active parse");
    }
    state_ = GxState{};
    parser_ = FifoParser{};
    parser_.set_dump_dir("generated");
    pending_fifo_.clear();
}

void FramePeEventClassifier::classify(
    const std::byte* fifo_data,
    std::size_t fifo_size,
    GuestMemoryV1* memory,
    std::vector<FramePeEventSignature>& events) {
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
                parser_.run_available(fifo, memory, *this, state_);
            if (consumed < fifo.size()) {
                pending_fifo_.assign(
                    fifo.begin() + static_cast<std::ptrdiff_t>(consumed),
                    fifo.end());
            }
        } else {
            pending_fifo_.insert(
                pending_fifo_.end(), fifo_data, fifo_data + fifo_size);
            const std::size_t consumed = parser_.run_available(
                pending_fifo_, memory, *this, state_);
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
}

std::size_t FramePeEventClassifier::invalidate_display_list_cache_range(
    std::uint32_t guest_addr,
    std::uint32_t size) {
    return parser_.invalidate_display_list_cache_range(guest_addr, size);
}

std::size_t FramePeEventClassifier::draw_payload_size(
    std::uint8_t vtxfmt,
    std::uint16_t vertex_count) const {
    return VertexLoader::source_vertex_size(state_.vertex_desc(vtxfmt)) *
        static_cast<std::size_t>(vertex_count);
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
    vertex_layout_cache_ = {};

    frame_memory_ = nullptr;
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
    current_vs_constants_ = 0u;
    current_ps_constants_ = 0u;
    current_texture_table_ = {};
    current_sampler_table_ = {};
    current_matrix_palette_ = 0u;
    matrix_palette_stale_ = true;
    inline_matrices_stale_ = true;
    frame_bindings_dirty_ = true;
    texture_bindings_dirty_ = true;
    current_texture_map_mask_ = 0u;
    current_texture_binding_key_ = {};
    current_texture_binding_key_valid_ = false;
    clear_texture_binding_table_cache();
    texture_handle_cache_.clear();
    dirty_texture_ranges_scratch_.clear();

    // The new EFB/XFB resources have no relationship to the old session's
    // seed, frame stamps, or registry addresses.
    frame_index_ = 0u;
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
}

// ---------------------------------------------------------------------------
// GxBackend lifecycle
// ---------------------------------------------------------------------------

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

    // Reset is deliberately before native resource acquisition. It audits the
    // previous session and advances the token epoch, so a failed prior session
    // cannot be hidden by renderer reinitialization and an old token can never
    // alias this one.
    frame_pe_completions_.reset();
    reset_fifo_session_state();
    deferred_gx_stats_count_ = 0;
    deferred_gx_stats_dropped_ = 0;

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
    // shutdown() calls this only after joining the render thread.  Do all
    // potentially blocking stderr work here, never from render_frame_on_thread.
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
                  << static_cast<double>(sample.render_ns) / 600000000.0
                  << " producer-wait-ms="
                  << static_cast<double>(sample.queue_wait_ns) / 600000000.0
                  << " dependency-scan-ms="
                  << static_cast<double>(sample.dependency_scan_ns) /
                         600000000.0
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
                  << static_cast<double>(sample.snapshot_ns) / 600000000.0
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
                  << sample.inline_matrix_uploads << '\n';
    }
    if (deferred_gx_stats_dropped_ != 0u) {
        std::cerr << "[gx-stats] deferred-samples-dropped="
                  << deferred_gx_stats_dropped_ << '\n';
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

    MemorySnapshotStorage* target = nullptr;
    for (MemorySnapshotStorage& storage : snapshot.storage) {
        if (storage.source == source && storage.size == size) {
            target = &storage;
            break;
        }
    }
    if (target == nullptr) {
        for (MemorySnapshotStorage& storage : snapshot.storage) {
            if (!storage.used) {
                target = &storage;
                target->source = source;
                target->size = size;
                break;
            }
        }
    }
    if (target == nullptr) {
        snapshot.storage.emplace_back();
        target = &snapshot.storage.back();
        target->source = source;
        target->size = size;
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
        immutable_range_cache_.erase(victim);
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
    for (ImmutableRangeCacheEntry& entry : immutable_range_cache_) {
        if (!entry.valid) {
            continue;
        }
        for (const GuestWriteRange& range : ranges) {
            if (dependency_ranges_overlap(
                    entry.guest_base, entry.size, range.guest_addr, range.size)) {
                entry.valid = false;
                break;
            }
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
    snapshot.immutable_ranges.clear();
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
    const std::uint64_t fifo_hash = cacheable ? hash_bytes(fifo) : 0u;
    const std::uint64_t state_hash =
        cacheable ? dependency_shape_state_hash(dependency_state_) : 0u;
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
    // CALL_DL bytes are both dependency shape and render input. Keeping them
    // only in the parser-cache signature made compact snapshots accidentally
    // depend on that cache retaining a private copy. Own every recursively
    // visited display-list range so cache invalidation and delayed parsing are
    // correct independently.
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
        target->valid = true;
        target->fifo_hash = fifo_hash;
        target->state_hash = state_hash;
        target->pending_bp_write_mask = pending_bp_write_mask;
        target->last_used = ++dependency_range_cache_tick_;
        target->fifo.assign(fifo.begin(), fifo.end());
        target->ranges = out;
        target->shape_ranges = std::move(shape_ranges);
        target->final_state = dependency_state_;
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
        // The caller quiesces the render thread before taking this snapshot.
        // Copy into fixed storage so repeated snapshots do not mutate the
        // retained evidence and cannot allocate after the measured window.
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
        for (std::uint32_t page = first_page; page <= last_page; ++page) {
            const std::size_t word = page / 64u;
            const std::uint64_t bit = 1ull << (page % 64u);
            dirty_page_words_[word].fetch_or(bit, std::memory_order_relaxed);
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
    GuestWriteRange pending{};
    bool have_pending = false;
    const auto flush_pending = [&]() {
        if (have_pending) {
            out.push_back(pending);
            pending = {};
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
    for (std::size_t word_index = 0; word_index < dirty_page_words_.size();
         word_index = word_index + 1u == kMem1EndWord
             ? kMem2BeginWord : word_index + 1u) {
        // An empty word needs no locked exchange. A concurrent producer that
        // marks it after this observation leaves its bit for the next drain,
        // just as it would after an exchange that returned zero. Never clear
        // a nonempty word with a plain store: preserve concurrent additions.
        if (dirty_page_words_[word_index].load(std::memory_order_relaxed) == 0u) {
            continue;
        }
        std::uint64_t bits =
            dirty_page_words_[word_index].exchange(0u, std::memory_order_acquire);
        while (bits != 0u) {
            const unsigned bit_index =
                static_cast<unsigned>(std::countr_zero(bits));
            bits &= bits - 1u;
            const std::uint32_t page =
                static_cast<std::uint32_t>(word_index * 64u + bit_index);
            if (page >= kDirtyPageCount) {
                continue;
            }

            const std::uint32_t addr =
                kDirtyTrackedBase + page * kDirtyPageSize;
            if (have_pending &&
                pending.guest_addr + pending.size == addr) {
                pending.size += kDirtyPageSize;
            } else {
                flush_pending();
                pending = GuestWriteRange{addr, kDirtyPageSize};
                have_pending = true;
            }
        }
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
        for (unsigned byte = 0; byte < 8; ++byte) {
            hash ^= (value >> (byte * 8u)) & 0xFFu;
            hash *= 1099511628211ull;
        }
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
    return static_cast<std::size_t>(hash);
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
    std::vector<DecodedPacketRunCacheEntry::GuestDependencySnapshot>&
        snapshots,
    std::size_t& captured_bytes) {
    std::vector<DecodedPacketRunCacheEntry::GuestDependencySnapshot> captured;
    captured.reserve(dependencies.size());
    std::size_t total_bytes = 0u;
    for (const VertexDecodeGuestRange& dependency : dependencies) {
        if (dependency.size == 0u) {
            continue;
        }
        const std::size_t size = dependency.size;
        if (total_bytes > byte_budget || size > byte_budget - total_bytes) {
            snapshots.clear();
            captured_bytes = 0u;
            return false;
        }
        const std::byte* source = resolve_guest_const_bytes(
            memory, dependency.guest_base, dependency.size);
        if (source == nullptr) {
            snapshots.clear();
            captured_bytes = 0u;
            return false;
        }
        DecodedPacketRunCacheEntry::GuestDependencySnapshot snapshot{};
        snapshot.guest_base = dependency.guest_base;
        snapshot.bytes.assign(source, source + size);
        total_bytes += size;
        captured.push_back(std::move(snapshot));
    }
    snapshots = std::move(captured);
    captured_bytes = total_bytes;
    return true;
}

bool GxBackend::decoded_packet_run_dependencies_match(
    GuestMemoryV1* memory,
    const std::vector<
        DecodedPacketRunCacheEntry::GuestDependencySnapshot>& snapshots)
    noexcept {
    for (const auto& snapshot : snapshots) {
        if (snapshot.bytes.empty()) {
            continue;
        }
        if (snapshot.bytes.size() >
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            return false;
        }
        const auto size = static_cast<std::uint32_t>(snapshot.bytes.size());
        const std::byte* source = resolve_guest_const_bytes(
            memory, snapshot.guest_base, size);
        if (source == nullptr ||
            std::memcmp(source, snapshot.bytes.data(), snapshot.bytes.size()) !=
                0) {
            return false;
        }
    }
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
        decoded_packet_run_cache_.erase(it);
        ++frame_decoded_vertex_cache_evictions_;
    }
}

std::size_t GxBackend::invalidate_dirty_decoded_packet_run_ranges(
    const std::vector<GuestWriteRange>& ranges) noexcept {
    if (ranges.empty() || decoded_packet_run_cache_.empty()) {
        return 0;
    }

    std::size_t invalidated = 0;
    const bool use_sorted_ranges = sorted_vertex_invalidation_enabled();
    for (auto it = decoded_packet_run_cache_.begin();
         it != decoded_packet_run_cache_.end();) {
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
            it = decoded_packet_run_cache_.erase(it);
            ++invalidated;
        } else {
            ++it;
        }
    }
    return invalidated;
}

// ---------------------------------------------------------------------------
// Per-frame rendering
// ---------------------------------------------------------------------------

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

// Producer (sim thread): snapshot the frame and hand it to the render
// thread.  Blocks only when the consumer is a full frame behind.
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
    const bool fifo_empty = fifo_data == nullptr || fifo_size == 0u;
    const bool worker_snapshot = worker_memory_snapshot_enabled();
    if (worker_snapshot && !fifo_empty && memory == nullptr) {
        throw std::logic_error(
            "[GxBackend] worker memory snapshot has no guest memory owner");
    }
    std::vector<GuestWriteRange> drained_dirty_ranges;
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
    const auto wait_start =
        (trace_gx_stats || trace_gx_stalls)
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
    FrameChunk chunk;
    chunk.pointer_response=producer_pointer_response_;
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
        queue_space_cv_.wait(lock, [this, queue_depth_limit, &queued_render_chunks] {
            return queued_render_chunks() < queue_depth_limit ||
                   stop_render_thread_ ||
                   render_thread_error_ != nullptr;
        });
        if (cadence_diagnostics_ != nullptr) {
            const std::uint64_t diagnostic_queue_wait_end_ns =
                cadence::steady_now_ns_if_enabled(cadence_diagnostics_);
            diagnostic_queue_wait_ns =
                diagnostic_queue_wait_end_ns >= diagnostic_queue_wait_start_ns
                ? diagnostic_queue_wait_end_ns -
                      diagnostic_queue_wait_start_ns
                : 0u;
        }
        if (trace_gx_stats || trace_gx_stalls) {
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
    if (!worker_snapshot) {
        // The renderer consumes its own chunk's dirty ranges in FIFO order.
        // Applying later producer writes to that parser also held its mutex
        // through earlier PSO creation and defeated detached snapshots.
        invalidate_dirty_display_list_ranges(dependency_parser_, chunk.dirty_ranges);
    }
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
            const auto dependency_scan_start =
                (trace_gx_stats || trace_gx_stalls)
                    ? std::chrono::steady_clock::now()
                    : std::chrono::steady_clock::time_point{};
            collect_memory_dependency_ranges(
                chunk.fifo.data(),
                chunk.fifo.size(),
                memory,
                chunk.memory_ranges,
                chunk.dependency_cache_hit);
            if (trace_gx_stats || trace_gx_stalls) {
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
        const auto snapshot_start = (trace_gx_stats || trace_gx_stalls)
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
        if (trace_gx_stats || trace_gx_stalls) {
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
    if (timing.pe_events_gpu_fence && !worker_snapshot) {
        // Classify the immutable FIFO copy and the exact memory view the
        // render parser will consume. In snapshot mode, classifying live guest
        // memory before capture allowed a concurrent native DMA write to make
        // CALL_DL/LOAD_INDX command boundaries disagree after an event-free
        // receipt had already been issued.
        const auto classification_start =
            (trace_gx_stats || trace_gx_stalls || trace_gx_microprofile)
                ? std::chrono::steady_clock::now()
                : std::chrono::steady_clock::time_point{};
        pe_event_classifier_.classify(
            chunk.fifo.data(),
            chunk.fifo.size(),
            chunk.memory_snapshot_active ? chunk.memory : memory,
            chunk.preclassified_pe_events);
        if (trace_gx_stats || trace_gx_stalls || trace_gx_microprofile) {
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
            if (chunk.memory_snapshot_active) {
                chunk.memory_snapshot.immutable_ranges.clear();
                memory_snapshot_pool_.push_back(std::move(chunk.memory_snapshot));
            }
            if (!chunk.memory_ranges.empty()) {
                memory_range_pool_.push_back(std::move(chunk.memory_ranges));
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
    const std::uint64_t producer_wait_us = producer_wait_ns / 1000u;
    if (trace_gx_stalls &&
        producer_wait_us >= trace_gx_stall_threshold_us()) {
        std::cerr << "[gx-queue-wait] wait-us=" << producer_wait_us
                  << " fifo-bytes=" << fifo_size
                  << " queue-depth=" << queue_depth_after_push
                  << " queue-limit=" << queue_depth_limit << '\n';
    }
    queue_cv_.notify_one();
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
                pending_frame_effects_.empty());
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
                render_cpu_reads_in_flight_ == 0u);
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

bool GxBackend::enqueue_pointer_depth_capture(const std::shared_ptr<PointerDepthCapture>& capture) {
    if (!initialized_ || !capture) return false;
    if (!capture->complete.exchange(false,std::memory_order_acq_rel))
        throw std::logic_error("pointer depth capture reused while owned by render worker");
    capture->success=false; capture->error=nullptr; capture->worker_ns=0u;
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
    (void)SetThreadDescription(GetCurrentThread(), L"Nebula GX Render");
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    EfbPeekRequest* active_peek = nullptr;
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
            if (idle_present || poll_frame_effects) {
                queue_cv_.wait_for(lock, std::chrono::milliseconds(1), [this] {
                    return stop_render_thread_ || !frame_queue_.empty();
                });
            } else {
                queue_cv_.wait(lock, [this] {
                    return stop_render_thread_ || !frame_queue_.empty();
                });
            }
            if (frame_queue_.empty()) {
                if (stop_render_thread_) {
                    if (pending_frame_effects_.empty()) {
                        break;  // stop requested, queue and PE fences drained
                    }
                    shutdown_fence =
                        pending_frame_effects_.back().fence_value;
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
                    ++utility_chunks_in_flight_;
                } else if (chunk.present_only) {
                    ++present_chunks_in_flight_;
                } else {
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
            } else if (have_chunk) {
                queue_space_cv_.notify_all();
                if (chunk.efb_peek_only) {
                    execute_efb_peek_on_thread(chunk);
                    active_peek = nullptr;
                } else if (chunk.present_only) {
                    present_cached_xfb_on_thread(
                        chunk.displayed_xfb_addr,
                        false);
                } else {
                    prepare_worker_memory_snapshot(chunk);
                    render_frame_on_thread(chunk);
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
            fifo_pool_.push_back(std::move(chunk.fifo));
            if (!chunk.memory_ranges.empty()) {
                memory_range_pool_.push_back(std::move(chunk.memory_ranges));
            }
            if (chunk.memory_snapshot_active) {
                // Guest bytes are consumed synchronously while command lists
                // are recorded. GPU work references only upload/default-heap
                // resources whose frame slots retire on their exact fence.
                chunk.memory_snapshot.immutable_ranges.clear();
                memory_snapshot_pool_.push_back(
                    std::move(chunk.memory_snapshot));
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

void GxBackend::execute_efb_peek_on_thread(FrameChunk& chunk) {
    if (chunk.pointer_depth_capture) {
        auto& capture=*chunk.pointer_depth_capture;
        const auto start=std::chrono::steady_clock::now();
        try {
            capture.success=renderer_.capture_pointer_depth(capture.pixels);
            if (!capture.success) throw std::runtime_error("owned pointer depth capture failed");
        } catch (...) {
            capture.error=std::current_exception();
            capture.success=false;
            capture.publish_complete();
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
            chunk.preclassified_pe_events);
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
        // No guest-visible PE signal exists for this capture. This function is
        // called only after RendererD3D12::end_frame returned successfully,
        // proving close, ordered queue submission, and queue signal all
        // succeeded. Resolve at that exact submission boundary; later work is
        // queue-ordered and no GPU-idle wait is warranted.
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
        for (const PeEvent& event : frame.pe_events) {
            deliver_pe_event(frame.services, event);
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
        if (path.empty()) {
            throw std::runtime_error("normal-present capture requires an output path");
        }
        renderer_.debug_log_backbuffer_readback();
        renderer_.set_debug_capture_paths({}, path);
    }
    renderer_.begin_frame(true);
    // Present-only frames do not acquire or resize XFB textures. Keep
    // EfbCopyManager retirement/recycling tied to render frames that can
    // actually produce GXCopyDisp resources; otherwise the split
    // render-then-present cadence advances XFB lifetime twice per VI.
    renderer_.present(selected, capture_present);
    renderer_.end_frame();
    if (capture_present) {
        renderer_.set_debug_capture_paths({}, {});
        std::cerr << "[gx-present-capture] request=" << present_requests
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
    const bool frame_timing_enabled =
        frame_profile_enabled || trace_gx_stats;
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
    frame_flush_matrix_us_ = 0;
    frame_flush_constants_us_ = 0;
    frame_flush_texture_us_ = 0;
    frame_vertex_load_us_ = 0;
    frame_cached_vertex_validation_us_ = 0;
    frame_cached_vertex_upload_us_ = 0;
    frame_draw_record_us_ = 0;
    frame_efb_copy_us_ = 0;
    frame_decoded_vertex_cache_hits_ = 0;
    frame_decoded_vertex_cache_misses_ = 0;
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
        renderer_.begin_frame(chunk.present_swap_chain);
        efb_copies_.begin_frame(renderer_.frame_slot(), kFramesInFlight);
        texture_cache_.begin_frame(renderer_.frame_slot(), kFramesInFlight);
        clear_frame_texture_binding_table_cache();
        pipeline_cache_.drain_completions();
        // New decoded textures upload through this frame's command list.
        texture_cache_.set_upload_list(renderer_.command_list());
    }
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

    // Bind EFB with default (null) viewport — the XF viewport registers
    // will provide the actual values once processed from the FIFO.
    renderer_.bind_efb(nullptr);

    // GX semantics: the EFB persists across frames and is cleared ONLY by
    // an EFB copy with the clear bit set (handled in on_efb_copy with the
    // latched copy-clear registers).  Clearing unconditionally every frame
    // with stale latched colors caused the boot105-107 purple/blue/white
    // frame flicker.  One initial clear (depth = FAR) seeds the targets.
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
    capture_readback_draw_ = [] {
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
        frame_services_ = services;

        try {
            if (pending_fifo_.empty()) {
                const std::span<const std::byte> fifo(fifo_data, fifo_size);
                const std::size_t consumed =
                    parser_.run_available(fifo, memory, *this, state_);
                if (consumed < fifo.size()) {
                    pending_fifo_.assign(
                        fifo.begin() + static_cast<std::ptrdiff_t>(consumed),
                        fifo.end());
                }
            } else {
                if (fifo_data != nullptr && fifo_size > 0) {
                    pending_fifo_.insert(
                        pending_fifo_.end(), fifo_data, fifo_data + fifo_size);
                }
                const std::size_t consumed = parser_.run_available(
                    pending_fifo_, memory, *this, state_);
                if (consumed == pending_fifo_.size()) {
                    pending_fifo_.clear();
                } else if (consumed > 0) {
                    pending_fifo_.erase(
                        pending_fifo_.begin(),
                        pending_fifo_.begin() +
                            static_cast<std::ptrdiff_t>(consumed));
                }
            }
        } catch (const GxFatalError& e) {
            parser_.set_profile(nullptr);
            frame_memory_ = nullptr;
            frame_services_ = nullptr;
            frame_pe_events_ = saved_pe_events;
            frame_pe_callbacks_enabled_ = saved_pe_callbacks_enabled;
            std::cerr << "[GxBackend] fatal FIFO error: " << e.what()
                      << " at offset 0x" << std::hex << e.fifo_offset()
                      << " opcode=0x" << static_cast<unsigned>(e.opcode())
                      << std::dec << '\n';
            throw;
        } catch (...) {
            parser_.set_profile(nullptr);
            frame_memory_ = nullptr;
            frame_services_ = nullptr;
            frame_pe_events_ = saved_pe_events;
            frame_pe_callbacks_enabled_ = saved_pe_callbacks_enabled;
            throw;
        }

        frame_memory_   = nullptr;
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
        renderer_.debug_copy_efb_to_readback();
    }

    // Present every VI after the first valid XFB. SMG has legitimate retraces
    // that only update EFB/texture side effects or queue no FIFO at all; if
    // those retraces skip the swap chain, flip-model presentation cadence
    // collapses and the player sees black/jittery gaps even though the game
    // is still advancing. Before the first XFB exists, keep the window hidden
    // unless a diagnostic capture explicitly asks for a backbuffer.
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
    const bool capture_backbuffer =
        capture_this_frame && selected_xfb != nullptr;
    const bool present_swap_chain =
        capture_backbuffer ||
        (chunk.present_swap_chain && selected_xfb != nullptr &&
         !skip_duplicate_present);
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
        renderer_.present(selected_xfb, capture_backbuffer);
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
    const std::uint64_t submitted_fence =
        renderer_.end_frame(present_swap_chain);
    if (chunk.present_swap_chain && present_swap_chain &&
        selected_xfb != nullptr) {
        present_epoch_ = current_present_epoch;
        record_presented_xfb_copy(*selected_xfb);
        last_presented_xfb_addr_.store(
            selected_xfb->guest_addr, std::memory_order_relaxed);
    }
    // Preclassified event-free receipts were made ready on the producer and
    // have no pending FIFO-effects state here. Compatibility/direct-callback
    // captures resolve at this successful ordered-submission boundary; every
    // deferred PE callback binds to this capture's exact GPU fence.
    queue_frame_fifo_effects_fence(chunk, submitted_fence);
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
    if (trace_xfb_causal_enabled() || trace_xfb_causal_deferred_enabled()) {
        causal_trace_frame_index_.store(
            frame_index_, std::memory_order_relaxed);
    }
    const auto render_end = frame_timing_enabled
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
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

    if (frame_profile_enabled) {
        const std::uint64_t total_us = elapsed_us(render_start, render_end);
        const std::uint64_t parse_us = elapsed_us(parse_start, parse_end);
        const std::uint64_t submit_us = elapsed_us(parse_end, gpu_submit_end);
        const std::uint64_t producer_wait_us = chunk.producer_wait_ns / 1000u;
        if (total_us >= trace_gx_stall_threshold_us() ||
            producer_wait_us >= trace_gx_stall_threshold_us() ||
            frame_microprofile_enabled_) {
            std::ostringstream timing_message;
            timing_message << "[gx-frame-timing] frame=" << frame_index_
                  << " fifo-bytes=" << fifo_size
                  << " total-us=" << total_us
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
                  << " flush-matrix-us=" << frame_flush_matrix_us_
                  << " flush-constants-us=" << frame_flush_constants_us_
                  << " flush-texture-us=" << frame_flush_texture_us_
                  << " vertex-load-us=" << frame_vertex_load_us_
                  << " cached-vertex-validation-us=" << frame_cached_vertex_validation_us_
                  << " cached-vertex-upload-us=" << frame_cached_vertex_upload_us_
                  << " immutable-vertex-reused-bytes=" << renderer_.vertex_ring().reused_bytes()
                  << " immutable-vertex-copied-bytes=" << renderer_.vertex_ring().copied_bytes()
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
                  << " decoded-vtx-cache-bytes="
                  << decoded_packet_run_cache_bytes_
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

    if (trace_gx_stats && frame_index_ % 600 == 0) {
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
        if (deferred_gx_stats_count_ < deferred_gx_stats_.size()) {
            deferred_gx_stats_[deferred_gx_stats_count_++] = sample;
        } else {
            ++deferred_gx_stats_dropped_;
        }
        stat_palette_uploads_ = 0;
        stat_inline_matrix_uploads_ = 0;
    }

    // Normal play persists the shader cache at shutdown. Periodic disk writes
    // are opt-in diagnostics because even an occasional cache flush can show
    // up as a visible pacing hitch during gameplay.
    constexpr std::uint64_t kFlushInterval = 600;
    if (shader_cache_periodic_flush_enabled() &&
        frame_index_ % kFlushInterval == 0) {
        pipeline_cache_.flush_disk();
    }
}

// ---------------------------------------------------------------------------
// flush_draw_state: build shader/render keys, upload constants, return PSO.
// Returns nullptr when no pipeline is ready (draw is skipped).
// ---------------------------------------------------------------------------

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
        generated_mips ? 1u : 0u,
    };
}

void GxBackend::clear_frame_texture_binding_table_cache() {
    frame_texture_tables_.clear();
    frame_texture_binding_tables_.clear();
    frame_texture_handle_cache_.clear();
}

void GxBackend::clear_texture_binding_table_cache() {
    clear_frame_texture_binding_table_cache();
    persistent_texture_binding_tables_.clear();
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
    erase_matching_tables(persistent_texture_binding_tables_);
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
    prune_cache(persistent_texture_binding_tables_);
    dirty_texture_aliases_.clear();
}

ID3D12PipelineState* GxBackend::flush_draw_state(
    D3D12_PRIMITIVE_TOPOLOGY_TYPE primitive_topology_type) {
    const bool time_detail =
        frame_microprofile_enabled_ || trace_gx_stalls_enabled();
    const auto flush_start = time_detail
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    const auto topology_key =
        static_cast<std::uint8_t>(primitive_topology_type);
    // Peek at dirty bits without consuming; if we end up skipping the draw,
    // we need to preserve dirty state so the next draw re-uploads constants.
    // We consume only after confirming a pipeline is available.
    const std::uint32_t dirty = state_.consume_dirty();
    if (dirty == 0u &&
        !texture_bindings_dirty_ &&
        !frame_bindings_dirty_ &&
        !matrix_palette_stale_ &&
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
    const bool tev_dirty  = (dirty & GxState::kDirtyTev)         != 0;
    const bool rs_dirty   = (dirty & GxState::kDirtyRenderState)  != 0;
    const bool xf_dirty   = (dirty & GxState::kDirtyXfShader)    != 0;
    const bool vcd_dirty  = (dirty & GxState::kDirtyVcd)         != 0;
    const bool mtx_dirty  = (dirty & GxState::kDirtyXfMatrices)  != 0;
    const bool tev_c_dirty = (dirty & GxState::kDirtyTevConstants) != 0;
    const bool vs_c_dirty = (dirty & GxState::kDirtyVsConstants) != 0;
    if (mtx_dirty) {
        matrix_palette_stale_ = true;
        inline_matrices_stale_ = true;
    }

    const std::uint8_t texture_map_mask =
        sampled_texture_map_mask(state_);
    const TextureBindingKey texture_binding_key =
        capture_texture_binding_key(texture_map_mask);
    const bool texture_binding_changed =
        !current_texture_binding_key_valid_ ||
        !(texture_binding_key == current_texture_binding_key_);
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
    shader_key_hashes_valid_ = true;
    if (rs_dirty) {
        render_state_key_ = build_render_state_key(state_);
    }
    const bool topology_dirty =
        render_state_key_.primitive_topology != topology_key;
    render_state_key_.primitive_topology = topology_key;

    if (current_pipeline_ == nullptr || tev_dirty || xf_dirty ||
        vcd_dirty || rs_dirty || topology_dirty) {
        const PsoKey pso_key{
            current_vs_hash_,
            current_ps_hash_,
            render_state_key_,
        };
        const auto pso_start = time_detail
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        if (current_pipeline_ == nullptr || !current_pso_key_valid_ ||
            !(current_pso_key_ == pso_key)) {
            current_pipeline_ = pipeline_cache_.get(pso_key, ps_key_, vs_key_);
            current_pso_key_ = pso_key;
            current_pso_key_valid_ = current_pipeline_ != nullptr;
        }
        if (time_detail) {
            frame_flush_pso_us_ +=
                elapsed_us(pso_start, std::chrono::steady_clock::now());
        }
    }
    if (current_pipeline_ == nullptr) {
        // No pipeline ready (M2 — uber shader not yet compiled).  Re-arm
        // all dirty bits so the next draw attempt re-uploads constants
        // when a pipeline becomes available.
        throw std::runtime_error(
            "[GxBackend] failed to create required graphics pipeline");
    }

    // Snapshot the XF matrix palette ONLY when the matrices actually changed
    // (kDirtyXfMatrices).  Re-uploading the full 6144-byte palette on every
    // TEV/render-state change exhausted the per-frame matrix ring the moment
    // the scene started rendering hundreds of draws ("UploadRing 'matrix'
    // exhausted" → crash).  Draws whose matrices are unchanged reuse the last
    // snapshot's GPU VA, which stays valid until begin_frame() rewinds the
    // ring at the next frame.  The in-shader palette base is 0 (the root SRV
    // is bound at the snapshot's own VA).
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
    if (palette_required &&
        (matrix_palette_stale_ || current_matrix_palette_ == 0)) {
        const auto matrix_start = time_detail
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        const std::size_t palette_bytes =
            kMatrixPaletteWords * sizeof(std::uint32_t);
        UploadRing::Allocation mtx_alloc =
            renderer_.matrix_ring().allocate(palette_bytes, /*alignment=*/256u);
        // Single bulk copy from the contiguous XF register file (replaces the
        // per-word loop — this runs on most draws of a busy frame because SMG
        // re-loads matrices per object via LOAD_INDX).
        std::memcpy(mtx_alloc.cpu, state_.xf_raw(), palette_bytes);
        current_matrix_palette_ = mtx_alloc.gpu;
        matrix_palette_stale_ = false;
        ++stat_palette_uploads_;
        if (time_detail) {
            frame_flush_matrix_us_ +=
                elapsed_us(matrix_start, std::chrono::steady_clock::now());
        }
    }

    // Upload VS constants when scalar state or fixed inline matrices change.
    if (vs_c_dirty || inline_matrices_stale_ ||
        frame_bindings_dirty_ || dirty == ~0u) {
        const auto constants_start = time_detail
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        GxVsConstants vs_c{};
        fill_vs_constants(
            state_, /*palette_base=*/0u, renderer_.efb_scale(), vs_c);
        UploadRing::Allocation vs_alloc =
            renderer_.constant_ring().allocate(sizeof(GxVsConstants), 256u);
        std::memcpy(vs_alloc.cpu, &vs_c, sizeof(vs_c));
        current_vs_constants_ = vs_alloc.gpu;
        inline_matrices_stale_ = false;
        ++stat_inline_matrix_uploads_;
        if (time_detail) {
            frame_flush_constants_us_ +=
                elapsed_us(constants_start, std::chrono::steady_clock::now());
        }
    }

    // Upload PS constants when TEV stage config / fog / alpha / tex dims change.
    if (tev_dirty || tev_c_dirty || tex_dirty || frame_bindings_dirty_ ||
        dirty == ~0u) {
        const auto constants_start = time_detail
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        GxPsConstants ps_c{};
        fill_ps_constants(state_, ps_c);

        UploadRing::Allocation ps_alloc =
            renderer_.constant_ring().allocate(sizeof(GxPsConstants), 256u);
        std::memcpy(ps_alloc.cpu, &ps_c, sizeof(ps_c));
        current_ps_constants_ = ps_alloc.gpu;
        if (time_detail) {
            frame_flush_constants_us_ +=
                elapsed_us(constants_start, std::chrono::steady_clock::now());
        }
    }

    // Refresh texture descriptor table when any texture register changed.
    if (tex_dirty || dirty == ~0u) {
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

        if (texture_map_mask == 0u) {
            current_texture_table_ = {};
            current_sampler_table_ = {};
        } else {
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
                        texture_cache_.srv_heap()->GetCPUDescriptorHandleForHeapStart();
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
                // cached/persistent.
                std::uint32_t samp_keys[8]{};
                for (unsigned m = 0; m < kMaxTextureMaps; ++m) {
                    const TexMode mode = state_.tex_mode(m);
                    bool generated_mips = false;
                    std::uint8_t mip_levels = 1u;
                    if ((texture_map_mask & (1u << m)) != 0u) {
                        const TextureHandle& th = texture_handles[m];
                        generated_mips = th.generated_mips;
                        mip_levels = th.mip_levels;
                    }
                    samp_keys[m] =
                        pack_sampler_key(mode, generated_mips, mip_levels);
                }
                const auto sampler_start = time_detail
                    ? std::chrono::steady_clock::now()
                    : std::chrono::steady_clock::time_point{};
                current_sampler_table_ =
                    renderer_.sampler_table_for(samp_keys);
                if (time_detail) {
                    sampler_table_us = elapsed_us(
                        sampler_start, std::chrono::steady_clock::now());
                }
                TextureBindingTables cached_tables{
                    current_texture_table_,
                    current_sampler_table_,
                    binding_tables.dependencies,
                    binding_tables.dependency_count,
                };
                frame_texture_binding_tables_.emplace(
                    texture_binding_key,
                    cached_tables);
            }
        }
        current_texture_map_mask_ = texture_map_mask;
        current_texture_binding_key_ = texture_binding_key;
        current_texture_binding_key_valid_ = true;
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

    const bool time_detail =
        frame_microprofile_enabled_ || trace_gx_stalls_enabled();
    const auto draw_record_start = time_detail
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    renderer_.draw(pending_draw_batch_.call);
    if (time_detail) {
        frame_draw_record_us_ +=
            elapsed_us(draw_record_start, std::chrono::steady_clock::now());
    }
    ++frame_draw_batches_;
    pending_draw_batch_ = {};
}

// ---------------------------------------------------------------------------
// FifoSink callbacks
// ---------------------------------------------------------------------------

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

    const ScissorRect sc = state_.scissor();
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

    ID3D12PipelineState* pso = flush_draw_state(topology_type);
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
    cached_draw_run_state_call_.matrix_palette = current_matrix_palette_;
    cached_draw_run_state_call_.topology       = topology;
    cached_draw_run_state_call_.scissor        = scissor_rect;
    for (unsigned i = 0; i < 6; ++i) {
        cached_draw_run_state_call_.xf_viewport[i] = std::bit_cast<float>(
            state_.xf(static_cast<std::uint16_t>(xf::kViewportBase + i)));
    }
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

    const bool time_detail =
        frame_microprofile_enabled_ || trace_gx_stalls_enabled();
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
            const bool dependencies_match =
                shape_matches && decoded_packet_run_dependencies_match(
                    frame_memory_, it->second.guest_array_snapshots);
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
                it->second.last_used = ++decoded_packet_run_cache_tick_;
                ++frame_decoded_vertex_cache_hits_;
                decoded_cache_hit = true;
                const auto upload_start = time_detail
                    ? std::chrono::steady_clock::now()
                    : std::chrono::steady_clock::time_point{};
                prim = vertex_loader_.upload_cached_packet_run_vertices(
                    it->second.vertices,
                    base_offset,
                    packets,
                    primitive,
                    renderer_.vertex_ring(),
                    renderer_.index_ring(),
                    append_to_pending && cached_draw_run_primitive_indexed_
                        ? pending_draw_batch_.base_vertex
                        : 0u,
                    append_to_pending && cached_draw_run_primitive_indexed_,
                    it->second.total_indices,
                    precomputed_indices,
                    immutable_vertex_upload_reuse_enabled()
                        ? &it->second.upload_token : nullptr);
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
                parser_provided_totals ? total_indices : 0u);
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
        std::vector<
            DecodedPacketRunCacheEntry::GuestDependencySnapshot>
            dependency_snapshots;
        std::size_t dependency_snapshot_bytes = 0u;
        const bool dependencies_captured =
            decoded_cache_enabled && !decoded.vertices.empty() &&
            capture_decoded_packet_run_dependencies(
                frame_memory_,
                decoded.guest_array_reads,
                kDecodedPacketRunCacheMaxEntryBytes - decoded_vertex_bytes,
                dependency_snapshots,
                dependency_snapshot_bytes);
        if (dependencies_captured) {
            DecodedPacketRunCacheEntry entry{};
            entry.key = decoded_cache_key;
            entry.total_indices = decoded.total_indices;
            entry.last_used = ++decoded_packet_run_cache_tick_;
            entry.byte_size = decoded_vertex_bytes + dependency_snapshot_bytes;
            entry.vertices = std::move(decoded.vertices);
            entry.guest_array_reads = std::move(decoded.guest_array_reads);
            entry.guest_array_snapshots = std::move(dependency_snapshots);
            if (auto existing =
                    decoded_packet_run_cache_.find(decoded_cache_key);
                existing != decoded_packet_run_cache_.end()) {
                decoded_packet_run_cache_bytes_ -= existing->second.byte_size;
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
        renderer_.vertex_ring().resource()->GetGPUVirtualAddress()
        + prim.vertex_byte_offset;
    vbv.SizeInBytes =
        prim.vertex_count * static_cast<UINT>(sizeof(GxVertexOut));
    vbv.StrideInBytes = static_cast<UINT>(sizeof(GxVertexOut));

    D3D12_INDEX_BUFFER_VIEW ibv{};
    if (prim.indexed) {
        ibv.BufferLocation =
            renderer_.index_ring().resource()->GetGPUVirtualAddress()
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
    pending_draw_batch_.indexed = prim.indexed;
    pending_draw_batch_.base_vertex = prim.base_vertex;
    pending_draw_batch_.vertex_count = prim.vertex_count;
    pending_draw_batch_.vertex_byte_end = vertex_byte_end;
    pending_draw_batch_.first_index = prim.first_index;
    pending_draw_batch_.index_count = prim.index_count;
    pending_draw_batch_.draw_count =
        static_cast<std::uint32_t>(packets.size());
    return true;
}

void GxBackend::on_cached_draw_run_draw(
    PrimitiveClass primitive,
    std::uint8_t vtxfmt,
    FifoCursor& cursor) {
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
        const bool time_detail =
            frame_microprofile_enabled_ || trace_gx_stalls_enabled();
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

    const bool time_detail =
        frame_microprofile_enabled_ || trace_gx_stalls_enabled();
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
        renderer_.vertex_ring().resource()->GetGPUVirtualAddress()
        + prim.vertex_byte_offset;
    vbv.SizeInBytes =
        prim.vertex_count * static_cast<UINT>(sizeof(GxVertexOut));
    vbv.StrideInBytes = static_cast<UINT>(sizeof(GxVertexOut));

    D3D12_INDEX_BUFFER_VIEW ibv{};
    if (prim.indexed) {
        ibv.BufferLocation =
            renderer_.index_ring().resource()->GetGPUVirtualAddress()
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
    const bool time_detail =
        frame_microprofile_enabled_ || trace_gx_stalls_enabled();
    D3D12_PRIMITIVE_TOPOLOGY topology{};
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topology_type{};
    gx_topology_for_primitive(primitive, topology, topology_type);

    const ScissorRect sc = state_.scissor();
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

    ID3D12PipelineState* pso = flush_draw_state(topology_type);
    if (pso == nullptr) {
        flush_pending_draw_batch();
        // No pipeline ready; vertex_loader still needs to consume the packet
        // so the cursor advances correctly.  We skip drawing but must not
        // leave the cursor in the middle of the stream.
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
    state_call.matrix_palette = current_matrix_palette_;
    state_call.topology       = topology;
    state_call.scissor        = scissor_rect;
    for (unsigned i = 0; i < 6; ++i) {
        state_call.xf_viewport[i] = std::bit_cast<float>(
            state_.xf(static_cast<std::uint16_t>(xf::kViewportBase + i)));
    }
    state_call.has_viewport =
        state_call.xf_viewport[0] > 0.0f &&
        state_call.xf_viewport[1] < 0.0f;

    const bool primitive_indexed =
        primitive != PrimitiveClass::Triangles;
    bool append_to_pending =
        pending_draw_batch_.active &&
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
        renderer_.vertex_ring().resource()->GetGPUVirtualAddress()
        + prim.vertex_byte_offset;
    vbv.SizeInBytes    =
        prim.vertex_count * static_cast<UINT>(sizeof(GxVertexOut));
    vbv.StrideInBytes  = static_cast<UINT>(sizeof(GxVertexOut));

    D3D12_INDEX_BUFFER_VIEW ibv{};
    if (prim.indexed) {
        ibv.BufferLocation =
            renderer_.index_ring().resource()->GetGPUVirtualAddress()
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
                "blend=%06X alpha=%06X depth=%02X scissor=%d,%d,%d,%d "
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
                fill_projection(state_, capture_projection);
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
                fill_projection(state_, projection);
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
        pending_draw_batch_.indexed = prim.indexed;
        pending_draw_batch_.base_vertex = prim.base_vertex;
        pending_draw_batch_.vertex_count = prim.vertex_count;
        pending_draw_batch_.vertex_byte_end = vertex_byte_end;
        pending_draw_batch_.first_index = prim.first_index;
        pending_draw_batch_.index_count = prim.index_count;
        pending_draw_batch_.draw_count = 1;
    }
    if (capture_draws_ && draw_index == capture_readback_draw_) {
        flush_pending_draw_batch();
        renderer_.debug_copy_efb_to_readback();
        capture_readback_recorded_ = true;
        std::cerr << "[efb-dump] captured after draw "
                  << draw_index << '\n';
    }
}

void GxBackend::on_efb_copy(std::uint32_t exec_command) {
    flush_pending_draw_batch();
    const EfbCopyParams params = state_.efb_copy(exec_command);
    if (frame_dependency_event_sink_ != nullptr && params.copy_to_xfb) {
        frame_dependency_event_sink_->xfb_copy(
            params.dest_addr, exec_command);
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
    const bool time_copy =
        frame_microprofile_enabled_ || trace_gx_stalls_enabled();
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
            renderer_.debug_copy_efb_to_readback();
            capture_readback_recorded_ = true;
            std::cerr << "[efb-pre-xfb-capture] frame=" << (frame_index_ + 1u)
                      << " dest=0x" << std::hex << params.dest_addr << std::dec
                      << " src=" << params.src_x << ',' << params.src_y << ' '
                      << params.src_width << 'x' << params.src_height
                      << " stride=" << params.dest_stride << " clear=" << params.clear << '\n';
        }
        XfbTexture* xfb = efb_copies_.acquire_xfb(params, frame_index_);
        if (xfb != nullptr) {
            const bool capture_owned = capture_owned_xfb_requested(frame_index_ + 1u);
            if (capture_owned) {
                renderer_.set_debug_capture_paths(
                    capture_output_path("GALAXY_GX_CAPTURE_PPM", frame_index_ + 1u),
                    capture_output_path("GALAXY_GX_CAPTURE_BACKBUFFER_PPM", frame_index_ + 1u));
                renderer_.debug_copy_efb_to_readback();
            }
            renderer_.copy_efb_to_xfb(params, *xfb);
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
                renderer_.debug_copy_selected_xfb_to_readback(*xfb);
                std::cerr << "[owned-xfb-capture] source-chunk=" << (frame_index_ + 1u)
                    << " serial=" << xfb->copy_serial << " stamp=" << xfb->frame_stamp
                    << " addr=0x" << std::hex << params.dest_addr << std::dec
                    << " src=" << params.src_x << ',' << params.src_y << ' '
                    << params.src_width << 'x' << params.src_height
                    << " dest=" << xfb->width << 'x' << xfb->height
                    << " scope=matched-copy-source-and-destination-not-performance\n";
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
            renderer_.debug_copy_efb_to_readback();
            capture_readback_recorded_ = true;
            std::cerr << "[efb-pre-texture-capture] frame=" << (frame_index_ + 1u)
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

    // GX clear-on-copy: the copy command's clear bit wipes the COPY'S SRC
    // RECT with the latched copy-clear registers after the copy, honoring
    // the PE write masks (Dolphin BPStructs BPMEM_TRIGGER_EFB_COPY:
    // ClearScreen(srcRect) with colorEnable = blendmode.colorupdate,
    // alphaEnable = blendmode.alphaupdate, zEnable = zmode.updateenable).
    // A full-target clear here wiped EFB content later copies in the same
    // frame still needed (SMG performs ~3 copies/frame).
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
    // Keep the required callback authoritative. The optional trace is capped
    // so bring-up can observe the signal without
    // flooding stderr (these fire every GXDrawDone — thousands per boot).
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
    // BP 0x64 (source) was latched by GxState; BP 0x65 (dest) triggered this.
    // Without this transfer every palettized (C4/C8/C14X2) texture decodes
    // against a zeroed TLUT bank — black/garbage UI and fonts.
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
