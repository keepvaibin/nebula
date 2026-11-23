// SPDX-License-Identifier: GPL-3.0-only
// What both renderers share (renderer_common.h).

#include "galaxy/render/renderer_common.h"
#include "galaxy/render/post_process_settings.h"
#include "galaxy/platform_time.h"

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#include <dxgi.h>
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
    float src_limit_x; // the source's pixels holding the picture (0: all of them)
    float src_limit_y;
};
Texture2D srctex : register(t0);
SamplerState s_lin : register(s0);
struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
float4 main(V v) : SV_Target {
    float2 dims;
    srctex.GetDimensions(dims.x, dims.y);
    // Each sample's footprint inside the picture's part of the source: the clamp sampler's own edge when that is all of it.
    const float2 limit = src_limit_x > 0.0 ? float2(src_limit_x, src_limit_y) : dims;
    const float2 centre = src_xform.xy + v.pos.xy * src_xform.zw;
    // A picture smaller than its source (more than one source pixel a picture pixel): four bilinear taps a quarter of the
    // footprint from its centre, so every source pixel under it counts (an exact box at 2 and 4 to 1), not one sample's
    // four (which aliases). One tap at or above the source's own size.
    const float2 step = abs(src_xform.zw);
    const float2 spread = float2(step.x > 1.0 ? step.x * 0.25 : 0.0, step.y > 1.0 ? step.y * 0.25 : 0.0);
    float4 c;
    if (spread.x > 0.0 || spread.y > 0.0) {
        c = srctex.SampleLevel(s_lin, clamp(centre + float2(-spread.x, -spread.y), 0.5, limit - 0.5) / dims, 0);
        c += srctex.SampleLevel(s_lin, clamp(centre + float2(spread.x, -spread.y), 0.5, limit - 0.5) / dims, 0);
        c += srctex.SampleLevel(s_lin, clamp(centre + float2(-spread.x, spread.y), 0.5, limit - 0.5) / dims, 0);
        c += srctex.SampleLevel(s_lin, clamp(centre + float2(spread.x, spread.y), 0.5, limit - 0.5) / dims, 0);
        c *= 0.25;
    } else {
        c = srctex.SampleLevel(s_lin, clamp(centre, 0.5, limit - 0.5) / dims, 0);
    }
    // A texture's channels are levels; the frame shown keeps light past white for a float picture (HDR output; an 8- or
    // 10-bit one clamps it as it writes).
    c = channels == 4u ? max(c, 0.0) : saturate(c);
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

// The frame shown, one frame pixel to one picture pixel, opaque; light past white kept for a float picture.
static constexpr char kDisplayFilterPs[] = R"HLSL(
cbuffer C : register(b0) {
    float4 src_xform;
    uint channels;
    float src_limit_x;
    float src_limit_y;
};
Texture2D srctex : register(t0);
float4 main(float4 pos : SV_Position) : SV_Target {
    return float4(max(srctex.Load(int3(int2(src_xform.xy) + int2(pos.xy), 0)).rgb, 0.0), 1.0);
}
)HLSL";

// HDR output's 2D layer back into the float scene (layer_merge_shader_source): the pixels the HUD and menus drew, exactly
// (the layer started as the scene in its own format, so an untouched pixel is bit for bit the start).
static constexpr char kLayerMergePs[] = R"HLSL(
Texture2D drawn : register(t0);
Texture2D start : register(t1);
float4 main(float4 pos : SV_Position) : SV_Target {
    const int3 p = int3(int2(pos.xy), 0);
    const float4 c = drawn.Load(p);
    if (all(c == start.Load(p))) {
        discard;
    }
    return c;
}
)HLSL";

