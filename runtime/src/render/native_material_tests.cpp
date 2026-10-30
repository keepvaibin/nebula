// Native materials: a program applied once and then only its values gives the drawing state the whole material gives
// (RenderCore applies a repeated program's values only, NativeDrawState::apply's `whole` false).

#include "galaxy/render/draw_state.h"
#include "galaxy/render/native_material.h"
#include "galaxy/render/shader_keys.h"

#include <cstdio>
#include <cstring>
#include <random>

namespace {

using namespace galaxy::render;

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++g_failures;
        std::printf("FAILED: %s\n", what);
    }
}

struct Random {
    std::mt19937 rng{20261030u};
    unsigned operator()(unsigned n) { return static_cast<unsigned>(rng() % n); }
    float real(float lo, float hi) { return lo + (hi - lo) * static_cast<float>(rng() % 100000u) / 100000.0f; }
};

material::Program random_program(Random& r) {
    material::Program p{};
    p.tev_stages = static_cast<std::uint8_t>(1u + r(16u));
    p.tex_gens = static_cast<std::uint8_t>(r(9u));
    p.channels = static_cast<std::uint8_t>(r(3u));
    p.ind_stages = static_cast<std::uint8_t>(r(4u));
    p.cull = static_cast<std::uint8_t>(r(4u));
    p.dither = static_cast<std::uint8_t>(r(2u));
    p.z_compare_location = static_cast<std::uint8_t>(r(2u));
    p.origin = 1u;
    p.loads = static_cast<std::uint16_t>(r(1u << 11u));
    p.diff_flags = r(4u) == 0u ? static_cast<std::uint32_t>(r.rng()) : 0u;
    for (auto& s : p.tev) {
        s.tex_coord = static_cast<std::uint8_t>(r(9u));
        s.tex_map = static_cast<std::uint8_t>(r(9u));
        s.channel = static_cast<std::uint8_t>(r(8u));
        for (auto& in : s.color_in) in = static_cast<std::uint8_t>(r(16u));
        for (auto& in : s.alpha_in) in = static_cast<std::uint8_t>(r(8u));
        s.color_op = static_cast<std::uint8_t>(r(16u));
        s.color_bias = static_cast<std::uint8_t>(r(4u));
        s.color_scale = static_cast<std::uint8_t>(r(4u));
        s.color_clamp = static_cast<std::uint8_t>(r(2u));
        s.color_reg = static_cast<std::uint8_t>(r(4u));
        s.alpha_op = static_cast<std::uint8_t>(r(16u));
        s.alpha_bias = static_cast<std::uint8_t>(r(4u));
        s.alpha_scale = static_cast<std::uint8_t>(r(4u));
        s.alpha_clamp = static_cast<std::uint8_t>(r(2u));
        s.alpha_reg = static_cast<std::uint8_t>(r(4u));
        s.ras_swap = static_cast<std::uint8_t>(r(4u));
        s.tex_swap = static_cast<std::uint8_t>(r(4u));
        s.konst_color = static_cast<std::uint8_t>(r(32u));
        s.konst_alpha = static_cast<std::uint8_t>(r(32u));
        if (r(3u) == 0u) {
            s.ind_stage = static_cast<std::uint8_t>(r(4u));
            s.ind_format = static_cast<std::uint8_t>(r(4u));
            s.ind_bias = static_cast<std::uint8_t>(r(8u));
            s.ind_matrix = static_cast<std::uint8_t>(r(12u));
            s.ind_wrap_s = static_cast<std::uint8_t>(r(7u));
            s.ind_wrap_t = static_cast<std::uint8_t>(r(7u));
            s.ind_prev = static_cast<std::uint8_t>(r(2u));
            s.ind_alpha = static_cast<std::uint8_t>(r(4u));
        }
    }
    for (auto& row : p.swap_table) {
        for (auto& c : row) c = static_cast<std::uint8_t>(r(4u));
    }
    for (auto& o : p.ind_order) {
        o = {static_cast<std::uint8_t>(r(8u)), static_cast<std::uint8_t>(r(8u)), static_cast<std::uint8_t>(r(10u)),
             static_cast<std::uint8_t>(r(10u))};
    }
    for (auto& g : p.tex_gen) {
        g.type = static_cast<std::uint8_t>(r(11u));
        g.source = static_cast<std::uint8_t>(r(21u));
        g.matrix = static_cast<std::uint8_t>(30u + 3u * r(10u));
        g.post_matrix = static_cast<std::uint8_t>(64u + 3u * r(20u));
    }
    for (auto& c : p.channel) {
        c.enable = static_cast<std::uint8_t>(r(2u));
        c.material_source = static_cast<std::uint8_t>(r(2u));
        c.ambient_source = static_cast<std::uint8_t>(r(2u));
        c.light_mask = static_cast<std::uint8_t>(r(256u));
        c.diffuse = static_cast<std::uint8_t>(r(3u));
        c.attenuation = static_cast<std::uint8_t>(r(4u));
    }
    p.fog.type = static_cast<std::uint8_t>(r(8u));
    p.alpha_compare = {static_cast<std::uint8_t>(r(8u)), static_cast<std::uint8_t>(r(4u)),
                       static_cast<std::uint8_t>(r(8u)), 0u};
    p.blend = {static_cast<std::uint8_t>(r(4u)), static_cast<std::uint8_t>(r(8u)), static_cast<std::uint8_t>(r(8u)),
               static_cast<std::uint8_t>(r(16u))};
    p.z_mode = {static_cast<std::uint8_t>(r(2u)), static_cast<std::uint8_t>(r(8u)), static_cast<std::uint8_t>(r(2u)), 0u};
    return p;
}

