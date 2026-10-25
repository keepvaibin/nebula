// SPDX-License-Identifier: GPL-3.0-only
// What both GX renderers share (renderer_common.h).

#include "galaxy/gx/renderer_common.h"
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

namespace galaxy::gx {

// EFB-copy conversion pixel shader. Operation order on hardware: quantize source samples to
// bytes -> integer 3-row filter -> gamma -> optional RGB-to-YUV conversion -> destination-format
// channel mapping. The filter applies to color and packed depth RGB alike; alpha is always the
// unfiltered center sample. The depth buffer is reversed, so guest z24 is reconstructed from
// 1 - sampled depth before filtering its three bytes.
//
// Root constants (b0):
//   src_xform = src origin.xy, src texels per dest pixel .zw (scaled EFB px)
//   modes     = intensity, gamma, is_depth, keep_alpha
//   filt      = integer filter upper/middle/lower (1/64 units), clamp_y min
//   filt2     = clamp_y max, decoded EFB-copy format (15 = XFB), scaled
//               one-logical-row filter offset, EFB-peek reduction width
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
    uint z24 = uint(clamp(
        (1.0 - depth_sample) * 16777216.0, 0.0, 16777215.0));
    return uint4(
        (z24 >> 16) & 255u,
        (z24 >> 8) & 255u,
        z24 & 255u,
        255u);
}
uint4 sample_efb(float2 sample_pos, float2 inv) {
    // A destination texel covering more than 2x2 source texels (a native-size
    // effect copy of a scaled EFB) averages the central half of its
    // footprint. At 1x each EFB pixel is one sample at its centre; when the
    // game minifies a texture 4:1 into the bloom workspace that sample is a
    // bilinear 2x2 of each 4x4 block, and the bloom threshold that follows is
    // tuned to that sparse coverage. The central half reproduces it (a full
    // footprint average dims small glows below the threshold) while still
    // averaging many scaled samples. Bilinear taps cover 2x2 texel groups.
    const float2 step = src_xform.zw;
    float4 tex_sample;
    [branch] if (max(step.x, step.y) > 2.0) {
        const float2 footprint = 0.5 * step;
        const uint2 taps = uint2(clamp(ceil(footprint * 0.5), 1.0, 16.0));
        const float2 origin = sample_pos - 0.5 * footprint;
        const float2 pitch = footprint / float2(taps);
        tex_sample = 0.0;
        [loop] for (uint ty = 0u; ty < taps.y; ++ty) {
            [loop] for (uint tx = 0u; tx < taps.x; ++tx) {
                tex_sample += srctex.SampleLevel(
                    s_lin, (origin + (float2(tx, ty) + 0.5) * pitch) * inv, 0);
            }
        }
        tex_sample /= float(taps.x * taps.y);
    } else {
        tex_sample = srctex.SampleLevel(s_lin, sample_pos * inv, 0);
    }
    if (modes.z != 0.0) {
        return pack_depth(tex_sample.r);
    }
    return uint4(saturate(tex_sample) * 255.0);
}
float4 main(V v) : SV_Target {
    float2 dims;
    srctex.GetDimensions(dims.x, dims.y);
    float2 inv = 1.0 / dims;
    float2 sp = src_xform.xy + v.pos.xy * src_xform.zw;
    uint reduction_width = filt2.w < 0.0 ? 1u : uint(filt2.w + 0.5);
    uint4 texcol_raw;
    if (reduction_width != 0u || filt2.w < 0.0) {
        if (modes.z != 0.0) {
            // Depth peeks are point reads of the physical texel the CPU
            // geometry selected (src_xform.xy); Load avoids a multi-texel
            // blend at scaled EFB sizes.
            texcol_raw = pack_depth(
                srctex.Load(int3(
                    filt2.w < 0.0 ? int2(src_xform.xy + floor(v.pos.xy) * src_xform.zw)
                                 : int2(src_xform.xy), 0)).r);
        } else {
            // Color peeks reduce the complete scaled logical-pixel block; a whole field (filt2.w < 0)
            // reduces each destination pixel's block.
            const bool field = filt2.w < 0.0;
            const uint block = field ? uint(src_xform.z + 0.5) : reduction_width;
            const int2 origin = field ? int2(src_xform.xy + floor(v.pos.xy) * src_xform.zw) : int2(src_xform.xy);
            float4 sum = 0.0;
            [loop] for (uint py = 0u; py < block; ++py) {
                [loop] for (uint px = 0u; px < block; ++px) {
                    sum += srctex.Load(int3(origin + int2(px, py), 0));
                }
            }
            float4 reduced = sum / float(block * block);
            texcol_raw = uint4(round(saturate(reduced) * 255.0));
        }
    } else {
        float ylo = filt.w, yhi = filt2.x;
        float filter_offset = max(filt2.z, 1.0);
        uint3 coefficients = uint3(filt.xyz + 0.5);
        uint4 current_row = sample_efb(
            float2(sp.x, clamp(sp.y, ylo, yhi)), inv);
        uint3 combined_rows = current_row.rgb * coefficients.y;
        [branch] if (coefficients.x != 0u) {
            uint4 prev_row = sample_efb(
                float2(sp.x, clamp(sp.y - filter_offset, ylo, yhi)), inv);
            combined_rows += prev_row.rgb * coefficients.x;
        }
        [branch] if (coefficients.z != 0u) {
            uint4 next_row = sample_efb(
                float2(sp.x, clamp(sp.y + filter_offset, ylo, yhi)), inv);
            combined_rows += next_row.rgb * coefficients.z;
        }
        texcol_raw = uint4(combined_rows >> 6, current_row.a);
        if (coefficients.x + coefficients.y + coefficients.z >= 128u) {
            texcol_raw &= 0x1ffu;
        }
        texcol_raw = min(texcol_raw, uint4(255u, 255u, 255u, 255u));
    }
    if (modes.y != 1.0) {
        texcol_raw = uint4(round(
            pow(abs(float4(texcol_raw) / 255.0),
                float4(1.0 / modes.y, 1.0 / modes.y, 1.0 / modes.y, 1.0)) *
            255.0));
    }
    if (modes.x != 0.0) {
        int3 rgb = int3(texcol_raw.rgb);
        uint3 yuv = uint3(
            66 * rgb.r + 129 * rgb.g + 25 * rgb.b + 4096,
            -38 * rgb.r - 74 * rgb.g + 112 * rgb.b + 32768,
            112 * rgb.r - 94 * rgb.g - 18 * rgb.b + 32768);
        texcol_raw.rgb = (yuv >> 8) + ((yuv >> 7) & 1u);
    }

    // The GPU alias represents the texture after the EFB copy bytes are
    // decoded again, not the unencoded render target: the copy format's
    // channels and precision, so direct aliases match a RAM roundtrip. The
    // 16-bit colour formats keep 8-bit colour: the game's bloom, depth of field
    // and screen captures copy at RGB565, and 5-6 bits band every soft glow,
    // blur and frozen backdrop at a raised resolution.
    uint fmt = uint(filt2.y + 0.5);
    if (fmt == 0u) {             // R4 -> I4
        uint i = (texcol_raw.r & 0xF0u) | (texcol_raw.r >> 4);
        texcol_raw = uint4(i, i, i, i);
    } else if (fmt == 1u || fmt == 8u) { // R8 -> I8
        texcol_raw = texcol_raw.rrrr;
    } else if (fmt == 2u) {      // RA4 -> IA4
        uint i = (texcol_raw.r & 0xF0u) | (texcol_raw.r >> 4);
        uint a = (texcol_raw.a & 0xF0u) | (texcol_raw.a >> 4);
        texcol_raw = uint4(i, i, i, a);
    } else if (fmt == 3u) {      // RA8 -> IA8
        texcol_raw = texcol_raw.rrra;
    } else if (fmt == 4u) {      // RGB565: 8-bit colour kept (below)
        texcol_raw.a = 255u;
    } else if (fmt == 5u) {      // RGB5A3: 8-bit colour kept, the alpha's 3 bits
        if (texcol_raw.a > 224u) {
            texcol_raw.a = 255u;
        } else {
            texcol_raw.a =
                (texcol_raw.a & 0xE0u) | (texcol_raw.a >> 3) |
                (texcol_raw.a >> 6);
        }
    } else if (fmt == 7u) {      // A8 bytes are loaded as I8
        texcol_raw = texcol_raw.aaaa;
    } else if (fmt == 9u) {      // G8 -> I8
        texcol_raw = texcol_raw.gggg;
    } else if (fmt == 10u) {     // B8 -> I8
        texcol_raw = texcol_raw.bbbb;
    } else if (fmt == 11u) {     // RG8 -> IA8
        texcol_raw = texcol_raw.rrrg;
    } else if (fmt == 12u) {     // GB8 -> IA8
        texcol_raw = texcol_raw.gggb;
    } else if (fmt == 15u) {     // XFB: full RGB precision, opaque alpha
        texcol_raw.a = 255u;
    }
    return float4(texcol_raw) / 255.0;
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

}  // namespace

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

