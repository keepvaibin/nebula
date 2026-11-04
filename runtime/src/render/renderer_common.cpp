// SPDX-License-Identifier: GPL-3.0-only
// What both renderers share (renderer_common.h).

#include "galaxy/render/renderer_common.h"
#include "galaxy/platform_time.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <sched.h>
#include <time.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>

namespace galaxy::render {

// The capture shader (captures, the frame shown, screen probes). A capture samples the frame inside its rectangle, through
// the display's vertical filter when the game asked for it (the line above and the line, weighted), takes the colour's
// luminance or the depth when asked, and writes the channels the picture carries. Colour stays in floating point; the
// target's format rounds it once. A screen probe reads one pixel (depth as its 24-bit value, in three bytes).
//
// Root constants (b0):
//   src_xform = src origin.xy, src texels per dest pixel .zw (scaled frame px)
//   modes     = luminance, unused, depth, keep_alpha
//   filt      = filter weights above/line/below (64ths), clamp_y min
//   filt2     = clamp_y max, picture channels (7 the frame shown), the scaled one-logical-row filter offset, 1 a probe
static constexpr char kConvPs[] = R"HLSL(
cbuffer C : register(b0) {
    float4 src_xform;
    float4 modes;
    float4 filt;
    float4 filt2;
};
Texture2D srctex : register(t0);
SamplerState s_lin : register(s0);
SamplerState s_pt  : register(s1);
struct V { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
uint4 pack_depth(float depth_sample) {
    uint depth24 = uint(clamp(
        (1.0 - depth_sample) * 16777216.0, 0.0, 16777215.0));
    return uint4(
        (depth24 >> 16) & 255u,
        (depth24 >> 8) & 255u,
        depth24 & 255u,
        255u);
}
float4 main(V v) : SV_Target {
    float2 dims;
    srctex.GetDimensions(dims.x, dims.y);
    float4 c;
    if (filt2.w > 0.0) {
        // A screen probe: the pixel src_xform.xy names.
        const float4 texel = srctex.Load(int3(int2(src_xform.xy), 0));
        c = modes.z != 0.0 ? float4(pack_depth(texel.r)) / 255.0 : saturate(texel);
    } else {
        const float2 at = (src_xform.xy + v.pos.xy * src_xform.zw) / dims;
        c = srctex.SampleLevel(s_lin, at, 0);
        // The depth buffer is reversed: the game's depth is one minus it.
        c = modes.z != 0.0 ? float4(saturate(1.0 - c.rrr), 1.0) : saturate(c);
    }
    // The picture's channels (capture.h): the game's NebulaTargetChannels, 7 the frame shown. (Not named `layout`:
    // glslang's HLSL front end reserves the word.)
    uint channels = uint(filt2.y + 0.5);
    if (channels == 3u) {          // red in every channel
        c = c.rrrr;
    } else if (channels == 6u || channels == 7u) { // colour, opaque
        c.a = 1.0;
    } else if (channels == 2u) {   // alpha in every channel
        c = c.aaaa;
    }
    return c;
}
)HLSL";

// The frame shown, one frame pixel to one picture pixel, opaque.
static constexpr char kDisplayFilterPs[] = R"HLSL(
cbuffer C : register(b0) {
    float4 src_xform;
    float4 modes;
    float4 filt;
    float4 filt2;
};
Texture2D srctex : register(t0);
float4 main(float4 pos : SV_Position) : SV_Target {
    return float4(saturate(srctex.Load(int3(int2(src_xform.xy) + int2(pos.xy), 0)).rgb), 1.0);
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

// Native settings overlay corner decoration: one small textured quad in
// screen-pixel coordinates. Root constants (b0, all-stage-visible):
//   rect      = x, y, w, h in backbuffer pixels
//   uv_minmax = u0, v0, u1, v1 (lets one square corner texture be mirrored
//               per panel corner instead of needing four textures)
//   tint      = RGBA multiplier (alpha also modulates the overlay's own
//               translucency)
//   screen    = backbuffer width, height (for the pixel->NDC divide)
constexpr char kUiTexturedQuadVs[] = R"HLSL(
cbuffer C : register(b0) {
    float4 rect;
    float4 uv_minmax;
    float4 tint;
    float2 screen;
};
struct V { float4 pos:SV_Position; float2 uv:TEXCOORD0; };
V main(uint id : SV_VertexID) {
    V o;
    float2 corner = float2(float(id & 1u), float((id >> 1u) & 1u));
    float2 px = rect.xy + corner * rect.zw;
    float2 ndc = float2(px.x / screen.x * 2.0 - 1.0, 1.0 - px.y / screen.y * 2.0);
    o.pos = float4(ndc, 0.5, 1.0);
    o.uv = lerp(uv_minmax.xy, uv_minmax.zw, corner);
    return o;
}
)HLSL";

constexpr char kUiTexturedQuadPs[] = R"HLSL(
cbuffer C : register(b0) {
    float4 rect;
    float4 uv_minmax;
    float4 tint;
    float2 screen;
};
Texture2D t : register(t0);
SamplerState s : register(s0);
struct V { float4 p:SV_Position; float2 uv:TEXCOORD0; };
float4 main(V v) : SV_Target { return t.Sample(s, v.uv) * tint; }
)HLSL";

