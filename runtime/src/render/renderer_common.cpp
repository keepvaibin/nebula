// SPDX-License-Identifier: GPL-3.0-only
// What both renderers share (renderer_common.h).

#include "galaxy/render/renderer_common.h"
#include "galaxy/platform_time.h"

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#else
#include <sched.h>
#include <time.h>
#endif

#include <algorithm>
#include <bit>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <mutex>
#include <thread>

namespace galaxy::render {

// The capture shader (captures and the frame shown): samples the frame inside the capture's rectangle and writes the
// channels the picture carries (SceneConversionShaderConstants). Colour stays in floating point; the target's format
// rounds it once.
static constexpr char kConvPs[] = R"HLSL(
cbuffer C : register(b0) {
    float4 src_xform;  // the rectangle's origin in frame pixels, then frame pixels per picture pixel
    uint channels;     // NebulaTargetChannels; 4 the frame shown (not named `layout`: glslang's HLSL reserves it)
};
Texture2D srctex : register(t0);
SamplerState s_lin : register(s0);
struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
float4 main(V v) : SV_Target {
    float2 dims;
    srctex.GetDimensions(dims.x, dims.y);
    float4 c = saturate(srctex.SampleLevel(s_lin, (src_xform.xy + v.pos.xy * src_xform.zw) / dims, 0));
    if (channels == 1u) {          // alpha in every channel
        c = c.aaaa;
    } else if (channels == 2u) {   // red in every channel
        c = c.rrrr;
    } else if (channels >= 3u) {   // colour, opaque
        c.a = 1.0;
    }
    return c;
}
)HLSL";

// The frame shown, one frame pixel to one picture pixel, opaque.
static constexpr char kDisplayFilterPs[] = R"HLSL(
cbuffer C : register(b0) {
    float4 src_xform;
    uint channels;
};
Texture2D srctex : register(t0);
float4 main(float4 pos : SV_Position) : SV_Target {
    return float4(saturate(srctex.Load(int3(int2(src_xform.xy) + int2(pos.xy), 0)).rgb), 1.0);
}
)HLSL";

// Screen probes: each point's depth, 0 near to 1 far (the depth buffer is reversed), at the frame pixel the CPU found
// for it (ScreenProbePixel); a point the game did not ask about (~0u) reads 0.
static constexpr char kScreenProbeCs[] = R"HLSL(
cbuffer C : register(b0) {
    uint count;
};
Texture2D<float> depth : register(t0);
StructuredBuffer<uint2> pixels : register(t1);
RWStructuredBuffer<float> values : register(u0);
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= count) return;
    const uint2 pixel = pixels[id.x];
    values[id.x] = pixel.x == 0xffffffffu ? 0.0 : saturate(1.0 - depth.Load(int3(int2(pixel), 0)));
}
)HLSL";

namespace {

// Fullscreen triangle with UVs, without a vertex buffer.
constexpr char kBlitVs[] =
    "struct V { float4 pos:SV_Position; float2 uv:TEXCOORD0; };"
    "V main(uint id:SV_VertexID){"
    " V o;"
    " o.uv = float2(id==1?2:0, id==2?2:0);"
    " o.pos = float4(o.uv.x*2-1, 1-o.uv.y*2, 0.5, 1);"
    " return o;"
    "}";

constexpr char kBlitPs[] =
    "Texture2D t:register(t0);"
    "SamplerState s:register(s0);"
    "struct V{float4 p:SV_Position;float2 uv:TEXCOORD0;};"
    "float4 main(V v):SV_Target{return t.Sample(s,v.uv);}";

constexpr char kClearPs[] =
    "cbuffer C:register(b0){float4 clear_color;};"
    "float4 main():SV_Target{return clear_color;}";
constexpr char kClearDepthPs[] =
    "cbuffer C:register(b0){float4 clear_value;};"
    "float main():SV_Depth{return clear_value.x;}";
// The depth resolve: each pixel's first sample.
constexpr char kDepthResolvePs[] = R"HLSL(
Texture2DMS<float> depth_samples : register(t0);
float main(float4 pos : SV_Position) : SV_Depth {
    return depth_samples.Load(int2(pos.xy), 0);
}
)HLSL";

}  // namespace

std::string_view depth_resolve_pixel_shader_source() noexcept {
    return {kDepthResolvePs, sizeof(kDepthResolvePs) - 1u};
}


std::string_view blit_vertex_shader_source() noexcept {
    return {kBlitVs, sizeof(kBlitVs) - 1u};
}

std::string_view blit_pixel_shader_source() noexcept {
    return {kBlitPs, sizeof(kBlitPs) - 1u};
}

