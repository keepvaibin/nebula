// The drawing state as typed values (draw_state.h): the fixed-function, texture and vertex state the native draws
// read. The TEV, indirect, alpha-compare and fog state is the native materials' own (the game folds it into them).

#include "galaxy/render/draw_state.h"

#include <bit>
#include <cstddef>
#include <cstring>
#include <iterator>

namespace galaxy::render {
namespace {

// GXCullMode to the generation mode's cull field (front and back swap).
constexpr CullMode kCull[4] = {CullMode::None, CullMode::Back, CullMode::Front, CullMode::All};

// GXBlendMode: NONE 0, BLEND 1, LOGIC 2, SUBTRACT 3.
void set_blend(BlendMode& b, std::uint8_t type, std::uint8_t source, std::uint8_t destination, std::uint8_t logic) {
    b.blend_enable = type == 1u || type == 3u;
    b.logic_op_enable = type == 2u;
    b.subtract = type == 3u;
    b.src_factor = static_cast<BlendFactor>(source & 7u);
    b.dst_factor = static_cast<BlendFactor>(destination & 7u);
    b.logic_mode = static_cast<std::uint8_t>(logic & 15u);
}

void set_z(ZMode& z, std::uint8_t enable, std::uint8_t func, std::uint8_t update) {
    z.test_enable = (enable & 1u) != 0u;
    z.func = static_cast<CompareFunc>(func & 7u);
    z.update_enable = (update & 1u) != 0u;
}

// A texture's sampling as the lookup op or the material record states it (texture_sampler.h).
void set_texture_mode(TextureMap& t, std::uint8_t wrap_s, std::uint8_t wrap_t, bool min_linear, bool mag_linear,
                      std::uint8_t mip, bool edge_lod, float lod_bias, std::uint8_t anisotropy, bool bias_clamp,
                      float min_lod, float max_lod) {
    t.mode = TextureSampler{static_cast<TexWrap>(wrap_s > 2u ? 2u : wrap_s),
                            static_cast<TexWrap>(wrap_t > 2u ? 2u : wrap_t),
                            mag_linear,
                            min_linear,
                            static_cast<std::uint8_t>(mip > 2u ? 2u : mip),
                            static_cast<std::uint8_t>(anisotropy & 3u),
                            lod_bias,
                            min_lod,
                            max_lod};
    t.edge_lod = edge_lod;
    t.bias_clamp = bias_clamp;
}

void set_texture_address(TextureMap& t, std::uint64_t image) {
    t.texture = image;
}

// GXAttnFn: SPEC 0, SPOT 1, NONE 2 (the channel's diffuse function only with an attenuation). The channel's
// attenuation: 1 specular, 2 directional (none), 3 spotlight.
XfChannelCtrl make_channel(std::uint8_t material_source, std::uint8_t enable, std::uint8_t light_mask,
                           std::uint8_t ambient_source, std::uint8_t diffuse, std::uint8_t attenuation) {
    return XfChannelCtrl{(material_source & 1u) != 0u,
                         (enable & 1u) != 0u,
                         light_mask,
                         (ambient_source & 1u) != 0u,
                         static_cast<std::uint8_t>(attenuation == 0u ? 0u : diffuse & 3u),
                         static_cast<std::uint8_t>((attenuation != 2u ? 1u : 0u) | (attenuation != 0u ? 2u : 0u))};
}

}  // namespace

namespace {

// Field groups and what a change in each invalidates (at least what the register file's dirty bits did).
struct Group {
    std::size_t offset, size;
    std::uint32_t dirty;
};
#define NEBULA_GROUP(field, flags) Group{offsetof(NativeDrawState, field), sizeof(NativeDrawState::field), flags}
enum : unsigned {
    kGen, kZ, kBlend, kPe, kChannel, kMaterialColor, kAmbientColor, kTexgen, kPostTexgen, kProjection,
    kProjectionType, kViewport, kPositionMatrix, kTextureMatrix, kLinePoint, kCoord, kTexture, kGroupCount
};
const Group kGroups[kGroupCount] = {
    NEBULA_GROUP(gen, kStateDirtyVertexShader | kStateDirtyRenderState | kStateDirtyVertexConstants),
    NEBULA_GROUP(z, kStateDirtyRenderState),
    NEBULA_GROUP(blend, kStateDirtyRenderState),
    NEBULA_GROUP(pe, kStateDirtyRenderState),
    NEBULA_GROUP(channel, kStateDirtyVertexShader | kStateDirtyVertexConstants),
    NEBULA_GROUP(material_color, kStateDirtyVertexConstants),
    NEBULA_GROUP(ambient_color, kStateDirtyVertexConstants),
    NEBULA_GROUP(texgen, kStateDirtyVertexShader | kStateDirtyVertexConstants),
    NEBULA_GROUP(post_texgen, kStateDirtyVertexShader | kStateDirtyVertexConstants),
    NEBULA_GROUP(projection, kStateDirtyVertexConstants),
    NEBULA_GROUP(projection_type, kStateDirtyVertexConstants),
    NEBULA_GROUP(viewport, kStateDirtyVertexConstants),
    NEBULA_GROUP(position_matrix, kStateDirtyVertexConstants),
    NEBULA_GROUP(texture_matrix, kStateDirtyVertexConstants),
    NEBULA_GROUP(line_point, kStateDirtyVertexConstants),
    NEBULA_GROUP(coord, kStateDirtyVertexConstants),
    NEBULA_GROUP(texture, kStateDirtyTex),
};
#undef NEBULA_GROUP

constexpr std::uint32_t bit(unsigned group) {
    return 1u << group;
}

// The groups an op can change (the scissor, copy settings and palettes have their own paths).
std::uint32_t op_groups(std::uint8_t kind) {
    switch (kind) {
    case state::kBlendEtc: case state::kBlend: case state::kDstAlpha: case state::kColorUpdate:
    case state::kAlphaUpdate: case state::kDither:
        return bit(kBlend);
    case state::kZMode: return bit(kZ);
    case state::kCull: case state::kGenMode: case state::kGenMode2: case state::kNumTexGens: case state::kNumChans:
    case state::kNumTevStages: case state::kNumIndStages: case state::kCoPlanar:
        return bit(kGen);
    case state::kChannel: return bit(kChannel);
    case state::kMaterialColor: return bit(kMaterialColor);
    case state::kAmbientColor: return bit(kAmbientColor);
    case state::kTexCoordGen: return bit(kTexgen) | bit(kPostTexgen) | bit(kTextureMatrix);
    case state::kTexLookup: case state::kTexImage:
        return bit(kTexture);
    case state::kFog: return bit(kGen);
    case state::kProjection: return bit(kProjection) | bit(kProjectionType);
    case state::kViewport: return bit(kViewport);
    case state::kZCompLoc: case state::kPixelFormat: return bit(kPe);
    case state::kCurrentMatrix: return bit(kPositionMatrix);
    case state::kLineWidth: case state::kPointSize: return bit(kLinePoint);
    case state::kTexOffsets: return bit(kCoord);
    default: return 0u;
    }
}

// Runs `change` and marks the dirty bits of the groups in `groups` whose bytes it changed.
template <typename Change>
void tracked(NativeDrawState& s, std::uint32_t groups, Change&& change) {
    alignas(NativeDrawState) std::byte before[sizeof(NativeDrawState)];
    const auto* self = reinterpret_cast<const std::byte*>(&s);
    for (std::uint32_t g = groups; g != 0u; g &= g - 1u) {
        const Group& group = kGroups[std::countr_zero(g)];
        std::memcpy(before + group.offset, self + group.offset, group.size);
    }
    change();
    for (std::uint32_t g = groups; g != 0u; g &= g - 1u) {
        const Group& group = kGroups[std::countr_zero(g)];
        if (std::memcmp(before + group.offset, self + group.offset, group.size) != 0) {
            s.dirty |= group.dirty;
        }
    }
}

}  // namespace

void NativeDrawState::apply(const state::Op& op) {
    std::uint32_t groups = op_groups(op.kind);
    tracked(*this, groups, [&] { set_op(op); });
}

void NativeDrawState::apply(const material::Program& m, const material::Values& v, bool program) {
    constexpr std::uint32_t kMaterialGroups = bit(kGen) | bit(kZ) | bit(kBlend) | bit(kPe) | bit(kChannel) |
                                              bit(kMaterialColor) | bit(kAmbientColor) | bit(kTexgen) |
                                              bit(kPostTexgen) | bit(kCoord) | bit(kTexture);
    constexpr std::uint32_t kValueGroups = bit(kMaterialColor) | bit(kAmbientColor) | bit(kCoord) | bit(kTexture);
    tracked(*this, program ? kMaterialGroups : kValueGroups, [&] { set_material(m, v, program); });
}

void NativeDrawState::set_op(const state::Op& op) {
    const std::uint8_t* a = op.a;
    switch (op.kind) {
    case state::kBlendEtc:
        set_blend(blend, a[0], a[1], a[2], a[3]);
        blend.color_update = a[4] != 0u;
        blend.alpha_update = a[5] != 0u;
        blend.dither = a[6] != 0u;
        break;
    case state::kBlend:
        set_blend(blend, a[0], a[1], a[2], a[3]);
        break;
    case state::kZMode:
        set_z(z, a[0], a[1], a[2]);
        break;
    case state::kDstAlpha:
        blend.const_alpha_enable = a[0] != 0u;
        blend.const_alpha = a[1];
        break;
    case state::kCull:
        gen.cull = kCull[a[0] & 3u];
        break;
    case state::kGenMode:
        gen.num_texgens = static_cast<std::uint8_t>(a[0] & 15u);
        gen.num_color_chans = static_cast<std::uint8_t>(a[1] & 7u);
        gen.num_tev_stages = static_cast<std::uint8_t>(((a[2] - 1u) & 15u) + 1u);
        break;
    case state::kGenMode2:
        gen.num_texgens = static_cast<std::uint8_t>(a[0] & 15u);
        gen.num_color_chans = static_cast<std::uint8_t>(a[1] & 7u);
        gen.num_tev_stages = static_cast<std::uint8_t>(((a[2] - 1u) & 15u) + 1u);
        gen.num_ind_stages = static_cast<std::uint8_t>(a[3] & 7u);
        gen.cull = kCull[a[4] & 3u];
        break;
    case state::kColorUpdate:
        blend.color_update = a[0] != 0u;
        break;
    case state::kAlphaUpdate:
        blend.alpha_update = a[0] != 0u;
        break;
    case state::kDither:
        blend.dither = a[0] != 0u;
        break;
    case state::kPixelFormat:
        if (a[0] != 0u) {
            pe.pixel_format = static_cast<std::uint8_t>(a[1] & 7u);
            pe.z_format = static_cast<std::uint8_t>(a[2] & 7u);
        }
        break;
    case state::kZCompLoc:
        pe.early_z = (a[0] & 1u) != 0u;
        break;
    case state::kNumTexGens:
        gen.num_texgens = static_cast<std::uint8_t>(a[0] & 15u);
        break;
    case state::kNumChans:
        gen.num_color_chans = static_cast<std::uint8_t>(a[0] & 7u);
        break;
    case state::kNumTevStages:
        gen.num_tev_stages = static_cast<std::uint8_t>(((a[0] - 1u) & 15u) + 1u);
        break;
    case state::kNumIndStages:
        gen.num_ind_stages = static_cast<std::uint8_t>(a[0] & 7u);
        break;
    case state::kFog:
        // GXSetFog leaves B's two words without their register id for a projected fog: the generation mode takes them
        // (cleared). The fog itself is the native materials' own.
        if (((a[0] >> 3u) & 1u) != 0u && a[5] != 0u) {
            gen = GenMode{0u, 0u, 1u, CullMode::None, 0u, false};
        }
        break;
    case state::kChannel: {
        const XfChannelCtrl channel_ctrl = make_channel(a[3], a[1], a[4], a[2], a[5], a[6]);
        channel[a[0] & 3u] = channel_ctrl;
        if (a[0] == 4u || a[0] == 5u) {  // GX_COLOR0A0, GX_COLOR1A1: the alpha channel too
            channel[a[0] - 2u] = channel_ctrl;
        }
        break;
    }
    case state::kMaterialColor:
    case state::kAmbientColor: {
        std::uint8_t* colour = op.kind == state::kMaterialColor ? material_color[a[0] & 1u] : ambient_color[a[0] & 1u];
        // GXSetChan*Color: GX_COLOR0/1 keep the alpha, GX_ALPHA0/1 keep the colour.
        const bool keep = a[5] != 0u && a[0] < 4u;
        for (unsigned c = 0; c < 4u; ++c) {
            if (!keep || (a[0] < 2u) == (c < 3u)) {
                colour[c] = a[1u + c];
            }
        }
        break;
    }
    case state::kTexCoordGen: {
        XfTexGen made = material::tex_gen(a[1], a[2]);
        if (a[5] != 0u && (a[1] < 2u || a[1] > 9u)) {
            made.emboss_source = 0u;  // GXSetTexCoordGen2 sets the bump source only for a bump texgen.
        }
        texgen[a[0] & 7u] = made;
        post_texgen[a[0] & 7u] = material::post_tex_gen(a[4], a[3]);
        if (a[5] != 0u) {
            texture_matrix[a[0] & 7u] = static_cast<std::uint8_t>(a[6] & 63u);
        }
        break;
    }
    case state::kProjection:
        for (unsigned i = 0; i < 6u; ++i) {
            projection[i] = op.f[i];
        }
        projection_type = a[0];
        break;
    case state::kViewport:
        for (unsigned i = 0; i < 6u; ++i) {
            viewport[i] = op.f[i];
        }
        break;
    case state::kCurrentMatrix:
        position_matrix = static_cast<std::uint8_t>(a[0] & 63u);
        break;
    case state::kLineWidth:
        line_point.line_width = op.f[0];
        line_point.line_divisor = a[1];
        break;
    case state::kPointSize:
        line_point.point_size = op.f[0];
        line_point.point_divisor = a[1];
        break;
    case state::kTexOffsets:
        coord[a[0] & 7u].line_offset = (a[1] & 1u) != 0u;
        coord[a[0] & 7u].point_offset = (a[2] & 1u) != 0u;
        break;
    case state::kScissor: {
        // Left, top, width, height: the box's corners, inclusive.
        const auto left = static_cast<std::int32_t>(op.f[0]);
        const auto top = static_cast<std::int32_t>(op.f[1]);
        scissor = ScissorRect{left, top, left + static_cast<std::int32_t>(op.f[2]) - 1,
                              top + static_cast<std::int32_t>(op.f[3]) - 1};
        break;
    }
    case state::kTexLookup:
        set_texture_mode(texture[a[0] & 7u], a[1], a[2], a[3] != 0u, a[4] != 0u, a[5], a[7] != 0u, op.f[2], a[8],
                         a[6] != 0u, op.f[0], op.f[1]);
        break;
    case state::kTexImage:
        set_texture_address(texture[a[0] & 7u], op.image);
        break;
    default:
        break;
    }
}

void NativeDrawState::set_material(const material::Program& m, const material::Values& v, bool program) {
    const bool dither = (m.loads & material::kLoadsDither) != 0u;
    const auto blend_and_z = [&] {
        set_blend(blend, m.blend.type, m.blend.source, m.blend.destination, m.blend.logic_op);
        if (dither) {
            blend.dither = (m.dither & 1u) != 0u;
        }
        set_z(z, m.z_mode.enable, m.z_mode.func, m.z_mode.update);
    };
    // J3D's coordinate scale (J3DTexCoordScaleInfo): the s word keeps the line and point offsets.
    const auto coord_scale = [&](unsigned c) {
        const auto& s = v.tex_coord_scale[c & 7u];
        auto& out = coord[c & 7u];
        const std::uint32_t sw = (static_cast<std::uint32_t>(s.scale_s - 1u) | static_cast<std::uint32_t>(s.bias_s) << 16u) & 0x3FFFFu;
        const std::uint32_t tw = static_cast<std::uint32_t>(s.scale_t - 1u) | static_cast<std::uint32_t>(s.bias_t) << 16u;
        out.size_minus_one[0] = static_cast<std::uint16_t>(sw);
        out.bias[0] = ((sw >> 16u) & 1u) != 0u;
        out.wrap[0] = ((sw >> 17u) & 1u) != 0u;
        out.size_minus_one[1] = static_cast<std::uint16_t>(tw);
        out.bias[1] = ((tw >> 16u) & 1u) != 0u;
        out.wrap[1] = ((tw >> 17u) & 1u) != 0u;
    };
    // J3D's texture (J3DTexture::loadGX; a colour-indexed one is cooked with its palette applied).
    const auto textures = [&] {
        for (unsigned map = 0; map < 8u; ++map) {
            const material::Texture& t = v.texture[map];
            if (t.image == 0u) {
                continue;
            }
            TextureMap& out = texture[map];
            set_texture_address(out, t.image);
            set_texture_mode(out, t.wrap_s, t.wrap_t, t.min_linear != 0u, t.mag_linear != 0u, t.mip_filter, t.edge_lod != 0u,
                             t.lod_bias, t.anisotropy, t.bias_clamp != 0u, t.min_lod, t.max_lod);
        }
    };
    const auto tex_gens = [&] {
        for (unsigned i = 0; i < m.tex_gens && i < 8u; ++i) {
            texgen[i] = material::tex_gen(m.tex_gen[i].type, m.tex_gen[i].source);
            post_texgen[i] = XfPostTexGen{static_cast<std::uint8_t>(m.tex_gen[i].post_matrix & 63u), false};
        }
    };
    // Channel controls (J3D orders colour 0, alpha 0, colour 1, alpha 1).
    const auto channels = [&] {
        static constexpr unsigned kOrder[4] = {0u, 2u, 1u, 3u};
        for (unsigned i = 0; i < 4u; ++i) {
            const auto& c = m.channel[kOrder[i]];
            channel[i] = make_channel(c.material_source, c.enable, c.light_mask, c.ambient_source, c.diffuse,
                                      c.attenuation);
        }
    };
    const auto material_colours = [&] {
        for (unsigned i = 0; i < 2u; ++i) {
            for (unsigned c = 0; c < 4u; ++c) {
                material_color[i][c] = v.material_color[i][c];
            }
        }
    };
    const std::uint32_t f = m.diff_flags;
    if (f != 0u) {
        if (((f >> 16u) & 15u) != 0u) {  // TexNoNum: diffTexNo
            textures();
        }
        if ((f & 0x4000000u) != 0u) {  // TexCoordScale
            for (unsigned i = 0; i < m.tev_stages; i += 2u) {
                coord_scale(m.tev[i].tex_coord);
                coord_scale(m.tev[i + 1u].tex_coord);
            }
        }
        if (program && ((f >> 8u) & 15u) != 0u && (f & 0x1000u) != 0u) {  // TexGenNum: diffTexGen
            tex_gens();
        }
        if ((f & 0x1u) != 0u) {  // MatColor
            material_colours();
        }
        if (program && ((f & 0x2u) != 0u || ((f >> 4u) & 15u) != 0u)) {  // ColorChan or LightObjNum: diffLight
            channels();
        }
        if (program && (f & 0x20000000u) != 0u) {  // Blend: diffBlend
            blend_and_z();
        }
        if (program && (f & 0x2000000u) != 0u) {  // KonstColor: GenMode_3Param
            gen.num_texgens = static_cast<std::uint8_t>(m.tex_gens & 15u);
            gen.num_tev_stages = static_cast<std::uint8_t>(((m.tev_stages - 1u) & 15u) + 1u);
            gen.num_ind_stages = static_cast<std::uint8_t>(m.ind_stages & 7u);
        }
        return;
    }
    if ((m.loads & material::kLoadsTev) != 0u) {
        textures();
        for (unsigned i = 0; i < m.tev_stages; i += 2u) {
            coord_scale(m.tev[i].tex_coord);
            if (i + 1u < m.tev_stages || m.tev[i + 1u].tex_map != 0xFFu) {
                coord_scale(m.tev[i + 1u].tex_coord);
            }
        }
    }
    if ((m.loads & material::kLoadsIndirect) != 0u) {
        for (unsigned i = 0; i < 4u; ++i) {
            coord_scale(m.ind_order[i].coord);
        }
    }
    if (program && m.tex_gens != 0u) {
        tex_gens();
    }
    if ((m.loads & material::kLoadsChannels) != 0u) {
        material_colours();
        if ((m.loads & material::kLoadsAmbient) != 0u) {
            for (unsigned i = 0; i < 2u; ++i) {
                for (unsigned c = 0; c < 4u; ++c) {
                    ambient_color[i][c] = v.ambient_color[i][c];
                }
            }
        }
        if (program) {
            channels();
        }
    }
    if (!program) {
        return;
    }
    blend_and_z();
    pe.early_z = (m.z_compare_location & 1u) != 0u;
    gen.num_texgens = static_cast<std::uint8_t>(m.tex_gens & 15u);
    gen.num_color_chans = static_cast<std::uint8_t>(m.channels & 7u);
    gen.num_tev_stages = static_cast<std::uint8_t>(((m.tev_stages - 1u) & 15u) + 1u);
    gen.cull = kCull[m.cull & 3u];
    gen.num_ind_stages = static_cast<std::uint8_t>(m.ind_stages & 7u);
}

std::uint32_t rgba_word(const std::uint8_t* c) {
    return static_cast<std::uint32_t>(c[0]) << 24u | static_cast<std::uint32_t>(c[1]) << 16u |
           static_cast<std::uint32_t>(c[2]) << 8u | c[3];
}

std::uint32_t matrix_index_a(const NativeDrawState& s) {
    return static_cast<std::uint32_t>(s.position_matrix) | static_cast<std::uint32_t>(s.texture_matrix[0]) << 6u |
           static_cast<std::uint32_t>(s.texture_matrix[1]) << 12u | static_cast<std::uint32_t>(s.texture_matrix[2]) << 18u |
           static_cast<std::uint32_t>(s.texture_matrix[3]) << 24u;
}

std::uint32_t matrix_index_b(const NativeDrawState& s) {
    return static_cast<std::uint32_t>(s.texture_matrix[4]) | static_cast<std::uint32_t>(s.texture_matrix[5]) << 6u |
           static_cast<std::uint32_t>(s.texture_matrix[6]) << 12u | static_cast<std::uint32_t>(s.texture_matrix[7]) << 18u;
}

namespace {

constexpr std::uint8_t kCaptureFiltered = 16u;  // NEBULA_CAPTURE_FILTERED

void set_filter(EfbCopyParams& p, std::uint8_t flags) {
    p.filtered = (flags & kCaptureFiltered) != 0u;
}

}  // namespace

EfbCopyParams frame_show_params(std::uint16_t width, std::uint16_t height, std::uint8_t flags) {
    EfbCopyParams p{};
    p.src_width = static_cast<std::uint16_t>(((width - 1u) & 0x3FFu) + 1u);
    p.src_height = static_cast<std::uint16_t>(((height - 1u) & 0x3FFu) + 1u);
    p.clamp_top = true;
    p.clamp_bottom = true;
    p.copy_to_xfb = true;
    set_filter(p, flags);
    return p;
}

EfbCopyParams capture_copy_params(std::uint64_t key, const std::uint16_t rect[4],
                                  std::uint8_t channels, std::uint8_t flags) {
    EfbCopyParams p{};
    p.src_x = rect[0];
    p.src_y = rect[1];
    p.src_width = rect[2];
    p.src_height = rect[3];
    p.dest_addr = key;
    p.clamp_top = true;
    p.clamp_bottom = true;
    p.clear = (flags & 1u) != 0u;
    p.half_scale = (flags & 2u) != 0u;
    p.intensity = (flags & 4u) != 0u;
    p.channels = channels < 7u ? channels : 0u;  // NebulaTargetChannels
    p.clear_z = 0xFFFFFFu;  // NEBULA_CAPTURE_CLEAR: colour 0, depth far
    set_filter(p, flags);
    return p;
}

}  // namespace galaxy::render