// Flat-coloured quad: the settings overlay's panel background and text glyphs
// (ensure_native_settings_solid_pipeline_loaded).
constexpr char kUiSolidQuadVs[] = R"HLSL(
cbuffer C : register(b0) {
    int4 rect;
    float4 color;
    uint2 screen;
    uint2 glyph_mask;
    uint glyph_mode;
};
struct V { float4 pos:SV_Position; };
V main(uint id : SV_VertexID, uint cell : SV_InstanceID) {
    V o;
    float2 corner = float2(float(id & 1u), float((id >> 1u) & 1u));
    float2 px = float2(rect.xy) + corner * float2(rect.zw);
    if (glyph_mode != 0u) {
        uint mask_word = cell < 32u ? glyph_mask.x : glyph_mask.y;
        uint bit = (mask_word >> (cell & 31u)) & 1u;
        // Cells are placed and clipped in integer pixels before the NDC conversion.
        int2 cell_min = rect.xy + int2(cell % 5u, cell / 5u) * rect.zw;
        int2 cell_max = clamp(cell_min + rect.zw, int2(0, 0), int2(screen));
        cell_min = clamp(cell_min, int2(0, 0), int2(screen));
        px = float2(cell_min) + corner * float2(cell_max - cell_min) * float(bit);
    }
    float2 ndc = float2(px.x / screen.x * 2.0 - 1.0, 1.0 - px.y / screen.y * 2.0);
    o.pos = float4(ndc, 0.5, 1.0);
    return o;
}
)HLSL";

constexpr char kUiSolidQuadPs[] = R"HLSL(
cbuffer C : register(b0) {
    int4 rect;
    float4 color;
    uint2 screen;
    uint2 glyph_mask;
    uint glyph_mode;
};
float4 main() : SV_Target { return color; }
)HLSL";


constexpr char kClearPs[] =
    "cbuffer C:register(b0){float4 clear_color;};"
    "float4 main():SV_Target{return clear_color;}";
constexpr char kDepthCarryLoadPs[] =
    "Texture2D t:register(t0);SamplerState s:register(s0);"
    "struct V{float4 p:SV_Position;float2 uv:TEXCOORD0;};"
    "float main(V v):SV_Depth{float2 d;t.GetDimensions(d.x,d.y);"
    "return t.Load(int3(min(int2(v.uv*d),int2(d)-1),0)).r;}";
constexpr char kClearDepthPs[] =
    "cbuffer C:register(b0){float4 clear_value;};"
    "float main():SV_Depth{return clear_value.x;}";
constexpr char kDepthCarryPs[] =
    "Texture2D<float> t:register(t0);"
    "SamplerState s:register(s0);"
    "struct V{float4 p:SV_Position;float2 uv:TEXCOORD0;};"
    "float main(V v):SV_Depth{return t.SampleLevel(s,v.uv,0);}";

// The depth resolve: each pixel's first sample.
constexpr char kDepthResolvePs[] = R"HLSL(
Texture2DMS<float> depth_samples : register(t0);
float main(float4 pos : SV_Position) : SV_Depth {
    return depth_samples.Load(int2(pos.xy), 0);
}
)HLSL";

constexpr char kMotionDebugPs[] =
    "Texture2D<float2> mv:register(t0);"
    "struct V{float4 p:SV_Position;float2 uv:TEXCOORD0;};"
    "float4 main(V v):SV_Target{"
    " float2 m=mv.Load(int3(int2(v.p.xy),0));"
    " return float4(saturate(0.5+m.x/16.0),saturate(0.5+m.y/16.0),0.5,1.0);}";

}  // namespace

std::string_view depth_resolve_pixel_shader_source() noexcept {
    return {kDepthResolvePs, sizeof(kDepthResolvePs) - 1u};
}

std::string_view motion_debug_pixel_shader_source() noexcept {
    return {kMotionDebugPs, sizeof(kMotionDebugPs) - 1u};
}

std::string_view blit_vertex_shader_source() noexcept {
    return {kBlitVs, sizeof(kBlitVs) - 1u};
}

std::string_view blit_pixel_shader_source() noexcept {
    return {kBlitPs, sizeof(kBlitPs) - 1u};
}

std::string_view ui_textured_quad_vertex_shader_source() noexcept {
    return {kUiTexturedQuadVs, sizeof(kUiTexturedQuadVs) - 1u};
}

std::string_view ui_textured_quad_pixel_shader_source() noexcept {
    return {kUiTexturedQuadPs, sizeof(kUiTexturedQuadPs) - 1u};
}

std::string_view ui_solid_quad_vertex_shader_source() noexcept {
    return {kUiSolidQuadVs, sizeof(kUiSolidQuadVs) - 1u};
}

std::string_view ui_solid_quad_pixel_shader_source() noexcept {
    return {kUiSolidQuadPs, sizeof(kUiSolidQuadPs) - 1u};
}

std::string_view clear_pixel_shader_source() noexcept {
    return {kClearPs, sizeof(kClearPs) - 1u};
}