material::Values random_values(Random& r) {
    material::Values v{};
    for (auto& row : v.tev_color) {
        for (auto& c : row) c = static_cast<std::int16_t>(static_cast<int>(r(2048u)) - 1024);
    }
    for (auto& row : v.konst_color) {
        for (auto& c : row) c = static_cast<std::uint8_t>(r(256u));
    }
    for (unsigned i = 0; i < 2u; ++i) {
        for (unsigned c = 0; c < 4u; ++c) {
            v.material_color[i][c] = static_cast<std::uint8_t>(r(256u));
            v.ambient_color[i][c] = static_cast<std::uint8_t>(r(256u));
        }
    }
    v.alpha_ref[0] = static_cast<std::uint8_t>(r(256u));
    v.alpha_ref[1] = static_cast<std::uint8_t>(r(256u));
    for (auto& m : v.ind_matrix) {
        for (auto& row : m.m) {
            for (auto& x : row) x = r.real(-1.0f, 1.0f);
        }
        m.scale = static_cast<std::int8_t>(static_cast<int>(r(40u)) - 20);
    }
    for (auto& s : v.tex_coord_scale) {
        s = {static_cast<std::uint16_t>(1u + r(1024u)), static_cast<std::uint16_t>(1u + r(1024u)),
             static_cast<std::uint8_t>(r(2u)), static_cast<std::uint8_t>(r(2u)), 0u, 0u};
    }
    for (unsigned map = 0; map < 8u; ++map) {
        auto& t = v.texture[map];
        if (r(3u) == 0u) continue;
        t.image = 0x10000u + 0x1000u * map + r(16u) * 0x40u;
        t.width = static_cast<std::uint16_t>(8u << r(6u));
        t.height = static_cast<std::uint16_t>(8u << r(6u));
        t.format = static_cast<std::uint8_t>(r(7u));
        t.wrap_s = static_cast<std::uint8_t>(r(3u));
        t.wrap_t = static_cast<std::uint8_t>(r(3u));
        t.min_filter = static_cast<std::uint8_t>(r(6u));
        t.mag_filter = static_cast<std::uint8_t>(r(2u));
        t.max_lod = static_cast<float>(r(8u));
    }
    v.fog.start = r.real(0.0f, 1000.0f);
    v.fog.end = v.fog.start + r.real(1.0f, 5000.0f);
    v.fog.near_z = r.real(1.0f, 50.0f);
    v.fog.far_z = v.fog.near_z + r.real(100.0f, 100000.0f);
    for (auto& c : v.fog.color) c = static_cast<std::uint8_t>(r(256u));
    return v;
}

