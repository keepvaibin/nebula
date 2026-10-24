// SPDX-License-Identifier: GPL-3.0-only
// The GX TEV program evaluated on the CPU (rt_tev_eval.h).
#include "galaxy/gx/rt_tev_eval.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace galaxy::gx {
namespace {

using Int4 = std::array<int, 4>;

Int4 swizzle(const Int4& v, std::uint32_t codes) {
    return {v[codes & 3u], v[(codes >> 2u) & 3u], v[(codes >> 4u) & 3u], v[(codes >> 6u) & 3u]};
}

Int4 rgba8(std::uint32_t c) {
    return {static_cast<int>(c >> 24u), static_cast<int>((c >> 16u) & 0xFFu), static_cast<int>((c >> 8u) & 0xFFu),
            static_cast<int>(c & 0xFFu)};
}

float half_to_float(std::uint32_t h) {
    const std::uint32_t sign = (h & 0x8000u) << 16u;
    const std::uint32_t exponent = (h >> 10u) & 0x1Fu;
    const std::uint32_t mantissa = h & 0x3FFu;
    if (exponent == 0u) {
        return std::bit_cast<float>(sign) + (sign != 0u ? -1.0f : 1.0f) * static_cast<float>(mantissa) * 5.9604645e-8f;
    }
    if (exponent == 31u) {
        return std::bit_cast<float>(sign | 0x7F800000u | (mantissa << 13u));
    }
    return std::bit_cast<float>(sign | ((exponent + 112u) << 23u) | (mantissa << 13u));
}

Int4 tev_register(const RtTevMaterial& material, unsigned r) {
    const std::uint32_t rg = material.regs[r * 2u];
    const std::uint32_t ba = material.regs[r * 2u + 1u];
    const float v[4] = {half_to_float(rg & 0xFFFFu), half_to_float(rg >> 16u), half_to_float(ba & 0xFFFFu),
                        half_to_float(ba >> 16u)};
    Int4 out{};
    for (unsigned i = 0; i < 4u; ++i) {
        out[i] = static_cast<int>(std::lround(v[i] * 255.0f));
    }
    return out;
}

int fixed_konst(std::uint32_t k) {
    return k < 4u ? static_cast<int>(255u - 32u * k) : static_cast<int>(256u - 32u * k);
}

Int4 konst(const RtTevMaterial& material, std::uint32_t kcsel, std::uint32_t kasel) {
    Int4 out{};
    if (kcsel <= 7u) {
        out[0] = out[1] = out[2] = fixed_konst(kcsel);
    } else if (kcsel >= 12u && kcsel <= 15u) {
        const Int4 k = rgba8(material.konst[kcsel - 12u]);
        out[0] = k[0];
        out[1] = k[1];
        out[2] = k[2];
    } else if (kcsel >= 16u) {
        const int c = rgba8(material.konst[(kcsel - 16u) % 4u])[(kcsel - 16u) / 4u];
        out[0] = out[1] = out[2] = c;
    }
    if (kasel <= 7u) {
        out[3] = fixed_konst(kasel);
    } else if (kasel >= 16u) {
        out[3] = rgba8(material.konst[(kasel - 16u) % 4u])[(kasel - 16u) / 4u];
    }
    return out;
}

std::array<int, 3> color_arg(std::uint32_t sel, const Int4& prev, const Int4& c0, const Int4& c1, const Int4& c2,
                             const Int4& tex, const Int4& ras, const Int4& kon) {
    const auto rgb = [](const Int4& v) { return std::array<int, 3>{v[0], v[1], v[2]}; };
    const auto aaa = [](const Int4& v) { return std::array<int, 3>{v[3], v[3], v[3]}; };
    switch (sel) {
    case 0u: return rgb(prev);
    case 1u: return aaa(prev);
    case 2u: return rgb(c0);
    case 3u: return aaa(c0);
    case 4u: return rgb(c1);
    case 5u: return aaa(c1);
    case 6u: return rgb(c2);
    case 7u: return aaa(c2);
    case 8u: return rgb(tex);
    case 9u: return aaa(tex);
    case 10u: return rgb(ras);
    case 11u: return aaa(ras);
    case 12u: return {255, 255, 255};
    case 13u: return {128, 128, 128};
    case 14u: return rgb(kon);
    default: return {0, 0, 0};
    }
}

int alpha_arg(std::uint32_t sel, const Int4& prev, const Int4& c0, const Int4& c1, const Int4& c2, const Int4& tex,
              const Int4& ras, const Int4& kon) {
    switch (sel) {
    case 0u: return prev[3];
    case 1u: return c0[3];
    case 2u: return c1[3];
    case 3u: return c2[3];
    case 4u: return tex[3];
    case 5u: return ras[3];
    case 6u: return kon[3];
    default: return 0;
    }
}

// One combiner (colour component or alpha) outside compare mode, as the uber pixel shader computes it.
int combine(int a, int b, int c, int d, std::uint32_t bias, bool sub, std::uint32_t scale) {
    a &= 255;
    b &= 255;
    c &= 255;
    const unsigned left = scale == 1u ? 1u : scale == 2u ? 2u : 0u;
    const int lhs = (d + (bias == 1u ? 128 : bias == 2u ? -128 : 0)) * (1 << left);
    int mix = ((a * 256) + (b - a) * (c + (c >> 7))) * (1 << left);
    mix = scale != 3u ? (mix + (sub ? 127 : 128)) >> 8 : mix >> 8;
    int out = sub ? lhs - mix : lhs + mix;
    if (scale == 3u) {
        out >>= 1;
    }
    return out;
}

}  // namespace