// The constants watch (d3d11::ConstantsWatch, P-332): the three words of the piece bound at b0, as this dispatch reads
// them, into u0.
static constexpr char kConstantsWatchCs[] = R"HLSL(
cbuffer C : register(b0) {
    uint3 words;
};
RWStructuredBuffer<uint> seen : register(u0);
[numthreads(1, 1, 1)]
void main() {
    seen[0] = words.x;
    seen[1] = words.y;
    seen[2] = words.z;
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
// The scene's depth as a picture (SceneConversionShaderConstants): the nearest source pixel to each picture pixel's centre,
// inside the part of the source that holds the picture. Depth is never blended (a mean of two surfaces is neither).
constexpr char kDepthCopyPs[] = R"HLSL(
cbuffer C : register(b0) {
    float4 src_xform;
    uint channels;
    float src_limit_x;
    float src_limit_y;
};
Texture2D<float> depth : register(t0);
float main(float4 pos : SV_Position) : SV_Target {
    float2 dims;
    depth.GetDimensions(dims.x, dims.y);
    const float2 limit = src_limit_x > 0.0 ? float2(src_limit_x, src_limit_y) : dims;
    const float2 at = clamp(floor(src_xform.xy + pos.xy * src_xform.zw), 0.0, limit - 1.0);
    return depth.Load(int3(int2(at), 0));
}
)HLSL";

}  // namespace

std::string_view depth_resolve_pixel_shader_source() noexcept {
    return {kDepthResolvePs, sizeof(kDepthResolvePs) - 1u};
}