std::string_view clear_pixel_shader_source() noexcept {
    return {kClearPs, sizeof(kClearPs) - 1u};
}

std::string_view clear_depth_pixel_shader_source() noexcept {
    return {kClearDepthPs, sizeof(kClearDepthPs) - 1u};
}

std::string_view scene_conversion_shader_source() noexcept {
    return std::string_view{kConvPs, sizeof(kConvPs) - 1u};
}

std::string_view display_filter_shader_source() noexcept {
    return std::string_view{kDisplayFilterPs, sizeof(kDisplayFilterPs) - 1u};
}

std::string_view screen_probe_shader_source() noexcept {
    return std::string_view{kScreenProbeCs, sizeof(kScreenProbeCs) - 1u};
}

SceneConversionShaderConstants make_scene_conversion_constants(std::uint8_t channels, float src_x, float src_y,
                                                               float step_x, float step_y) noexcept {
    return SceneConversionShaderConstants{{src_x, src_y, step_x, step_y}, channels};
}

ScreenProbePixel screen_probe_pixel(const ScreenProbe& probe, const FrameMap& map) noexcept {
    // Not a number fails both tests too.
    if (!(probe.x >= 0.0f) || !(probe.y >= 0.0f) || map.empty()) return {~0u, ~0u};
    const auto pixel = [](float at, float scale, unsigned size) {
        return static_cast<std::uint32_t>(std::min(at * scale, static_cast<float>(size - 1u)));
    };
    return {pixel(probe.x, map.sx, map.width), pixel(probe.y, map.sy, map.height)};
}

SceneClearValues make_scene_clear_values(std::uint32_t rgba, std::uint8_t frame_format) noexcept {
    const std::uint32_t r = rgba >> 24u, g = (rgba >> 16u) & 255u;
    const std::uint32_t b = (rgba >> 8u) & 255u;
    const std::uint32_t a = (frame_format & 1u) != 0u ? rgba & 255u : 255u;
    return {{{static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
              static_cast<float>(b) / 255.0f, static_cast<float>(a) / 255.0f}},
            0.0f};
}

namespace {
// FNV-1a over a sampler's words and the settings it depends on.
struct SamplerHash {
    std::uint64_t hash = 0xCBF29CE484222325ull;
    void mix(std::uint32_t value) noexcept {
        for (unsigned byte = 0u; byte < 4u; ++byte) {
            hash ^= (value >> (byte * 8u)) & 0xffu;
            hash *= 0x100000001B3ull;
        }
    }
    void sampler(const SamplerKey& key) noexcept {
        const auto bits = [](float f) { return f == 0.0f ? 0u : std::bit_cast<std::uint32_t>(f); };
        mix(key.filter);
        mix(bits(key.min_lod));
        mix(bits(key.max_lod));
        mix(bits(key.lod_bias));
    }
};
}  // namespace

std::size_t SamplerSlotKeyHasher::operator()(const SamplerSlotKey& key) const noexcept {
    SamplerHash h;
    h.sampler(key.sampler);
    h.mix(key.config);
    return static_cast<std::size_t>(h.hash);
}

std::size_t SamplerTableKeyHasher::operator()(const SamplerTableKey& key) const noexcept {
    SamplerHash h;
    for (const SamplerKey& sampler : key.samplers) {
        h.sampler(sampler);
    }
    h.mix(static_cast<std::uint32_t>(key.samplers.size()));
    h.mix(key.config);
    return static_cast<std::size_t>(h.hash);
}

std::uint32_t sampler_config_key(const RenderConfig& cfg) noexcept {
    return (cfg.enhanced_mipmaps ? 1u << 16u : 0u) | ((std::clamp(cfg.anisotropic_filtering, 1u, 16u) & 0x1fu) << 8u) |
        (std::clamp(cfg.texture_lod_bias, 0u, 8u) & 0xfu);
}

SamplerSlotKey make_sampler_slot_key(const SamplerKey& sampler, const RenderConfig& cfg) noexcept {
    return SamplerSlotKey{sampler, sampler_config_key(cfg)};
}

SamplerTableKey make_sampler_table_key(const SamplerKey* keys, unsigned count, const RenderConfig& cfg) {
    SamplerTableKey key;
    key.samplers.assign(keys, keys + count);
    key.config = sampler_config_key(cfg);
    return key;
}

SamplerSlotCache::Allocation SamplerSlotCache::get_or_allocate(const SamplerSlotKey& key) {
    if (const auto it = slots_.find(key); it != slots_.end()) {
        return {it->second, false};
    }
    const unsigned index = cursor_++;
    slots_.emplace(key, index);
    return {index, true};
}