std::string_view efb_conversion_shader_source() noexcept {
    return std::string_view{kConvPs, sizeof(kConvPs) - 1u};
}

EfbConversionShaderConstants make_efb_conversion_constants(
    const EfbCopyParams& params,
    bool is_depth_copy,
    bool intensity,
    bool keep_alpha,
    std::uint8_t copy_format,
    float src_x,
    float src_y,
    float step_x,
    float step_y,
    float filter_row_offset,
    unsigned efb_scale,
    unsigned logical_efb_height,
    unsigned reduction_width) noexcept {
    const float scale = static_cast<float>(
        std::clamp(efb_scale, 1u, kMaxEfbScale));
    float fw_up = static_cast<float>(params.filter_upper());
    float fw_mid = static_cast<float>(params.filter_middle());
    float fw_low = static_cast<float>(params.filter_lower());
    if (fw_up == 0.0f && fw_mid == 0.0f && fw_low == 0.0f) {
        // BP 0x53/0x54 not programmed yet (before GXInit): the centre row alone.
        fw_mid = 64.0f;
    }

    const float copy_y0 = static_cast<float>(params.src_y) * scale;
    const float copy_y1 =
        copy_y0 + static_cast<float>(params.src_height) * scale;
    const float full_y1 =
        static_cast<float>(std::max(logical_efb_height, 1u)) * scale;
    const float clamp_y_min =
        (params.clamp_top ? copy_y0 : 0.0f) + 0.5f;
    const float clamp_y_max = std::max(
        clamp_y_min,
        (params.clamp_bottom ? copy_y1 : full_y1) - 0.5f);
    static constexpr float kGammaLut[4] = {1.0f, 1.7f, 2.2f, 2.2f};

    return EfbConversionShaderConstants{{
        src_x, src_y, step_x, step_y,
        intensity ? 1.0f : 0.0f,
        kGammaLut[params.gamma & 3u],
        is_depth_copy ? 1.0f : 0.0f,
        keep_alpha ? 1.0f : 0.0f,
        fw_up, fw_mid, fw_low, clamp_y_min,
        clamp_y_max,
        static_cast<float>(copy_format),
        filter_row_offset,
        static_cast<float>(reduction_width),
    }};
}