std::array<int, 4> rt_tev_output(const RtTevMaterial& material, const RtTevStage* stages, const std::array<int, 4>& texel,
                                 const std::array<int, 4>& ras0, const std::array<int, 4>& ras1) noexcept {
    const std::uint32_t count = std::min<std::uint32_t>(material.header & 0x1Fu, kRtTevMaxStages);
    const std::uint32_t texgens = material.header >> 28u;
    Int4 prev = tev_register(material, 0u);
    Int4 c0 = tev_register(material, 1u);
    Int4 c1 = tev_register(material, 2u);
    Int4 c2 = tev_register(material, 3u);
    for (std::uint32_t s = 0; s < count; ++s) {
        const RtTevStage& stage = stages[s];
        const bool tex_enable = ((stage.order >> 6u) & 1u) != 0u;
        const std::uint32_t ras_channel = (stage.order >> 7u) & 7u;
        const Int4 tex = tex_enable && texgens != 0u ? swizzle(texel, (stage.ksel >> 18u) & 0xFFu)
                         : texgens != 0u             ? Int4{255, 255, 255, 255}
                                                     : Int4{0, 0, 0, 0};
        const Int4 ras = ras_channel == 0u   ? swizzle(ras0, (stage.ksel >> 10u) & 0xFFu)
                         : ras_channel == 1u ? swizzle(ras1, (stage.ksel >> 10u) & 0xFFu)
                                             : Int4{0, 0, 0, 0};
        const Int4 kon = konst(material, stage.ksel & 0x1Fu, (stage.ksel >> 5u) & 0x1Fu);

        const std::uint32_t ce = stage.color_env;
        const std::uint32_t c_bias = (ce >> 16u) & 3u;
        const bool c_sub = ((ce >> 18u) & 1u) != 0u;
        const std::uint32_t c_scale = (ce >> 20u) & 3u;
        const auto ca = color_arg((ce >> 12u) & 0xFu, prev, c0, c1, c2, tex, ras, kon);
        const auto cb = color_arg((ce >> 8u) & 0xFu, prev, c0, c1, c2, tex, ras, kon);
        const auto cc = color_arg((ce >> 4u) & 0xFu, prev, c0, c1, c2, tex, ras, kon);
        const auto cd = color_arg(ce & 0xFu, prev, c0, c1, c2, tex, ras, kon);
        const std::array<int, 3> cmp_a{ca[0] & 255, ca[1] & 255, ca[2] & 255};
        const std::array<int, 3> cmp_b{cb[0] & 255, cb[1] & 255, cb[2] & 255};
        std::array<int, 3> color_out{};
        if (c_bias == 3u) {
            const std::array<int, 3> ci{cc[0] & 255, cc[1] & 255, cc[2] & 255};
            if (c_scale == 3u) {
                for (unsigned i = 0; i < 3u; ++i) {
                    const bool pass = c_sub ? cmp_a[i] == cmp_b[i] : cmp_a[i] > cmp_b[i];
                    color_out[i] = cd[i] + (pass ? ci[i] : 0);
                }
            } else {
                const int av = c_scale == 0u ? cmp_a[0] : c_scale == 1u ? cmp_a[0] + (cmp_a[1] << 8)
                                                                         : cmp_a[0] + (cmp_a[1] << 8) + (cmp_a[2] << 16);
                const int bv = c_scale == 0u ? cmp_b[0] : c_scale == 1u ? cmp_b[0] + (cmp_b[1] << 8)
                                                                         : cmp_b[0] + (cmp_b[1] << 8) + (cmp_b[2] << 16);
                const bool pass = c_sub ? av == bv : av > bv;
                for (unsigned i = 0; i < 3u; ++i) {
                    color_out[i] = cd[i] + (pass ? ci[i] : 0);
                }
            }
        } else {
            for (unsigned i = 0; i < 3u; ++i) {
                color_out[i] = combine(ca[i], cb[i], cc[i], cd[i], c_bias, c_sub, c_scale);
            }
        }
        for (int& v : color_out) {
            v = ((ce >> 19u) & 1u) != 0u ? std::clamp(v, 0, 255) : std::clamp(v, -1024, 1023);
        }

        const std::uint32_t ae = stage.alpha_env;
        const std::uint32_t a_bias = (ae >> 16u) & 3u;
        const bool a_sub = ((ae >> 18u) & 1u) != 0u;
        const std::uint32_t a_scale = (ae >> 20u) & 3u;
        const int aa = alpha_arg((ae >> 13u) & 7u, prev, c0, c1, c2, tex, ras, kon);
        const int ab = alpha_arg((ae >> 10u) & 7u, prev, c0, c1, c2, tex, ras, kon);
        const int ac = alpha_arg((ae >> 7u) & 7u, prev, c0, c1, c2, tex, ras, kon);
        const int ad = alpha_arg((ae >> 4u) & 7u, prev, c0, c1, c2, tex, ras, kon);
        int alpha_out = 0;
        if (a_bias == 3u) {
            bool pass = false;
            if (a_scale == 3u) {
                pass = a_sub ? (aa & 255) == (ab & 255) : (aa & 255) > (ab & 255);
            } else {
                const int av = a_scale == 0u ? cmp_a[0] : a_scale == 1u ? cmp_a[0] + (cmp_a[1] << 8)
                                                                         : cmp_a[0] + (cmp_a[1] << 8) + (cmp_a[2] << 16);
                const int bv = a_scale == 0u ? cmp_b[0] : a_scale == 1u ? cmp_b[0] + (cmp_b[1] << 8)
                                                                         : cmp_b[0] + (cmp_b[1] << 8) + (cmp_b[2] << 16);
                pass = a_sub ? av == bv : av > bv;
            }
            alpha_out = ad + (pass ? (ac & 255) : 0);
        } else {
            alpha_out = combine(aa, ab, ac, ad, a_bias, a_sub, a_scale);
        }
        alpha_out = ((ae >> 19u) & 1u) != 0u ? std::clamp(alpha_out, 0, 255) : std::clamp(alpha_out, -1024, 1023);

        Int4* const color_dest[4] = {&prev, &c0, &c1, &c2};
        Int4& cdst = *color_dest[(ce >> 22u) & 3u];
        cdst[0] = color_out[0];
        cdst[1] = color_out[1];
        cdst[2] = color_out[2];
        (*color_dest[(ae >> 22u) & 3u])[3] = alpha_out;
        if (s + 1u == count) {
            const Int4& last_color = *color_dest[(ce >> 22u) & 3u];
            const Int4& last_alpha = *color_dest[(ae >> 22u) & 3u];
            prev = {last_color[0], last_color[1], last_color[2], last_alpha[3]};
        }
    }
    return {prev[0] & 255, prev[1] & 255, prev[2] & 255, prev[3] & 255};
}

