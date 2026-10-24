// SPDX-License-Identifier: GPL-3.0-only
// classify_rt_surface (rt_surfaces.h) on hand-built GX shader keys.
#include "galaxy/gx/rt_surfaces.h"

#include <cstdint>
#include <iostream>

namespace {

using galaxy::gx::PixelShaderKey;
using galaxy::gx::RtSurfaceClass;
using galaxy::gx::VertexShaderKey;

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

// XF texgen: regular type, `row` the source (0 position, 1 normal, 5 + n texcoord n).
constexpr std::uint32_t texgen(std::uint32_t row) { return row << 7u; }
// XF channel control: lighting with light 0, `attenuation` 1 specular, 3 spot.
constexpr std::uint32_t lit_channel(std::uint32_t attenuation) {
    return (1u << 1u) | (1u << 2u) | (2u << 7u) | (attenuation << 9u);
}
// Baked vertex colour: material from the vertex, no lighting.
constexpr std::uint32_t kVertexColour = 1u;
// TEV colour combiner d + lerp(a, b, c) from TevColorArg values.
constexpr std::uint32_t combiner(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d) {
    return d | (c << 4u) | (b << 8u) | (a << 12u);
}
constexpr std::uint32_t order(std::uint32_t texcoord, bool texture, std::uint32_t ras) {
    return (texcoord << 3u) | (texture ? 1u << 6u : 0u) | (ras << 7u);
}
constexpr std::uint32_t kCPrev = 0u, kC0 = 2u, kTexC = 8u, kRasC = 10u, kOne = 12u, kKonst = 14u, kZero = 15u;

struct Keys {
    VertexShaderKey vs{};
    PixelShaderKey ps{};
};

// Stage 0: texture 0 times the lit colour 0; texgen 0 from texcoord 0.
Keys base() {
    Keys k{};
    k.vs.num_texgens = 2u;
    k.vs.num_chans = 2u;
    k.vs.texgen[0] = texgen(5u);
    k.vs.channel[0] = lit_channel(3u);
    k.vs.channel[2] = lit_channel(3u);
    k.ps.num_tev_stages = 1u;
    k.ps.stages[0].order = order(0u, true, 0u);
    k.ps.stages[0].color_env = combiner(kZero, kTexC, kRasC, kZero);
    return k;
}

// Stage 1 looks up texture 1 through texgen 1 from normals with `combine`.
Keys environment(std::uint32_t combine) {
    Keys k = base();
    k.vs.texgen[1] = texgen(1u);
    k.ps.num_tev_stages = 2u;
    k.ps.stages[1].order = order(1u, true, 0u);
    k.ps.stages[1].color_env = combine;
    return k;
}

}  // namespace

int main() {
    using galaxy::gx::classify_rt_surface;
    using galaxy::gx::kRtSurfaceEnvAdded;
    using galaxy::gx::kRtSurfaceEnvBlended;
    bool passed = true;

    {
        const Keys k = base();
        const auto s = classify_rt_surface(k.vs, k.ps);
        passed &= expect(s.surface_class == RtSurfaceClass::Diffuse && s.features == 0u, "lit texture is diffuse");
    }
    {
        // The game's gold decorations: previous colour + environment map times a konst colour.
        const Keys k = environment(combiner(kZero, kKonst, kTexC, kCPrev));
        const auto s = classify_rt_surface(k.vs, k.ps);
        passed &= expect(s.surface_class == RtSurfaceClass::Metal && s.env_stage == 1u, "environment map is metal");
        passed &= expect((s.features & kRtSurfaceEnvAdded) != 0u && (s.features & kRtSurfaceEnvBlended) == 0u,
                         "konst-scaled environment map added to the previous colour");
    }
    {
        const Keys k = environment(combiner(kZero, kZero, kZero, kTexC));
        const auto s = classify_rt_surface(k.vs, k.ps);
        passed &= expect((s.features & kRtSurfaceEnvAdded) != 0u, "environment map as the added input");
    }
    {
        // Environment map times the previous colour, or a blend from konst to the map.
        Keys k = environment(combiner(kZero, kTexC, kCPrev, kZero));
        auto s = classify_rt_surface(k.vs, k.ps);
        passed &= expect(s.surface_class == RtSurfaceClass::Metal && (s.features & kRtSurfaceEnvBlended) != 0u,
                         "environment map times the colour");
        k = environment(combiner(kKonst, kTexC, kRasC, kC0));
        s = classify_rt_surface(k.vs, k.ps);
        passed &= expect((s.features & kRtSurfaceEnvBlended) != 0u, "blend towards the environment map");
    }
    {
        // An environment texgen whose lookup no combiner reads is not a reflection.
        const Keys k = environment(combiner(kZero, kZero, kZero, kCPrev));
        const auto s = classify_rt_surface(k.vs, k.ps);
        passed &= expect(s.surface_class == RtSurfaceClass::Diffuse, "unread environment lookup");
    }
    {
        // Colour 1 lit with the specular function, added to the base.
        Keys k = base();
        k.vs.channel[1] = lit_channel(1u);
        k.ps.num_tev_stages = 2u;
        k.ps.stages[1].order = order(0u, false, 1u);
        k.ps.stages[1].color_env = combiner(kZero, kZero, kZero, kRasC);
        const auto s = classify_rt_surface(k.vs, k.ps);
        passed &= expect(s.surface_class == RtSurfaceClass::Glossy &&
                             (s.features & galaxy::gx::kRtSurfaceSpecular) != 0u,
                         "specular channel is glossy");
    }
    {
        // A specular channel the TEV never reads changes nothing.
        Keys k = base();
        k.vs.channel[1] = lit_channel(1u);
        const auto s = classify_rt_surface(k.vs, k.ps);
        passed &= expect(s.surface_class == RtSurfaceClass::Diffuse, "unread specular channel");
    }
    {
        // Map geometry: texture times baked vertex colour.
        Keys k = base();
        k.vs.channel[0] = kVertexColour;
        k.vs.channel[2] = kVertexColour;
        const auto s = classify_rt_surface(k.vs, k.ps);
        passed &= expect(s.surface_class == RtSurfaceClass::Unlit &&
                             (s.features & galaxy::gx::kRtSurfaceUnlit) != 0u,
                         "baked vertex colour is unlit");
    }
    {
        Keys k = base();
        k.ps.stages[0].color_env = combiner(kZero, kZero, kZero, kOne);
        const auto s = classify_rt_surface(k.vs, k.ps);
        passed &= expect(s.surface_class == RtSurfaceClass::Unlit, "constant colour is unlit");
    }
    {
        // Water: an environment map distorted by an indirect stage.
        Keys k = environment(combiner(kZero, kTexC, kCPrev, kZero));
        k.ps.num_ind_stages = 1u;
        k.ps.ind_cmd[0][1] = 1u << 9u;
        const auto s = classify_rt_surface(k.vs, k.ps);
        passed &= expect(s.surface_class == RtSurfaceClass::Water &&
                             (s.features & galaxy::gx::kRtSurfaceIndirect) != 0u,
                         "indirect stage is water");
    }

    if (!passed) {
        return 1;
    }
    std::cout << "rt_surfaces tests passed\n";
    return 0;
}