EfbConversionShaderConstants make_efb_peek_conversion_constants(
    const EfbPeekSamplingGeometry& sampling,
    EfbPeekKind kind) noexcept {
    const bool is_depth = kind == EfbPeekKind::Depth;
    const float src_x = is_depth
        ? static_cast<float>(sampling.depth_texel_x)
        : sampling.src_x;
    const float src_y = is_depth
        ? static_cast<float>(sampling.depth_texel_y)
        : sampling.src_y;
    return EfbConversionShaderConstants{{
        src_x,
        src_y,
        sampling.step,
        sampling.step,
        0.0f,
        1.0f,
        is_depth ? 1.0f : 0.0f,
        1.0f,
        0.0f,
        64.0f,
        0.0f,
        sampling.center_y,
        sampling.center_y,
        255.0f,
        sampling.step,
        sampling.step,
    }};
}

EfbClearValues make_efb_clear_values(std::uint32_t ar, std::uint32_t gb,
    std::uint32_t z24, std::uint8_t pixel_format) noexcept {
    std::uint32_t r = ar & 255u, g = (gb >> 8u) & 255u;
    std::uint32_t b = gb & 255u, a = (ar >> 8u) & 255u;
    const auto expand6 = [](std::uint32_t v) { return (v & 0xfcu) | (v >> 6u); };
    const auto expand5 = [](std::uint32_t v) { return (v & 0xf8u) | (v >> 5u); };
    if (pixel_format == 1u) {
        r = expand6(r); g = expand6(g); b = expand6(b); a = expand6(a);
    } else {
        if (pixel_format == 2u) { r = expand5(r); g = expand6(g); b = expand5(b); }
        a = 255u;
    }
    z24 &= 0xffffffu;
    if (pixel_format == 2u) z24 = (z24 & 0xffff00u) | (z24 >> 16u);
    return {{{static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
              static_cast<float>(b) / 255.0f, static_cast<float>(a) / 255.0f}},
            1.0f - static_cast<float>(z24) / 16777216.0f};
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

std::uint64_t efb_copy_dest_budget_bytes() {
    return UINT64_C(192) << 20;
}

bool is_effect_copy(unsigned width, unsigned height) noexcept {
    return width <= 320u && height <= 228u;
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

// The pacer yields only for the last 0.2 ms before a target and sleeps before that: yielding through the
// whole wait costs the render thread about twice the CPU time.
constexpr double kPaceSpinMs = 0.2;

#if defined(_WIN32)
void PresentPacer::sleep_before(double remaining_ms) {
    if (!timer_tried_) {
        timer_tried_ = true;
        timer_ = CreateWaitableTimerExW(
            nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    }
    if (timer_ != nullptr && remaining_ms > kPaceSpinMs) {
        // Sleep to kPaceSpinMs before the target (the timer wakes up to a few hundred microseconds late), then
        // yield for the rest: the spin is at most kPaceSpinMs a frame. Due time in 100 ns units, relative.
        LARGE_INTEGER due{};
        due.QuadPart = -std::max<LONGLONG>(1, static_cast<LONGLONG>((remaining_ms - kPaceSpinMs) * 10000.0));
        if (SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) {
            WaitForSingleObject(timer_, INFINITE);
        } else {
            SwitchToThread();
        }
    } else if (timer_ != nullptr) {
        SwitchToThread();
    } else if (remaining_ms >= 2.0) {
        Sleep(static_cast<DWORD>(remaining_ms - 1.0));
    } else {
        SwitchToThread();
    }
}
#else
void PresentPacer::sleep_before(double remaining_ms) {
    if (remaining_ms > kPaceSpinMs) {
        // CLOCK_MONOTONIC sleeps are high resolution on Linux and Android.
        const auto ns = static_cast<long long>((remaining_ms - kPaceSpinMs) * 1e6);
        timespec duration{static_cast<time_t>(ns / 1000000000), static_cast<long>(ns % 1000000000)};
        clock_nanosleep(CLOCK_MONOTONIC, 0, &duration, nullptr);
    } else {
        sched_yield();
    }
}
#endif

// The longest wait for a present's slot at the frame-rate cap.
constexpr double kSmoothPresentMaxWaitUs = 2000.0;

double PresentPacer::pace(unsigned vsync_interval, float max_fps) {
    // Gameplay is paced by the VI scheduler. A bounded render-thread wait
    // smooths the handoff to DXGI when a VI edge arrives a little early; the cap
    // keeps it from throttling the producer or hiding missing XFBs behind
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
    while (now < target && now < wait_limit) {
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

}  // namespace galaxy::gx