std::uint32_t rt_tev_neutral_light(const RtTevMaterial& material, const RtTevStage* stages, unsigned channel,
                                   bool lit) noexcept {
    // A mid-grey texel (opaque) as the texture. The channel solved: unlit, its grey vertex light v times its light
    // factor; lit, its material (a vertex colour's hue: white) under the grey light v. The other channel as recorded.
    const Int4 texel{128, 128, 128, 255};
    const auto ras_of = [&](unsigned c, int v) {
        const bool from_vertex = (material.header & (1u << (24u + 2u * c))) != 0u;
        if (lit && c == channel) {
            const Int4 base = from_vertex ? Int4{255, 255, 255, 255} : rgba8(material.channel[c * 2u]);
            Int4 out{};
            for (unsigned i = 0; i < 3u; ++i) {
                out[i] = (base[i] * v + 127) / 255;
            }
            out[3] = base[3];
            return out;
        }
        const Int4 light = rgba8(material.channel[c * 2u + 1u]);
        const Int4 base = from_vertex ? Int4{v, v, v, 255} : rgba8(material.channel[c * 2u]);
        Int4 out{};
        for (unsigned i = 0; i < 4u; ++i) {
            out[i] = (base[i] * light[i] + 127) / 255;
        }
        return out;
    };
    const auto shown = [&](int v) {
        const Int4 ras0 = ras_of(0u, channel == 0u ? v : 255);
        const Int4 ras1 = ras_of(1u, channel == 1u ? v : 255);
        const auto out = rt_tev_output(material, stages, texel, ras0, ras1);
        return 0.2126f * static_cast<float>(out[0]) + 0.7152f * static_cast<float>(out[1]) +
               0.0722f * static_cast<float>(out[2]);
    };
    constexpr float kTarget = 128.0f;
    if (shown(255) <= kTarget) {
        return 255u;
    }
    int lo = 0;
    int hi = 255;
    if (shown(0) >= kTarget) {
        return 0u;
    }
    // The program's output rises with the vertex light (a lerp towards the lit colour, a product): bisect.
    while (hi - lo > 1) {
        const int mid = (lo + hi) / 2;
        if (shown(mid) < kTarget) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return static_cast<std::uint32_t>(hi);
}

}  // namespace galaxy::gx