// What a draw's pictures follow from: the three keys, and the values the constants and bindings take.
bool same_state(const NativeDrawState& a, const NativeDrawState& b) {
    if (!(build_pixel_shader_key(a, 1u) == build_pixel_shader_key(b, 1u)) ||
        !(build_vertex_shader_key(a, 0u) == build_vertex_shader_key(b, 0u))) {
        return false;
    }
    const RenderStateKey ra = build_render_state_key(a), rb = build_render_state_key(b);
    if (!(ra == rb)) return false;
    const auto eq = [](const auto& x, const auto& y) { return std::memcmp(&x, &y, sizeof(x)) == 0; };
    if (!eq(a.tev_color, b.tev_color) || !eq(a.konst, b.konst) || !eq(a.material_color, b.material_color) ||
        !eq(a.ambient_color, b.ambient_color) || a.alpha.ref0 != b.alpha.ref0 || a.alpha.ref1 != b.alpha.ref1) {
        return false;
    }
    if (a.fog.a != b.fog.a || a.fog.c != b.fog.c || a.fog.b_magnitude != b.fog.b_magnitude ||
        a.fog.b_shift != b.fog.b_shift || !eq(a.fog.color, b.fog.color)) {
        return false;
    }
    for (unsigned i = 0; i < 3u; ++i) {
        if (!eq(a.ind_matrix[i].m, b.ind_matrix[i].m) || a.ind_matrix[i].scale != b.ind_matrix[i].scale) return false;
    }
    for (unsigned c = 0; c < 8u; ++c) {
        if (!eq(a.coord[c].size_minus_one, b.coord[c].size_minus_one) || !eq(a.coord[c].bias, b.coord[c].bias) ||
            !eq(a.coord[c].wrap, b.coord[c].wrap)) {
            return false;
        }
    }
    for (unsigned m = 0; m < 8u; ++m) {
        if (!(a.texture[m].image == b.texture[m].image) || !(a.texture[m].mode == b.texture[m].mode)) return false;
    }
    return true;
}

void values_only_matches_whole() {
    Random r;
    unsigned compared = 0;
    for (int round = 0; round < 20000; ++round) {
        const material::Program program = random_program(r);
        const material::Values first = random_values(r);
        const material::Values second = random_values(r);
        NativeDrawState once;
        once.apply(program, first);
        once.apply(program, second, false);
        NativeDrawState whole;
        whole.apply(program, first);
        whole.apply(program, second);
        ++compared;
        if (!same_state(once, whole)) {
            check(false, "a repeated program's values alone give the whole material's drawing state");
            return;
        }
    }
    std::printf("ok: values-only application equals the whole material (%u random programs)\n", compared);
}

void values_only_marks_no_program_change() {
    Random r;
    for (int round = 0; round < 2000; ++round) {
        const material::Program program = random_program(r);
        NativeDrawState state;
        state.apply(program, random_values(r));
        const PixelShaderKey ps = build_pixel_shader_key(state, 1u);
        const VertexShaderKey vs = build_vertex_shader_key(state, 0u);
        const RenderStateKey rs = build_render_state_key(state);
        state.apply(program, random_values(r), false);
        if (!(build_pixel_shader_key(state, 1u) == ps) || !(build_vertex_shader_key(state, 0u) == vs) ||
            !(build_render_state_key(state) == rs)) {
            check(false, "values-only application changes no shader or pipeline key");
            return;
        }
    }
    std::printf("ok: values-only application leaves the shader keys\n");
}

}  // namespace

int main() {
    values_only_matches_whole();
    values_only_marks_no_program_change();
    if (g_failures != 0) {
        std::printf("%d native material checks FAILED\n", g_failures);
        return 1;
    }
    return 0;
}
