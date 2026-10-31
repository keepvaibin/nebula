// Native materials: a program applied once and then only its values gives the drawing state the whole material gives
// (RenderCore applies a repeated program's values only, NativeDrawState::apply's `whole` false).

#include "galaxy/render/draw_state.h"
#include "galaxy/render/native_material.h"
#include "galaxy/render/shader_keys.h"

#include <bit>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

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

// A light's words in the light rows, as the shaders read them.
bool section_holds(const DrawMatrices& matrices, unsigned n, const NativeLight& l) {
    const auto at = [&](std::uint32_t word) {
        const std::uint32_t w = draw_rows::kLightWords * n + word;
        return matrices.row(draw_rows::kLight + w / 4u)[w % 4u];
    };
    for (unsigned i = 0; i < 3u; ++i) {
        if (at(draw_rows::kLightPositionWord + i) != std::bit_cast<std::uint32_t>(l.position[i]) ||
            at(draw_rows::kLightDirectionWord + i) != std::bit_cast<std::uint32_t>(l.direction[i]) ||
            at(draw_rows::kLightAngleWord + i) != std::bit_cast<std::uint32_t>(l.angle[i]) ||
            at(draw_rows::kLightDistanceWord + i) != std::bit_cast<std::uint32_t>(l.distance[i])) {
            return false;
        }
    }
    return at(draw_rows::kLightColorWord) == light_color_word(l);
}

bool same_light(const NativeLight& a, const NativeLight& b) {
    return std::memcmp(&a, &b, sizeof(NativeLight)) == 0;
}

// E-05 step 2: the lights are typed; the light section is written from them. A material's lights, then the light
// ops' parts (colour and attenuation; position and direction), each leave the typed light and the section equal.
void typed_lights_write_the_section() {
    Random r;
    bool ok = true;
    for (unsigned trial = 0; trial < 2000u && ok; ++trial) {
        DrawMatrices memory;
        material::Program program{};
        program.loads = material::kLoadsLights | material::kLoadsChannels;  // lights load with the channels
        material::Values values{};
        values.light_mask = static_cast<std::uint8_t>(r(256u));
        for (unsigned n = 0; n < 8u; ++n) {
            NativeLight& l = values.light[n];
            for (unsigned i = 0; i < 3u; ++i) {
                l.position[i] = r.real(-1e5f, 1e5f);
                l.direction[i] = r.real(-1.0f, 1.0f);
                l.angle[i] = r.real(-2.0f, 2.0f);
                l.distance[i] = r.real(0.0f, 4.0f);
            }
            for (unsigned i = 0; i < 4u; ++i) {
                l.color[i] = static_cast<std::uint8_t>(r(256u));
            }
        }
        load_matrices(program, values, memory);
        for (unsigned n = 0; n < 8u; ++n) {
            const NativeLight expected = (values.light_mask & (1u << n)) != 0u ? values.light[n] : NativeLight{};
            ok = ok && same_light(memory.light(n), expected) && section_holds(memory, n, expected);
        }
        const unsigned n = r(8u);
        NativeLight next = memory.light(n);
        const std::uint8_t color[4] = {static_cast<std::uint8_t>(r(256u)), static_cast<std::uint8_t>(r(256u)),
                                       static_cast<std::uint8_t>(r(256u)), static_cast<std::uint8_t>(r(256u))};
        const float angle[3] = {r.real(-2.0f, 2.0f), r.real(-2.0f, 2.0f), r.real(-2.0f, 2.0f)};
        const float distance[3] = {r.real(0.0f, 4.0f), r.real(0.0f, 4.0f), r.real(0.0f, 4.0f)};
        memory.set_light_attenuation(n, color, angle, distance);
        std::memcpy(next.color, color, sizeof(color));
        std::memcpy(next.angle, angle, sizeof(angle));
        std::memcpy(next.distance, distance, sizeof(distance));
        ok = ok && same_light(memory.light(n), next) && section_holds(memory, n, next);
        const float position[3] = {r.real(-1e5f, 1e5f), r.real(-1e5f, 1e5f), r.real(-1e5f, 1e5f)};
        const float direction[3] = {r.real(-1.0f, 1.0f), r.real(-1.0f, 1.0f), r.real(-1.0f, 1.0f)};
        memory.set_light_position(n, position, direction);
        std::memcpy(next.position, position, sizeof(position));
        std::memcpy(next.direction, direction, sizeof(direction));
        ok = ok && same_light(memory.light(n), next) && section_holds(memory, n, next);
    }
    check(ok, "typed lights and the light section agree after material and light-op loads");
    if (ok) {
        std::printf("ok: typed lights write the light section\n");
    }
}

// E-05: a draw's blocks are appended per kind when the kind's rows changed. A write marks its kind changed exactly when
// its values differ from the rows it writes (a write of equal values marks none).
void changed_kinds_name_the_changed_blocks() {
    Random r;
    bool ok = true;
    DrawMatrices matrices;
    matrices.clear_changed();
    for (unsigned trial = 0; trial < 5000u && ok; ++trial) {
        const unsigned kind = r(4u);
        const unsigned place = r(10u);
        float values[12];
        // Half the writes repeat what is there.
        const bool same = r(2u) == 0u;
        const std::uint32_t first = kind == 0u ? draw_rows::kPosition + 3u * place
                                    : kind == 1u ? draw_rows::kNormal + 3u * place
                                    : kind == 2u ? draw_rows::kTexture + 3u * place
                                                 : draw_rows::kPost + 3u * place;
        const unsigned width = kind == 1u ? 3u : 4u;
        for (unsigned row = 0; row < 3u; ++row) {
            for (unsigned c = 0; c < width; ++c) {
                values[row * width + c] =
                    same ? std::bit_cast<float>(matrices.row(first + row)[c]) : r.real(-8.0f, 8.0f);
            }
        }
        bool differs = false;
        for (unsigned row = 0; row < 3u; ++row) {
            for (unsigned c = 0; c < 4u; ++c) {
                const std::uint32_t want = c < width ? std::bit_cast<std::uint32_t>(values[row * width + c]) : 0u;
                differs = differs || matrices.row(first + row)[c] != want;
            }
        }
        const std::uint32_t bit = kind == 0u ? DrawMatrices::kPositions
                                  : kind == 1u ? DrawMatrices::kNormals
                                  : kind == 2u ? DrawMatrices::kTextures
                                               : DrawMatrices::kPosts;
        if (kind == 0u) {
            matrices.set_position(place, values, 0u);
        } else if (kind == 1u) {
            matrices.set_normal(place, values, 0u);
        } else if (kind == 2u) {
            matrices.set_texture(place, values, 3u);
        } else {
            matrices.set_post(place, values);
        }
        ok = matrices.changed() == (differs ? bit : 0u);
        matrices.clear_changed();
    }
    check(ok, "a write marks its kind changed when its values differ, none when they are equal");
    if (ok) {
        std::printf("ok: changed kinds name the changed blocks\n");
    }
}

}  // namespace

int main() {
    values_only_matches_whole();
    values_only_marks_no_program_change();
    typed_lights_write_the_section();
    changed_kinds_name_the_changed_blocks();
    if (g_failures != 0) {
        std::printf("%d native material checks FAILED\n", g_failures);
        return 1;
    }
    return 0;
}