std::string_view depth_carry_load_pixel_shader_source() noexcept {
    return {kDepthCarryLoadPs, sizeof(kDepthCarryLoadPs) - 1u};
}

std::string_view clear_depth_pixel_shader_source() noexcept {
    return {kClearDepthPs, sizeof(kClearDepthPs) - 1u};
}

std::string_view depth_carry_pixel_shader_source() noexcept {
    return {kDepthCarryPs, sizeof(kDepthCarryPs) - 1u};
}

std::string_view scene_conversion_shader_source() noexcept {
    return std::string_view{kConvPs, sizeof(kConvPs) - 1u};
}

std::string_view display_filter_shader_source() noexcept {
    return std::string_view{kDisplayFilterPs, sizeof(kDisplayFilterPs) - 1u};
}

SceneConversionShaderConstants make_scene_conversion_constants(
    bool from_depth,
    std::uint8_t channels,
    float src_x,
    float src_y,
    float step_x,
    float step_y) noexcept {
    return SceneConversionShaderConstants{{
        src_x, src_y, step_x, step_y,
        0.0f, 1.0f, from_depth ? 1.0f : 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, static_cast<float>(channels), 0.0f, 0.0f,
    }};
}

SceneConversionShaderConstants make_screen_probe_constants(float x, float y, const FrameMap& map, bool depth) noexcept {
    const auto pixel = [](float at, float scale, unsigned size) {
        return static_cast<float>(std::min(static_cast<unsigned>(at * scale), size - 1u));
    };
    const float px = pixel(x, map.sx, map.width);
    const float py = pixel(y, map.sy, map.height);
    return SceneConversionShaderConstants{{
        px, py, 1.0f, 1.0f,
        0.0f, 1.0f, depth ? 1.0f : 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 255.0f, 0.0f, 1.0f,
    }};
}

SceneClearValues make_scene_clear_values(std::uint32_t rgba, std::uint8_t frame_format) noexcept {
    const std::uint32_t r = rgba >> 24u, g = (rgba >> 16u) & 255u;
    const std::uint32_t b = (rgba >> 8u) & 255u;
    const std::uint32_t a = (frame_format & 1u) != 0u ? rgba & 255u : 255u;
    return {{{static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
              static_cast<float>(b) / 255.0f, static_cast<float>(a) / 255.0f}},
            0.0f};
}

std::size_t SamplerTableKeyHasher::operator()(const SamplerTableKey& key) const noexcept {
    std::uint64_t hash = 0xCBF29CE484222325ull;
    const auto mix = [&hash](std::uint32_t value) {
        for (unsigned byte = 0u; byte < 4u; ++byte) {
            hash ^= (value >> (byte * 8u)) & 0xffu;
            hash *= 0x100000001B3ull;
        }
    };
    for (const auto sampler : key.samplers) mix(sampler);
    mix(key.config);
    return static_cast<std::size_t>(hash);
}

std::uint32_t sampler_config_key(const RenderConfig& cfg) noexcept {
    return ((std::clamp(cfg.anisotropic_filtering, 1u, 16u) & 0x1fu) << 8u) |
        (std::clamp(cfg.texture_lod_bias, 0u, 8u) & 0xfu);
}

SamplerTableKey make_sampler_table_key(const std::uint32_t keys[8], const RenderConfig& cfg) noexcept {
    SamplerTableKey key{};
    std::copy_n(keys, key.samplers.size(), key.samplers.begin());
    key.config = sampler_config_key(cfg);
    return key;
}

SamplerTableCache::Allocation SamplerTableCache::get_or_allocate(const SamplerTableKey& key) {
    if (const auto it = tables_.find(key); it != tables_.end()) {
        slot_frames_[(it->second - 8u) / 8u] = frame_;
        return {it->second, false};
    }
    const bool fresh_slot = cursor_ + 8u <= kSamplerHeapSlots;
    unsigned slot = fresh_slot ? (cursor_ - 8u) / 8u : eviction_cursor_;
    if (!fresh_slot) {
        unsigned checked = 0u;
        while (checked < kDynamicTables && slot_frames_[slot] == frame_) {
            slot = (slot + 1u) % kDynamicTables;
            ++checked;
        }
        if (checked == kDynamicTables) {
            throw std::runtime_error(
                "[Renderer] sampler heap exhausted: more than 255 distinct "
                "sampler configurations used within one frame");
        }
    }
    const unsigned index = 8u + slot * 8u;
    // Insert before retiring the old mapping: an allocation failure leaves
    // all resident descriptors discoverable. Do not hold a victim iterator
    // across insertion, which may rehash the map.
    const auto [it, inserted] = tables_.emplace(key, index);
    if (inserted) {
        if (fresh_slot) cursor_ += 8u;
        else {
            tables_.erase(slot_keys_[slot]);
            eviction_cursor_ = (slot + 1u) % kDynamicTables;
        }
        slot_keys_[slot] = key;
        slot_frames_[slot] = frame_;
    }
    return {it->second, inserted};
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
    clock_nanosleep(CLOCK_MONOTONIC, 0, &duration, nullptr);
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

}  // namespace galaxy::render
