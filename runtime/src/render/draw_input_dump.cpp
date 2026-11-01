// SPDX-License-Identifier: GPL-3.0-only
// Tests (GALAXY_DUMP_DRAW_INPUTS=<shader id>, GALAXY_DUMP_DRAW_INPUTS_FILE=<path>, GALAXY_DUMP_DRAW_INPUTS_FROM=<present>):
// every input the draws of that present (or the run's first 64)
// of the tagged material read, on whichever path draws it (native: its MaterialConstants; GX: the uber shader's
// PixelConstants), so a run with the material native and one with it on GX (GALAXY_TEST_NATIVE_J3D) diff field by
// field: the vertex constants (inline matrices, matrix indices, header words, post rows), the matrix rows 0-63 and
// the post-transform rows, each texture map's image, size, format, palette and sampling, each coordinate's scale.
#include "galaxy/render/render_core.h"

#include "galaxy/host_env.h"

#include "galaxy/render/draw_matrices.h"
#include "galaxy/render/native_shaders.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace galaxy::render {

namespace {

void write_words(std::FILE* f, const char* name, const void* data, std::size_t bytes) {
    const auto* w = static_cast<const std::uint32_t*>(data);
    const std::size_t n = bytes / 4u;
    for (std::size_t i = 0; i < n; i += 4u) {
        std::fprintf(f, "%s[%zu]", name, i);
        for (std::size_t j = i; j < i + 4u && j < n; ++j) {
            float v;
            std::memcpy(&v, &w[j], 4u);
            std::fprintf(f, " %08x(%g)", w[j], static_cast<double>(v));
        }
        std::fputc('\n', f);
    }
}

}  // namespace

