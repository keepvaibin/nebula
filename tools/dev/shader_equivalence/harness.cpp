// Shader equivalence: builds the pixel, vertex and render-state keys, and the generated vertex and pixel sources, for
// 200,000 seeded random drawing states, and writes them to argv[1]: with "sources", each state's two source hashes
// (FNV-1a), else 'P' + key bytes and 'V' + key bytes. Compiled against two source trees (run.ps1), equal "sources"
// outputs prove the trees generate the same shaders for the same states; equal key outputs prove the same keys when the
// key layout is the same in both trees.
#include "galaxy/render/draw_state.h"
#include "galaxy/render/shader_keys.h"
#include "galaxy/render/shader_gen.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <random>

using namespace galaxy::render;

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    std::FILE* f = std::fopen(argv[1], "wb");
    if (f == nullptr) return 3;
    const bool sources = argc > 2 && std::strcmp(argv[2], "sources") == 0;
    const ShaderGenerator gen;
    std::mt19937 rng(20261029u);
    const auto r = [&](unsigned n) { return static_cast<unsigned>(rng() % n); };
    const auto b = [&]() { return r(2) != 0u; };
    // A small domain per field, so equal-looking states are common and canonicalization is exercised.
    for (int i = 0; i < 200000; ++i) {
        NativeDrawState s;
        s.gen.num_texgens = static_cast<std::uint8_t>(r(9));
        s.gen.num_color_chans = static_cast<std::uint8_t>(r(3));
        s.gen.num_tev_stages = static_cast<std::uint8_t>(1u + r(r(2) ? 4u : 16u));
        s.gen.cull = static_cast<CullMode>(r(4));
        s.gen.num_ind_stages = static_cast<std::uint8_t>(r(3) == 0 ? r(5) : 0u);
        s.z = ZMode{b(), static_cast<CompareFunc>(r(8)), b()};
        s.blend = BlendMode{b(), r(4) == 0, b(), b(), b(), static_cast<BlendFactor>(r(8)), static_cast<BlendFactor>(r(8)),
                            r(4) == 0, static_cast<std::uint8_t>(r(16)), b(), static_cast<std::uint8_t>(r(256))};
        s.pe = PeControl{static_cast<std::uint8_t>(r(3)), static_cast<std::uint8_t>(r(2)), b()};
        s.alpha = AlphaCompare{static_cast<std::uint8_t>(r(256)), static_cast<std::uint8_t>(r(256)),
                               static_cast<CompareFunc>(r(8)), static_cast<CompareFunc>(r(8)),
                               static_cast<AlphaTestLogic>(r(4))};
        for (auto& t : s.tev) {
            t.color_a = static_cast<TevColorArg>(r(16));
            t.color_b = static_cast<TevColorArg>(r(16));
            t.color_c = static_cast<TevColorArg>(r(16));
            t.color_d = static_cast<TevColorArg>(r(16));
            t.color_bias = static_cast<TevBias>(r(4));
            t.color_sub = b();
            t.color_clamp = b();
            t.color_scale = static_cast<TevScale>(r(4));
            t.color_dest = static_cast<std::uint8_t>(r(4));
            t.alpha_a = static_cast<TevAlphaArg>(r(8));
            t.alpha_b = static_cast<TevAlphaArg>(r(8));
            t.alpha_c = static_cast<TevAlphaArg>(r(8));
            t.alpha_d = static_cast<TevAlphaArg>(r(8));
            t.alpha_bias = static_cast<TevBias>(r(4));
            t.alpha_sub = b();
            t.alpha_clamp = b();
            t.alpha_scale = static_cast<TevScale>(r(4));
            t.alpha_dest = static_cast<std::uint8_t>(r(4));
            t.ras_swap = static_cast<std::uint8_t>(r(4));
            t.tex_swap = static_cast<std::uint8_t>(r(4));
            t.order = TevOrder{static_cast<std::uint8_t>(r(8)), static_cast<std::uint8_t>(r(8)), b(),
                               static_cast<std::uint8_t>(r(8))};
            t.kcsel = static_cast<std::uint8_t>(r(32));
            t.kasel = static_cast<std::uint8_t>(r(32));
        }
        for (auto& row : s.swap) {
            for (auto& c : row) c = static_cast<std::uint8_t>(r(4));
        }
        s.fog_type = static_cast<FogType>(r(8));
        s.z_texture.op = static_cast<std::uint8_t>(r(3));
        s.z_texture.type = static_cast<std::uint8_t>(r(3));
        for (auto& ind : s.ind) {
            const bool on = r(3) == 0;
            ind = IndirectStage{};
            if (on) {
                ind.stage = static_cast<std::uint8_t>(r(4));
                ind.format = static_cast<std::uint8_t>(r(4));
                ind.bias = static_cast<std::uint8_t>(r(8));
                ind.alpha = static_cast<std::uint8_t>(r(4));
                ind.matrix = static_cast<std::uint8_t>(r(16));
                ind.wrap_s = static_cast<std::uint8_t>(r(8));
                ind.wrap_t = static_cast<std::uint8_t>(r(8));
                ind.lod = b();
                ind.prev = b();
            }
        }
        for (auto& o : s.ind_order) {
            o.map = static_cast<std::uint8_t>(r(8));
            o.coord = static_cast<std::uint8_t>(r(8));
            o.scale_s = static_cast<std::uint8_t>(r(16));
            o.scale_t = static_cast<std::uint8_t>(r(16));
        }
        for (auto& c : s.channel) {
            c = XfChannelCtrl{b(), b(), static_cast<std::uint8_t>(r(4) == 0 ? 0u : r(256)), b(),
                              static_cast<std::uint8_t>(r(3)), static_cast<std::uint8_t>(r(4))};
        }
        for (auto& g : s.texgen) {
            g = XfTexGen{b(), b(), static_cast<TexGenType>(r(4)), static_cast<std::uint8_t>(r(32)),
                         static_cast<std::uint8_t>(r(8)), static_cast<std::uint8_t>(r(8))};
        }
        for (auto& p : s.post_texgen) {
            p = XfPostTexGen{static_cast<std::uint8_t>(r(64)), b()};
        }
        const PixelShaderKey pk = build_pixel_shader_key(s, 1u, r(4) != 0);
        const VertexShaderKey vk = build_vertex_shader_key(s, rng() & 0x1FFu);
        if (sources) {
            // The generated sources' FNV-1a hashes (texts compared without the key bytes).
            const std::string ps = gen.generate_ps(pk);
            const std::string vs = gen.generate_vs(vk);
            const std::uint64_t hashes[2] = {fnv1a64(ps.data(), ps.size()), fnv1a64(vs.data(), vs.size())};
            std::fwrite(hashes, sizeof(hashes), 1, f);
            continue;
        }
        std::fputc('P', f);
        std::fwrite(&pk, sizeof(pk), 1, f);
        std::fputc('V', f);
        std::fwrite(&vk, sizeof(vk), 1, f);
    }
    std::fclose(f);
    return 0;
}