std::string_view depth_copy_pixel_shader_source() noexcept {
    return {kDepthCopyPs, sizeof(kDepthCopyPs) - 1u};
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

std::string_view layer_merge_shader_source() noexcept {
    return std::string_view{kLayerMergePs, sizeof(kLayerMergePs) - 1u};
}

SceneConversionShaderConstants make_scene_conversion_constants(std::uint8_t channels, float src_x, float src_y,
                                                               float step_x, float step_y, float limit_x,
                                                               float limit_y) noexcept {
    return SceneConversionShaderConstants{{src_x, src_y, step_x, step_y}, channels, limit_x, limit_y};
}

std::string_view constants_watch_shader_source() noexcept {
    return std::string_view{kConstantsWatchCs, sizeof(kConstantsWatchCs) - 1u};
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
    return ((std::clamp(cfg.anisotropic_filtering, 1u, 16u) & 0x1fu) << 8u) |
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
        !std::isfinite(max_fps) ||  // unlimited (render_config.h kUnlimitedFrameRate): no wait
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

#if defined(_WIN32)
std::string dxgi_driver_version(IDXGIAdapter* adapter) {
    LARGE_INTEGER umd{};
    if (adapter == nullptr || FAILED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) {
        return {};
    }
    const auto high = static_cast<unsigned long>(umd.HighPart);
    const auto low = static_cast<unsigned long>(umd.LowPart);
    return std::to_string(high >> 16u) + "." + std::to_string(high & 0xffffu) + "." + std::to_string(low >> 16u) + "." +
           std::to_string(low & 0xffffu);
}
#endif

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

// The scene's words (post_process_settings.h): here beside write_capture_rows, since the shader tools build this
// file without post_process_settings.cpp.
void rgba8_to_ten_bit(const std::uint8_t* rgba, std::uint8_t* words, unsigned width, unsigned height,
                      unsigned pitch) noexcept {
    for (unsigned y = 0u; y < height; ++y) {
        const std::uint8_t* from = rgba + static_cast<std::size_t>(y) * pitch;
        std::uint8_t* to = words + static_cast<std::size_t>(y) * pitch;
        for (unsigned x = 0u; x < width; ++x) {
            const auto wide = [](std::uint8_t v) { return (static_cast<std::uint32_t>(v) * 1023u + 127u) / 255u; };
            const std::uint32_t word = wide(from[x * 4u]) | wide(from[x * 4u + 1u]) << 10u |
                                       wide(from[x * 4u + 2u]) << 20u |
                                       static_cast<std::uint32_t>(from[x * 4u + 3u] >> 6u) << 30u;
            std::memcpy(to + x * 4u, &word, 4u);
        }
    }
}

void ten_bit_to_rgba8(const std::uint8_t* words, std::uint8_t* rgba, unsigned width, unsigned height,
                      unsigned word_pitch, unsigned rgba_pitch) noexcept {
    for (unsigned y = 0u; y < height; ++y) {
        const std::uint8_t* from = words + static_cast<std::size_t>(y) * word_pitch;
        std::uint8_t* to = rgba + static_cast<std::size_t>(y) * rgba_pitch;
        for (unsigned x = 0u; x < width; ++x) {
            std::uint32_t word = 0u;
            std::memcpy(&word, from + x * 4u, 4u);
            const auto narrow = [](std::uint32_t v) {
                return static_cast<std::uint8_t>(((v & 1023u) * 255u + 511u) / 1023u);
            };
            to[x * 4u] = narrow(word);
            to[x * 4u + 1u] = narrow(word >> 10u);
            to[x * 4u + 2u] = narrow(word >> 20u);
            to[x * 4u + 3u] = static_cast<std::uint8_t>((word >> 30u) * 85u);
        }
    }
}

void half4_to_ten_bit(const std::uint8_t* halves, std::uint8_t* words, unsigned width, unsigned height,
                      unsigned half_pitch, unsigned word_pitch) noexcept {
    // IEEE 754 binary16 to float: subnormals, infinities and NaN (NaN reads as 0 below) kept.
    const auto to_float = [](std::uint16_t h) {
        const std::uint32_t sign = static_cast<std::uint32_t>(h & 0x8000u) << 16u;
        std::uint32_t exponent = (h >> 10u) & 31u;
        std::uint32_t mantissa = h & 1023u;
        std::uint32_t bits = 0u;
        if (exponent == 31u) {
            bits = sign | 0x7F800000u | mantissa << 13u;
        } else if (exponent != 0u) {
            bits = sign | (exponent + 112u) << 23u | mantissa << 13u;
        } else if (mantissa != 0u) {
            exponent = 113u;
            while ((mantissa & 1024u) == 0u) {
                mantissa <<= 1u;
                --exponent;
            }
            bits = sign | exponent << 23u | (mantissa & 1023u) << 13u;
        } else {
            bits = sign;
        }
        return std::bit_cast<float>(bits);
    };
    const auto quantise = [](float v, float levels) {
        return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * levels + 0.5f);
    };
    for (unsigned y = 0u; y < height; ++y) {
        const std::uint8_t* from = halves + static_cast<std::size_t>(y) * half_pitch;
        std::uint8_t* to = words + static_cast<std::size_t>(y) * word_pitch;
        for (unsigned x = 0u; x < width; ++x) {
            std::uint16_t h[4];
            std::memcpy(h, from + x * 8u, 8u);
            float v[4];
            for (unsigned c = 0u; c < 4u; ++c) {
                const float f = to_float(h[c]);
                v[c] = f == f ? f : 0.0f;
            }
            const std::uint32_t word = quantise(v[0], 1023.0f) | quantise(v[1], 1023.0f) << 10u |
                                       quantise(v[2], 1023.0f) << 20u | quantise(v[3], 3.0f) << 30u;
            std::memcpy(to + x * 4u, &word, 4u);
        }
    }
}

void write_capture_rows(std::string path, const std::uint8_t* rows, unsigned width, unsigned height, unsigned pitch,
                        CaptureForm form) {
    const std::size_t count = static_cast<std::size_t>(width) * height;
    const unsigned packed = width * 4u;
    std::vector<std::uint8_t> rgba;
    if (form == CaptureForm::Half) {
        // The halves as the 10-bit target's words (the capture's range ends at 1, as the SDR view), then as RGBA8.
        std::vector<std::uint8_t> words(count * 4u);
        half4_to_ten_bit(rows, words.data(), width, height, pitch, packed);
        rgba.resize(count * 4u);
        ten_bit_to_rgba8(words.data(), rgba.data(), width, height, packed, packed);
    } else if (form == CaptureForm::TenBit) {
        rgba.resize(count * 4u);
        ten_bit_to_rgba8(rows, rgba.data(), width, height, pitch, packed);
    }
    if (!rgba.empty()) {
        rows = rgba.data();
        pitch = packed;
    }
    const unsigned red = form == CaptureForm::Bgra8 ? 2u : 0u;
    const unsigned blue = 2u - red;
    std::vector<std::uint8_t> rgb(count * 3u);
    for (unsigned y = 0u; y < height; ++y) {
        const std::uint8_t* in = rows + static_cast<std::size_t>(y) * pitch;
        std::uint8_t* out = rgb.data() + static_cast<std::size_t>(y) * width * 3u;
        for (unsigned x = 0u; x < width; ++x, in += 4, out += 3) {
            out[0] = in[red];
            out[1] = in[1];
            out[2] = in[blue];
        }
    }
    write_ppm_async(std::move(path), std::move(rgb), width, height);
}

void wait_picture_files() {
    std::unique_lock<std::mutex> hold(g_picture_files_lock);
    g_picture_files_done.wait(hold, [] { return g_picture_files_open == 0u; });
}

}  // namespace galaxy::render