void RenderCore::dump_draw_inputs(std::uint32_t native_pe, std::uint32_t native_texmtx, std::uint64_t presents) {
    if (dump_wanted_ == 0u) {
        const auto value = host::read_env_string("GALAXY_DUMP_DRAW_INPUTS");
        dump_wanted_ = value ? static_cast<std::uint32_t>(std::strtoul(value->c_str(), nullptr, 10)) : ~0u;
    }
    // The draws of the one present GALAXY_DUMP_DRAW_INPUTS_FROM names, or the run's first: up to
    // GALAXY_DUMP_DRAW_INPUTS_MAX (64; a shader many sites share draws more in one present).
    static const std::uint64_t from = [] {
        const auto value = host::read_env_string("GALAXY_DUMP_DRAW_INPUTS_FROM");
        return value ? std::strtoull(value->c_str(), nullptr, 10) : 0ull;
    }();
    static const std::uint32_t most = [] {
        const auto value = host::read_env_string("GALAXY_DUMP_DRAW_INPUTS_MAX");
        return value ? static_cast<std::uint32_t>(std::strtoul(value->c_str(), nullptr, 10)) : 64u;
    }();
    // 65535: every J3D material's draws (shader ids from kNativeShaderJ3DFirst).
    const bool wanted = dump_tag_ != 0u && (dump_tag_ == dump_wanted_ ||
                                            (dump_wanted_ == 65535u && dump_tag_ >= kNativeShaderJ3DFirst));
    if (!wanted || dump_count_ >= most || presents < from || (from != 0u && presents > from)) {
        return;
    }
    const auto path = host::read_env_string("GALAXY_DUMP_DRAW_INPUTS_FILE");
    std::FILE* f = nullptr;
    if (fopen_s(&f, path ? path->c_str() : "draw_inputs.txt", dump_count_ == 0u ? "w" : "a") != 0 || f == nullptr) {
        return;
    }
    ++dump_count_;
    const bool native = native_material_ != nullptr;
    std::fprintf(f, "draw %u shader %u path %s\n", dump_count_, dump_tag_, native ? "native" : "gx");
    // The render state the draw's pipeline is built for (blend words, depth, culling, topology) and the blend the
    // drawing state holds (GXSetBlendMode, GXSetDstAlpha).
    const BlendMode blend = draw_blend();
    std::fprintf(f, "render_state blend %08x z %x cull %u topology %u efb_format %u\n", render_state_key_.blend_bits,
                 static_cast<unsigned>(render_state_key_.zmode_bits), static_cast<unsigned>(render_state_key_.cull),
                 static_cast<unsigned>(render_state_key_.primitive_topology),
                 static_cast<unsigned>(draw_state_.pe.pixel_format));
    std::fprintf(f, "blend_mode enable %u subtract %u src %u dst %u color %u alpha %u const_alpha %u,%u\n",
                 blend.blend_enable ? 1u : 0u, blend.subtract ? 1u : 0u, static_cast<unsigned>(blend.src_factor),
                 static_cast<unsigned>(blend.dst_factor), blend.color_update ? 1u : 0u, blend.alpha_update ? 1u : 0u,
                 blend.const_alpha_enable ? 1u : 0u, static_cast<unsigned>(blend.const_alpha));
    // The vertex constants as this draw reads them (the native header words and post rows as flush_draw_state writes
    // them into the uploaded copy).
    VertexConstants vs = cpu_vs_constants_.constants;
    if (native) {
        vs.uber_vs_header[1] = renderer_.efb_scale();
        vs.uber_vs_header[2] = native_pe;
        vs.uber_vs_header[3] = native_texmtx;
        for (unsigned i = 0; i < 8u; ++i) {
            vs.uber_post_texgen[i] = (draw_state_.post_texgen[i].matrix_index & 0x3Fu) |
                                     (draw_state_.post_texgen[i].normalize ? 0x100u : 0u);
        }
    }
    write_words(f, "vs", &vs, sizeof(vs));
    if (native) {
        write_words(f, "material", native_material_->constants, sizeof(native_material_->constants));
    }
    std::fprintf(f, "position_matrix %u texture_matrix", static_cast<unsigned>(draw_state_.position_matrix));
    for (unsigned i = 0; i < 8u; ++i) {
        std::fprintf(f, " %u", static_cast<unsigned>(draw_state_.texture_matrix[i]));
    }
    std::fprintf(f, "\npost_texgen");
    for (unsigned i = 0; i < 8u; ++i) {
        std::fprintf(f, " %u%s", static_cast<unsigned>(draw_state_.post_texgen[i].matrix_index),
                     draw_state_.post_texgen[i].normalize ? "n" : "");
    }
    std::fputc('\n', f);
    for (unsigned r = 0; r < 64u; ++r) {
        std::fprintf(f, "row[%u] %g %g %g %g\n", r, static_cast<double>(matrices_.matrix_value(r, 0)),
                     static_cast<double>(matrices_.matrix_value(r, 1)), static_cast<double>(matrices_.matrix_value(r, 2)),
                     static_cast<double>(matrices_.matrix_value(r, 3)));
    }
    for (unsigned r = 0; r < 64u; ++r) {
        const auto& row = matrices_.row(draw_rows::kPost + r);
        float v[4];
        std::memcpy(v, row.data(), sizeof(v));
        std::fprintf(f, "post[%u] %g %g %g %g\n", r, static_cast<double>(v[0]), static_cast<double>(v[1]),
                     static_cast<double>(v[2]), static_cast<double>(v[3]));
    }
    const std::uint8_t mask = native ? native_texture_mask_ : current_texture_map_mask_;
    std::fprintf(f, "map_mask %02x\n", static_cast<unsigned>(mask));
    for (unsigned m = 0; m < 8u; ++m) {
        const TextureMap& t = draw_state_.texture[m];
        std::fprintf(f,
                     "map[%u] image %llx %ux%u format %u wrap %u,%u filter %u,%u lod_bias %d aniso %u "
                     "lod %u-%u edge %u\n",
                     m, static_cast<unsigned long long>(t.image.address), static_cast<unsigned>(t.image.width),
                     static_cast<unsigned>(t.image.height), static_cast<unsigned>(t.image.format),
                     static_cast<unsigned>(t.mode.wrap_s), static_cast<unsigned>(t.mode.wrap_t),
                     static_cast<unsigned>(t.mode.mag_filter), static_cast<unsigned>(t.mode.min_filter),
                     static_cast<int>(t.mode.lod_bias_x32), static_cast<unsigned>(t.mode.max_aniso),
                     static_cast<unsigned>(t.mode.min_lod_x16), static_cast<unsigned>(t.mode.max_lod_x16),
                     t.edge_lod ? 1u : 0u);
    }
    for (unsigned c = 0; c < 8u; ++c) {
        const auto& s = draw_state_.coord[c];
        std::fprintf(f, "coord[%u] scale %u,%u bias %u,%u wrap %u,%u\n", c,
                     static_cast<unsigned>(s.size_minus_one[0]) + 1u, static_cast<unsigned>(s.size_minus_one[1]) + 1u,
                     s.bias[0] ? 1u : 0u, s.bias[1] ? 1u : 0u, s.wrap[0] ? 1u : 0u, s.wrap[1] ? 1u : 0u);
    }
    std::fclose(f);
}

}  // namespace galaxy::render
