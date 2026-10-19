// SPDX-License-Identifier: GPL-3.0-only
// The CPU TEV evaluator (rt_tev_eval.h): known programs' outputs, the uber pixel shader's arithmetic, and the de-baking
// neutral light found by bisection.
#include "galaxy/gx/rt_tev_eval.h"

#include <cstdio>

using namespace galaxy::gx;

namespace {

// BP 0xC0 colour env: d, c, b, a selectors, bias, subtract, clamp, scale, destination.
std::uint32_t color_env(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d, std::uint32_t scale,
                        std::uint32_t dest = 0u) {
    return d | (c << 4u) | (b << 8u) | (a << 12u) | (1u << 19u) | (scale << 20u) | (dest << 22u);
}

// BP 0xC1 alpha env with the same layout from bit 4.
std::uint32_t alpha_env(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
    return (d << 4u) | (c << 7u) | (b << 10u) | (a << 13u) | (1u << 19u);
}

constexpr std::uint32_t kZero = 15u, kTexC = 8u, kRasC = 10u, kC0 = 2u, kC1 = 4u, kCPrev = 0u;
constexpr std::uint32_t kAZero = 7u, kATex = 4u;
constexpr std::uint32_t kOrderTex = 1u << 6u;   // texmap 0, texcoord 0, enabled, colour channel 0
constexpr std::uint32_t kIdentitySwizzles = (0xE4u << 10u) | (0xE4u << 18u);   // r g b a for both swaps

RtTevMaterial material(unsigned stages, std::uint32_t c0 = 0u, std::uint32_t c1 = 0u) {
    RtTevMaterial m{};
    m.header = stages | (1u << 24u) | (1u << 28u);   // colour 0 from the vertices, one texture generator
    m.channel[0] = 0xFFFFFFFFu;
    m.channel[1] = 0xFFFFFFFFu;   // unlit: full light
    m.channel[2] = 0xFFFFFFFFu;
    m.channel[3] = 0xFFFFFFFFu;
    const auto half = [](std::uint32_t v8) {
        // v8 / 255 as a half, exactly enough for the evaluator's rounding back to 0-255.
        const float f = static_cast<float>(v8) / 255.0f;
        if (f == 0.0f) return 0u;
        int e = 0;
        float m2 = f;
        while (m2 < 1.0f) { m2 *= 2.0f; --e; }
        while (m2 >= 2.0f) { m2 *= 0.5f; ++e; }
        const std::uint32_t mant = static_cast<std::uint32_t>((m2 - 1.0f) * 1024.0f + 0.5f);
        return (static_cast<std::uint32_t>(e + 15) << 10u) | (mant & 0x3FFu);
    };
    const std::uint32_t c0h = half(c0), c1h = half(c1);
    m.regs[2] = c0h | (c0h << 16u);   // C0 r g
    m.regs[3] = c0h | (half(255u) << 16u);
    m.regs[4] = c1h | (c1h << 16u);   // C1
    m.regs[5] = c1h | (half(255u) << 16u);
    return m;
}

bool check(bool condition, const char* what, int got, int expected) {
    std::printf("%s %s: got %d, expected %d\n", condition ? "PASS" : "FAIL", what, got, expected);
    return condition;
}

}  // namespace

int main() {
    bool ok = true;
    const std::array<int, 4> grey{128, 128, 128, 255};
    const std::array<int, 4> white{255, 255, 255, 255};
    {
        // texture x colour: shows its texture at full light; neutral light 255.
        RtTevMaterial m = material(1u);
        RtTevStage s[1]{};
        s[0].color_env = color_env(kZero, kTexC, kRasC, kZero, 0u);
        s[0].alpha_env = alpha_env(kAZero, kATex, kAZero, kAZero);
        s[0].order = kOrderTex;
        s[0].ksel = kIdentitySwizzles;
        ok &= check(rt_tev_output(m, s, grey, white, white)[0] == 128, "tex x ras at full light", rt_tev_output(m, s, grey, white, white)[0], 128);
        ok &= check(rt_tev_neutral_light(m, s, 0u, false) == 255u, "tex x ras neutral", static_cast<int>(rt_tev_neutral_light(m, s, 0u, false)), 255);
    }
    {
        // texture x colour x 2 (GX's doubled light): neutral at half light.
        RtTevMaterial m = material(1u);
        RtTevStage s[1]{};
        s[0].color_env = color_env(kZero, kTexC, kRasC, kZero, 1u);
        s[0].alpha_env = alpha_env(kAZero, kATex, kAZero, kAZero);
        s[0].order = kOrderTex;
        s[0].ksel = kIdentitySwizzles;
        const int at_full = rt_tev_output(m, s, grey, white, white)[0];
        ok &= check(at_full == 255, "tex x ras x 2 at full light (clamped)", at_full, 255);
        ok &= check(rt_tev_neutral_light(m, s, 0u, false) == 128u, "tex x ras x 2 neutral", static_cast<int>(rt_tev_neutral_light(m, s, 0u, false)), 128);
    }
    {
        // Beach Bowl's map program: clamp(lerp(C1, C0, v) x 2), then clamp(prev x texture x 2): lit colour C0 = 128,
        // shadow colour C1 = 32. Neutral where lerp(C1, C0, v) x 4 = 1 (lerp = 64): v = (64 - 32) / (128 - 32) of 255.
        RtTevMaterial m = material(2u, 128u, 32u);
        RtTevStage s[2]{};
        s[0].color_env = color_env(kC1, kC0, kRasC, kZero, 1u);
        s[0].alpha_env = alpha_env(kAZero, kAZero, kAZero, kAZero);
        s[0].order = kOrderTex;
        s[0].ksel = kIdentitySwizzles;
        s[1].color_env = color_env(kZero, kCPrev, kTexC, kZero, 1u);
        s[1].alpha_env = alpha_env(kAZero, kAZero, kAZero, kAZero);
        s[1].order = kOrderTex;
        s[1].ksel = kIdentitySwizzles;
        const std::uint32_t v = rt_tev_neutral_light(m, s, 0u, false);
        const int at = rt_tev_output(m, s, grey, {static_cast<int>(v), static_cast<int>(v), static_cast<int>(v), 255}, white)[0];
        const int below = rt_tev_output(m, s, grey, {static_cast<int>(v) - 1, static_cast<int>(v) - 1, static_cast<int>(v) - 1, 255}, white)[0];
        ok &= check(v >= 82u && v <= 88u, "Beach Bowl program neutral (about 85)", static_cast<int>(v), 85);
        ok &= check(at >= 128 && below < 128, "Beach Bowl program shows its texture at the neutral light", at, 128);
        const int full = rt_tev_output(m, s, grey, white, white)[0];
        ok &= check(full == 255, "Beach Bowl program at full light (clips)", full, 255);
    }
    std::printf("%s\n", ok ? "ALL PASS" : "FAILURES");
    return ok ? 0 : 1;
}