PresentPacer::PresentPacer() noexcept : frequency_(galaxy::qpc_frequency()) {}

PresentPacer::~PresentPacer() {
    reset();
}

void PresentPacer::reset() noexcept {
    last_qpc_ = 0;
#if defined(_WIN32)
    if (timer_ != nullptr) {
        CloseHandle(timer_);
        timer_ = nullptr;
    }
#endif
    timer_tried_ = false;
}

#if defined(_WIN32)
void PresentPacer::sleep_before(double remaining_ms) {
    if (!timer_tried_) {
        timer_tried_ = true;
        timer_ = CreateWaitableTimerExW(
            nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    }
    // Due time in 100 ns units, relative. The next target counts from this one, so a late wake does not move the rate.
    LARGE_INTEGER due{};
    due.QuadPart = -std::max<LONGLONG>(1, static_cast<LONGLONG>(remaining_ms * 10000.0));
    if (timer_ != nullptr && SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) {
        WaitForSingleObject(timer_, INFINITE);
    } else {
        Sleep(static_cast<DWORD>(std::ceil(remaining_ms)));
    }
}
#else
void PresentPacer::sleep_before(double remaining_ms) {
    // CLOCK_MONOTONIC sleeps are high resolution on Linux and Android.
    const auto ns = static_cast<long long>(remaining_ms * 1e6);
    timespec duration{static_cast<time_t>(ns / 1000000000), static_cast<long>(ns % 1000000000)};
#if defined(__APPLE__)
    // Darwin has no clock_nanosleep; nanosleep there is a Mach wait of the same resolution.
    nanosleep(&duration, nullptr);
#else
    clock_nanosleep(CLOCK_MONOTONIC, 0, &duration, nullptr);
#endif
}
#endif

// The longest wait for a present's slot at the frame-rate cap.
constexpr double kSmoothPresentMaxWaitUs = 2000.0;

double PresentPacer::pace(unsigned vsync_interval, float max_fps) {
    // Gameplay is paced by the frame clock. A bounded render-thread wait
    // smooths the handoff to DXGI when a frame edge arrives a little early; the cap
    // keeps it from throttling the producer or hiding missing pictures behind
    // duplicated presents.
    if (vsync_interval != 0u ||
        max_fps <= 0.0f ||
        frequency_ <= 0) {
        last_qpc_ = 0;
        return 0.0;
    }

    std::int64_t now = galaxy::qpc_now();
    if (last_qpc_ == 0) {
        last_qpc_ = now;
        return 0.0;
    }

    const auto wait_start_qpc = now;
    const double ticks_per_frame =
        static_cast<double>(frequency_) /
        static_cast<double>(max_fps);
    const auto target =
        last_qpc_ +
        static_cast<std::int64_t>(ticks_per_frame + 0.5);
    const std::int64_t wait_limit =
        now + static_cast<std::int64_t>(
                  (static_cast<double>(frequency_) * kSmoothPresentMaxWaitUs) /
                  1'000'000.0);
    if (now < target && now < wait_limit) {
        const double remaining_ms =
            static_cast<double>(std::min<std::int64_t>(target, wait_limit) - now) * 1000.0 /
            static_cast<double>(frequency_);
        sleep_before(remaining_ms);
        now = galaxy::qpc_now();
    }

    const double wait_ms =
        static_cast<double>(now - wait_start_qpc) * 1000.0 /
        static_cast<double>(frequency_);

    const auto max_lag =
        static_cast<std::int64_t>(ticks_per_frame * 2.0);
    last_qpc_ =
        now < target
            ? now
            : ((now - target > max_lag) ? now : target);
    return wait_ms;
}

namespace {

// The PPM writes asked and not yet finished (write_ppm_async, wait_picture_files).
std::mutex g_picture_files_lock;
std::condition_variable g_picture_files_done;
std::size_t g_picture_files_open = 0u;

}  // namespace

void write_ppm_async(std::string path, std::vector<std::uint8_t> rgb, unsigned width, unsigned height) {
    {
        const std::lock_guard<std::mutex> hold(g_picture_files_lock);
        ++g_picture_files_open;
    }
    std::thread([path = std::move(path), rgb = std::move(rgb), width, height] {
        {
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            out << "P6\n" << width << ' ' << height << "\n255\n";
            out.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
        }
        const std::lock_guard<std::mutex> hold(g_picture_files_lock);
        --g_picture_files_open;
        g_picture_files_done.notify_all();
    }).detach();
}

void wait_picture_files() {
    std::unique_lock<std::mutex> hold(g_picture_files_lock);
    g_picture_files_done.wait(hold, [] { return g_picture_files_open == 0u; });
}

}  // namespace galaxy::render
