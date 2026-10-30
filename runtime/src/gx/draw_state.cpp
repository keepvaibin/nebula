// The drawing state as typed values (draw_state.h). Each op and each material field sets what the GX function or the
// material display list it stands for set on the console (native_state.cpp and native_material.cpp write the same
// state as register words for the readers that have not moved yet).

#include "galaxy/gx/draw_state.h"

#include <bit>
#include <iterator>
#include <sstream>

namespace galaxy::gx {
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

// GXChannelID to the rasterized colour a TEV stage reads.
constexpr std::uint8_t kChannelToRas[16] = {0, 1, 0, 1, 0, 1, 7, 5, 6, 0, 0, 0, 0, 0, 0, 7};

// A TEV operation (GXTevOp; a compare operation's kind takes the scale's place, its bias the compare value 3).
void set_color_op(TevStageConfig& s, std::uint8_t op, std::uint8_t bias, std::uint8_t scale, std::uint8_t clamp,
                  std::uint8_t out) {
    const bool compare = op > 1u;
    s.color_bias = static_cast<TevBias>(compare ? 3u : bias & 3u);
    s.color_sub = (op & 1u) != 0u;
    s.color_clamp = (clamp & 1u) != 0u;
    s.color_scale = static_cast<TevScale>(compare ? (op >> 1u) & 3u : scale & 3u);
    s.color_dest = static_cast<std::uint8_t>(out & 3u);
}

void set_alpha_op(TevStageConfig& s, std::uint8_t op, std::uint8_t bias, std::uint8_t scale, std::uint8_t clamp,
                  std::uint8_t out) {
    const bool compare = op > 1u;
    s.alpha_bias = static_cast<TevBias>(compare ? 3u : bias & 3u);
    s.alpha_sub = (op & 1u) != 0u;
    s.alpha_clamp = (clamp & 1u) != 0u;
    s.alpha_scale = static_cast<TevScale>(compare ? (op >> 1u) & 3u : scale & 3u);
    s.alpha_dest = static_cast<std::uint8_t>(out & 3u);
}

void set_color_in(TevStageConfig& s, std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d) {
    s.color_a = static_cast<TevColorArg>(a & 15u);
    s.color_b = static_cast<TevColorArg>(b & 15u);
    s.color_c = static_cast<TevColorArg>(c & 15u);
    s.color_d = static_cast<TevColorArg>(d & 15u);
}

void set_alpha_in(TevStageConfig& s, std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d) {
    s.alpha_a = static_cast<TevAlphaArg>(a & 7u);
    s.alpha_b = static_cast<TevAlphaArg>(b & 7u);
    s.alpha_c = static_cast<TevAlphaArg>(c & 7u);
    s.alpha_d = static_cast<TevAlphaArg>(d & 7u);
}

// GDSetTevOrder's mapping (J3D's and the GD op's): a null map disables the texture, its index kept.
void set_order(TevOrder& o, std::uint8_t coord, std::uint8_t map, std::uint8_t channel) {
    o.texmap = static_cast<std::uint8_t>(map & 7u);
    o.texcoord = static_cast<std::uint8_t>(coord & 7u);
    o.tex_enable = map != 0xFFu;
    o.ras_channel = kChannelToRas[channel & 15u];
}

IndirectStage indirect_stage(std::uint8_t stage, std::uint8_t format, std::uint8_t bias, std::uint8_t alpha,
                             std::uint8_t matrix, std::uint8_t wrap_s, std::uint8_t wrap_t, std::uint8_t lod,
                             std::uint8_t prev) {
    return IndirectStage{static_cast<std::uint8_t>(stage & 3u),  static_cast<std::uint8_t>(format & 3u),
                         static_cast<std::uint8_t>(bias & 7u),   static_cast<std::uint8_t>(alpha & 3u),
                         static_cast<std::uint8_t>(matrix & 15u), static_cast<std::uint8_t>(wrap_s & 7u),
                         static_cast<std::uint8_t>(wrap_t & 7u), (lod & 1u) != 0u, (prev & 1u) != 0u};
}

std::int16_t sign11(std::uint32_t v) {
    v &= 0x7FFu;
    return static_cast<std::int16_t>((v & 0x400u) != 0u ? static_cast<std::int32_t>(v) - 0x800 : static_cast<std::int32_t>(v));
}

// A float as the pixel engine's 20-bit fog fields hold it (the low 12 bits cut).
float fog_float(float v) {
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(v) & 0xFFFFF000u);
}

// GXSetFog's A, B and C for a perspective fog (also J3D's fog block): B as a mantissa and a power of two.
void perspective_fog(float start, float end, float near_z, float far_z, float& a_f, std::uint32_t& b_m,
                     std::uint32_t& b_expn, float& c) {
    float a, b;
    if (far_z == near_z || end == start) {
        a = 0.0f;
        b = 0.5f;
        c = 0.0f;
    } else {
        a = (far_z * near_z) / ((far_z - near_z) * (end - start));
        b = far_z / (far_z - near_z);
        c = start / (end - start);
    }
    float mant = b;
    b_expn = 1u;
    while (mant > 1.0) {
        mant /= 2;
        ++b_expn;
    }
    while (mant > 0 && mant < 0.5) {
        mant *= 2;
        --b_expn;
    }
    a_f = a / static_cast<float>(1u << b_expn);
    b_m = static_cast<std::uint32_t>(mant * 8388638);
}

// GXTexFilter to the min filter field (near, linear, then the mip combinations).
constexpr std::uint8_t kFilter[6] = {0x00, 0x04, 0x01, 0x05, 0x02, 0x06};

// GXTlut names to their place in TMEM's palette half (512-byte units): 16 of 256 entries, then 4 of 1024.
std::uint16_t tlut_offset(std::uint8_t name) {
    return static_cast<std::uint16_t>(name < 16u ? 0x200u + 0x10u * name : 0x300u + 0x40u * ((name - 16u) & 3u));
}

// A texture's sampling as GXInitTexObjLOD and GXInitTexObj set it (mode words built as the hardware holds them,
// then read back: out-of-range values land where the hardware would put them).
void set_texture_mode(TextureMap& t, std::uint8_t wrap_s, std::uint8_t wrap_t, bool mag_linear, std::uint8_t min_filter,
                      bool edge_lod, float lod_bias, std::uint8_t anisotropy, std::uint8_t bias_clamp, float min_lod,
                      float max_lod) {
    const std::uint32_t mode0 =
        (static_cast<std::uint32_t>(wrap_s) | static_cast<std::uint32_t>(wrap_t) << 2u |
         static_cast<std::uint32_t>(mag_linear) << 4u | static_cast<std::uint32_t>(kFilter[min_filter % 6u]) << 5u |
         static_cast<std::uint32_t>(!edge_lod) << 8u |
         static_cast<std::uint32_t>(static_cast<std::uint8_t>(static_cast<std::int32_t>(32.0f * lod_bias))) << 9u |
         static_cast<std::uint32_t>(anisotropy) << 19u | static_cast<std::uint32_t>(bias_clamp) << 21u) &
        0xFFFFFFu;
    const std::uint32_t mode1 =
        static_cast<std::uint32_t>(static_cast<std::uint8_t>(static_cast<std::int32_t>(16.0f * min_lod))) |
        static_cast<std::uint32_t>(static_cast<std::uint8_t>(static_cast<std::int32_t>(16.0f * max_lod))) << 8u;
    t.mode = decode_tex_mode(mode0, mode1);
    t.edge_lod = ((mode0 >> 8u) & 1u) == 0u;
    t.bias_clamp = ((mode0 >> 21u) & 1u) != 0u;
}

void set_texture_size(TextureMap& t, std::uint32_t width, std::uint32_t height, std::uint8_t format) {
    const std::uint32_t word = ((width - 1u) | (height - 1u) << 10u | static_cast<std::uint32_t>(format) << 20u) & 0xFFFFFFu;
    const TexImage image = decode_tex_image(word, 0u);
    t.image.width = image.width;
    t.image.height = image.height;
    t.image.format = image.format;
}

// A palette transfer (BP 0x64 source, 0x65 place and size in 32-byte units, as GXLoadTlut writes them): each place
// it fills reads from the matching part of the source.
void load_palette(NativeDrawState& s, std::uint32_t source_word, std::uint32_t place_word) {
    const TlutTransfer transfer = decode_wii_tlut_transfer(source_word, place_word);
    for (std::uint32_t offset = 0; offset < transfer.byte_count; offset += 512u) {
        const std::uint32_t place = ((transfer.destination_offset + offset) >> 9u) & 1023u;
        const std::uint32_t address = transfer.source_address + offset;
        if (s.palette_place[place] != address) {
            s.palette_place[place] = address;
            ++s.palette_revision;
        }
    }
}

void set_texture_address(TextureMap& t, std::uint64_t image) {
    t.image.guest_addr = static_cast<std::uint32_t>((image >> 5u) & 0xFFFFFFu) << 5u;
}

// GXAttnFn: SPEC 0, SPOT 1, NONE 2 (the channel control's diffuse function only with an attenuation).
XfChannelCtrl make_channel(std::uint8_t material_source, std::uint8_t enable, std::uint8_t light_mask,
                           std::uint8_t ambient_source, std::uint8_t diffuse, std::uint8_t attenuation) {
    const std::uint32_t word = static_cast<std::uint32_t>(material_source) | static_cast<std::uint32_t>(enable) << 1u |
                               static_cast<std::uint32_t>(light_mask & 15u) << 2u |
                               static_cast<std::uint32_t>(ambient_source) << 6u |
                               (attenuation == 0u ? 0u : static_cast<std::uint32_t>(diffuse)) << 7u |
                               static_cast<std::uint32_t>(attenuation != 2u) << 9u |
                               static_cast<std::uint32_t>(attenuation != 0u) << 10u |
                               static_cast<std::uint32_t>(light_mask >> 4u) << 11u;
    return decode_xf_channel_ctrl(word);
}

void set_rgba(std::uint8_t* out, std::uint32_t word) {
    for (unsigned i = 0; i < 4u; ++i) {
        out[i] = static_cast<std::uint8_t>(word >> (24u - 8u * i));
    }
}

IndirectStage material_indirect(const material::TevStage& s) {
    return indirect_stage(s.ind_stage, s.ind_format, s.ind_bias, s.ind_alpha, s.ind_matrix, s.ind_wrap_s,
                          s.ind_wrap_t, s.ind_lod, s.ind_prev);
}

}  // namespace

void NativeDrawState::apply(const state::Op& op) {
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
    case state::kAlphaCompare:
        alpha.comp0 = static_cast<CompareFunc>(a[0] & 7u);
        alpha.ref0 = a[1];
        alpha.logic = static_cast<AlphaTestLogic>(a[2] & 3u);
        alpha.comp1 = static_cast<CompareFunc>(a[3] & 7u);
        alpha.ref1 = a[4];
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
    case state::kTevColorCalc:
        set_color_in(tev[a[0] & 15u], a[1], a[2], a[3], a[4]);
        set_color_op(tev[a[0] & 15u], a[5], a[6], a[7], a[8], a[9]);
        break;
    case state::kTevAlphaCalc:
        set_alpha_in(tev[a[0] & 15u], a[1], a[2], a[3], a[4]);
        set_alpha_op(tev[a[0] & 15u], a[5], a[6], a[7], a[8], a[9]);
        tev[a[0] & 15u].ras_swap = static_cast<std::uint8_t>(a[10] & 3u);
        tev[a[0] & 15u].tex_swap = static_cast<std::uint8_t>(a[11] & 3u);
        break;
    case state::kTevColorIn:
        set_color_in(tev[a[0] & 15u], a[1], a[2], a[3], a[4]);
        break;
    case state::kTevAlphaIn:
        set_alpha_in(tev[a[0] & 15u], a[1], a[2], a[3], a[4]);
        break;
    case state::kTevColorOp:
        set_color_op(tev[a[0] & 15u], a[1], a[2], a[3], a[4], a[5]);
        break;
    case state::kTevAlphaOp:
        set_alpha_op(tev[a[0] & 15u], a[1], a[2], a[3], a[4], a[5]);
        break;
    case state::kTevOrder:
        set_order(tev[a[0] & 14u].order, a[1], a[2], a[3]);
        set_order(tev[(a[0] & 14u) + 1u].order, a[4], a[5], a[6]);
        break;
    case state::kTevStageOrder: {
        // GXSetTevOrder's mapping (not GD's): a null coordinate or map reads as 0, the map enabled only when real.
        TevOrder& o = tev[a[0] & 15u].order;
        o.texmap = static_cast<std::uint8_t>(a[2] >= 8u ? 0u : a[2]);
        o.texcoord = static_cast<std::uint8_t>(a[1] >= 8u ? 0u : a[1]);
        o.tex_enable = a[2] != 0xFFu && a[4] == 0u;
        o.ras_channel = a[3] == 0xFFu ? 7u : kChannelToRas[a[3] & 15u];
        break;
    }
    case state::kTevKColorSel:
        tev[a[0] & 15u].kcsel = static_cast<std::uint8_t>(a[1] & 31u);
        break;
    case state::kTevKAlphaSel:
        tev[a[0] & 15u].kasel = static_cast<std::uint8_t>(a[1] & 31u);
        break;
    case state::kTevSwapMode:
        tev[a[0] & 15u].ras_swap = static_cast<std::uint8_t>(a[1] & 3u);
        tev[a[0] & 15u].tex_swap = static_cast<std::uint8_t>(a[2] & 3u);
        break;
    case state::kTevSwapTable:
        for (unsigned c = 0; c < 4u; ++c) {
            swap[a[0] & 3u][c] = static_cast<std::uint8_t>(a[1u + c] & 3u);
        }
        break;
    case state::kFog: {
        const std::uint32_t proj = (a[0] >> 3u) & 1u;
        const float start = op.f[0], end = op.f[1], near_z = op.f[2], far_z = op.f[3];
        float a_f, c;
        std::uint32_t b_expn = 0u, b_m = 0u;
        if (proj != 0u) {
            if (far_z == near_z || end == start) {
                a_f = 0.0f;
                c = 0.0f;
            } else {
                const float range = 1.0f / (end - start);
                a_f = (far_z - near_z) * range;
                c = (start - near_z) * range;
            }
        } else {
            perspective_fog(start, end, near_z, far_z, a_f, b_m, b_expn, c);
        }
        fog.a = fog_float(a_f);
        if (proj != 0u && a[5] != 0u) {
            // GXSetFog leaves B's two words without their register id for a projected fog: the generation mode
            // takes them (cleared), B keeps its value.
            gen = GenMode{0u, 0u, 1u, CullMode::None, 0u, false};
        } else {
            fog.b_magnitude = b_m & 0xFFFFFFu;
            fog.b_shift = b_expn & 0xFFFFFFu;
        }
        fog.c = fog_float(c);
        fog.projection = proj != 0u;
        fog_type = static_cast<FogType>(a[0] & 7u);
        fog.color[0] = a[1];
        fog.color[1] = a[2];
        fog.color[2] = a[3];
        break;
    }
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
        std::uint32_t word = material::tex_gen_word(a[1], a[2]);
        if (a[5] != 0u && (a[1] < 2u || a[1] > 9u)) {
            word &= ~(0x7u << 12u);  // GXSetTexCoordGen2 sets the bump source only for a bump texgen.
        }
        texgen[a[0] & 7u] = decode_xf_texgen(word);
        post_texgen[a[0] & 7u] = decode_xf_post_texgen(
            static_cast<std::uint32_t>(static_cast<std::int32_t>(a[4]) - 64) | static_cast<std::uint32_t>(a[3]) << 8u);
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
        line_point.line_width = a[0];
        line_point.line_offset = static_cast<std::uint8_t>(a[1] & 7u);
        break;
    case state::kPointSize:
        line_point.point_size = a[0];
        line_point.point_offset = static_cast<std::uint8_t>(a[1] & 7u);
        break;
    case state::kTexOffsets:
        coord[a[0] & 7u].line_offset = (a[1] & 1u) != 0u;
        coord[a[0] & 7u].point_offset = (a[2] & 1u) != 0u;
        break;
    case state::kScissor: {
        // GX's coordinates (342 added), 11 bits each, read back as the rasterizer holds them.
        const auto left = static_cast<std::uint32_t>(op.f[0]);
        const auto top = static_cast<std::uint32_t>(op.f[1]);
        const std::uint32_t tp = top + 342u, lf = left + 342u;
        const std::uint32_t bm = tp + static_cast<std::uint32_t>(op.f[3]) - 1u;
        const std::uint32_t rt = lf + static_cast<std::uint32_t>(op.f[2]) - 1u;
        scissor = decode_scissor((tp & 0x7FFu) | (lf & 0x7FFu) << 12u, (bm & 0x7FFu) | (rt & 0x7FFu) << 12u);
        break;
    }
    case state::kScissorOffset: {
        const std::uint32_t hx = static_cast<std::uint32_t>(static_cast<std::int32_t>(op.f[0]) + 342) >> 1u;
        const std::uint32_t hy = static_cast<std::uint32_t>(static_cast<std::int32_t>(op.f[1]) + 342) >> 1u;
        scissor_offset = decode_scissor_offset((hx & 0x3FFu) | (hy & 0x3FFu) << 10u);
        break;
    }
    case state::kTexLookup:
        set_texture_mode(texture[a[0] & 7u], a[1], a[2], a[4] == 1u, a[3], a[6] != 0u, op.f[2], a[7], a[5], op.f[0],
                         op.f[1]);
        break;
    case state::kTexImageAttr:
        set_texture_size(texture[a[0] & 7u], op.size[0], op.size[1], a[1]);
        break;
    case state::kTexImage:
        set_texture_address(texture[a[0] & 7u], op.image);
        break;
    case state::kCopyClear:
        copy_setup.clear_color[0] = a[0];
        copy_setup.clear_color[1] = a[1];
        copy_setup.clear_color[2] = a[2];
        copy_setup.clear_color[3] = a[3];
        copy_setup.clear_z = static_cast<std::uint32_t>(a[4]) | static_cast<std::uint32_t>(a[5]) << 8u |
                             static_cast<std::uint32_t>(a[6]) << 16u;
        break;
    case state::kCopyVfilter: {
        // Its coefficients, or GXSetCopyFilter's pass-through ones (0 0 21 22 21 0 0) without the filter.
        static constexpr std::uint8_t kOff[7] = {0, 0, 21, 22, 21, 0, 0};
        const std::uint8_t* c = a[0] != 0u ? a + 1 : kOff;
        for (unsigned i = 0; i < 7u; ++i) {
            copy_setup.vfilter[i] = static_cast<std::uint8_t>(c[i] & 63u);
        }
        break;
    }
    case state::kCopyYScale:
        copy_setup.y_scale = static_cast<std::uint16_t>(op.size[0] & 0x1FFu);
        break;
    case state::kTlutLoad:
        // GXLoadTlut: 16 places' worth for the 256-entry names, 64 for the big ones.
        load_palette(*this, static_cast<std::uint32_t>(op.image >> 5u) & 0xFFFFFFu,
                     tlut_offset(a[0]) | static_cast<std::uint32_t>(a[0] < 16u ? 16u : 64u) << 10u);
        break;
    case state::kTexTlut:
        texture[a[0] & 7u].tlut = TlutRef{tlut_offset(a[2]), static_cast<TlutFormat>(a[1] & 3u)};
        break;
    case state::kTevColor:
        for (unsigned c = 0; c < 4u; ++c) {
            tev_color[a[0] & 3u][c] = a[1u + c];
        }
        break;
    case state::kTevColorS10:
        tev_color[a[0] & 3u][0] = sign11(static_cast<std::uint32_t>(static_cast<std::int16_t>(op.size[0])));
        tev_color[a[0] & 3u][1] = sign11(static_cast<std::uint32_t>(static_cast<std::int16_t>(op.size[1])));
        tev_color[a[0] & 3u][2] = sign11(static_cast<std::uint32_t>(static_cast<std::int32_t>(op.f[0])));
        tev_color[a[0] & 3u][3] = sign11(static_cast<std::uint32_t>(static_cast<std::int32_t>(op.f[1])));
        break;
    case state::kTevKColor:
        for (unsigned c = 0; c < 4u; ++c) {
            konst[a[0] & 3u][c] = a[1u + c];
        }
        break;
    case state::kIndTexMatrix: {
        // GXSetIndTexMtx: 11-bit 1.10 terms; the scale exponent kept in six bits (+17).
        const unsigned id = a[0] >= 9u ? a[0] - 9u : a[0] >= 5u ? a[0] - 5u : a[0] >= 1u ? a[0] - 1u : 0u;
        const auto scale = static_cast<std::int8_t>(static_cast<std::int8_t>(a[1]) + 0x11);
        if (id >= 3u) {
            break;
        }
        auto& matrix = ind_matrix[id];
        for (unsigned w = 0; w < 3u; ++w) {
            for (unsigned row = 0; row < 2u; ++row) {
                matrix.m[row][w] = sign11(static_cast<std::uint32_t>(
                    static_cast<std::int32_t>(op.f[2u * w + row] * static_cast<float>(1 << 10))));
            }
        }
        matrix.scale = static_cast<std::int8_t>((static_cast<std::uint8_t>(scale) & 63u) - 17);
        break;
    }
    case state::kZTexture:
        // GXSetZTexture: the format as its type (Z8 0, Z16 1, otherwise 2).
        z_texture.op = static_cast<std::uint8_t>(a[0] & 3u);
        z_texture.type = static_cast<std::uint8_t>(a[1] == 0x11u ? 0u : a[1] == 0x13u ? 1u : 2u);
        z_texture.bias = static_cast<std::uint32_t>(a[2]) | static_cast<std::uint32_t>(a[3]) << 8u |
                         static_cast<std::uint32_t>(a[4]) << 16u;
        break;
    case state::kTevDirect:
        ind[a[0] & 15u] = IndirectStage{};
        break;
    case state::kTevIndirect:
        ind[a[0] & 15u] = indirect_stage(a[1], a[2], a[3], a[9], a[4], a[5], a[6], a[8], a[7]);
        break;
    case state::kIndTexScale:
        ind_order[a[0] & 3u].scale_s = static_cast<std::uint8_t>(a[1] & 15u);
        ind_order[a[0] & 3u].scale_t = static_cast<std::uint8_t>(a[2] & 15u);
        break;
    case state::kIndTexOrder:
        // GXSetIndTexOrder: a null map or coordinate reads as 0.
        ind_order[a[0] & 3u].map = static_cast<std::uint8_t>(a[2] >= 8u ? 0u : a[2]);
        ind_order[a[0] & 3u].coord = static_cast<std::uint8_t>(a[1] >= 8u ? 0u : a[1]);
        break;
    default:
        break;
    }
}

void NativeDrawState::apply(const material::Material& m) {
    const bool dither = (m.loads & material::kLoadsDither) != 0u;
    const auto blend_and_z = [&] {
        set_blend(blend, m.blend.type, m.blend.source, m.blend.destination, m.blend.logic_op);
        if (dither) {
            blend.dither = (m.dither & 1u) != 0u;
        }
        set_z(z, m.z_mode.enable, m.z_mode.func, m.z_mode.update);
    };
    const auto stage_env = [&](unsigned i) {
        const material::TevStage& s = m.tev[i];
        set_color_in(tev[i], s.color_in[0], s.color_in[1], s.color_in[2], s.color_in[3]);
        set_color_op(tev[i], s.color_op, s.color_bias, s.color_scale, s.color_clamp, s.color_reg);
        set_alpha_in(tev[i], s.alpha_in[0], s.alpha_in[1], s.alpha_in[2], s.alpha_in[3]);
        set_alpha_op(tev[i], s.alpha_op, s.alpha_bias, s.alpha_scale, s.alpha_clamp, s.alpha_reg);
        tev[i].ras_swap = static_cast<std::uint8_t>(s.ras_swap & 3u);
        tev[i].tex_swap = static_cast<std::uint8_t>(s.tex_swap & 3u);
    };
    // J3D's TEV colours (registers 1-3; register 0 is the previous stage's output), then konst colours.
    const auto tev_registers = [&] {
        if ((m.loads & material::kLoadsTevColor) != 0u) {
            for (unsigned r = 0; r < 3u; ++r) {
                for (unsigned c = 0; c < 4u; ++c) {
                    tev_color[r + 1u][c] = sign11(static_cast<std::uint32_t>(m.tev_color[r][c]));
                }
            }
        }
        if ((m.loads & material::kLoadsKonstColor) != 0u) {
            for (unsigned r = 0; r < 4u; ++r) {
                for (unsigned c = 0; c < 4u; ++c) {
                    konst[r][c] = m.konst_color[r][c];
                }
            }
        }
    };
    // J3D's fog block: always perspective.
    const auto material_fog = [&] {
        float a_f, c;
        std::uint32_t b_m, b_expn;
        perspective_fog(m.fog.start, m.fog.end, m.fog.near_z, m.fog.far_z, a_f, b_m, b_expn, c);
        fog.a = fog_float(a_f);
        fog.b_magnitude = b_m & 0xFFFFFFu;
        fog.b_shift = b_expn & 0xFFFFFFu;
        fog.c = fog_float(c);
        fog.projection = false;
        fog_type = static_cast<FogType>(m.fog.type & 7u);
        for (unsigned i = 0; i < 3u; ++i) {
            fog.color[i] = m.fog.color[i];
        }
    };
    // J3D's coordinate scale (J3DTexCoordScaleInfo): the s word keeps the line and point offsets.
    const auto coord_scale = [&](unsigned c) {
        const auto& s = m.tex_coord_scale[c & 7u];
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
    // J3D's texture (J3DTexture::loadGX, its palette into the map's own TMEM place).
    const auto textures = [&] {
        for (unsigned map = 0; map < 8u; ++map) {
            const material::Texture& t = m.texture[map];
            if (t.image == 0u) {
                continue;
            }
            TextureMap& out = texture[map];
            set_texture_address(out, t.image);
            set_texture_size(out, t.width, t.height, t.format);
            set_texture_mode(out, t.wrap_s, t.wrap_t, t.mag_filter == 1u, t.min_filter, t.edge_lod != 0u, t.lod_bias,
                             t.anisotropy, t.bias_clamp, t.min_lod, t.max_lod);
            if (t.palette != 0u) {
                const std::uint32_t tmem = (map << 13u) + 0xF0000u;
                const std::uint32_t size = t.palette_entries > 16u ? 256u : 16u;  // GXTlutSize: entries / 16
                load_palette(*this, static_cast<std::uint32_t>(t.palette >> 5u) & 0xFFFFFFu,
                             ((tmem - 0x80000u) >> 9u) | (size / 16u) << 10u);
                out.tlut = TlutRef{static_cast<std::uint16_t>(((tmem - 0x80000u) >> 9u) & 0x3FFu),
                                   static_cast<TlutFormat>(t.palette_format & 3u)};
            }
        }
    };
    const auto tex_gens = [&] {
        for (unsigned i = 0; i < m.tex_gens && i < 8u; ++i) {
            texgen[i] = decode_xf_texgen(material::tex_gen_word(m.tex_gen[i].type, m.tex_gen[i].source));
            post_texgen[i] = decode_xf_post_texgen(m.tex_gen[i].post_matrix);
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
                material_color[i][c] = m.material_color[i][c];
            }
        }
    };
    const unsigned stages = m.tev_stages < 16u ? m.tev_stages : 16u;
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
        if (((f >> 8u) & 15u) != 0u && (f & 0x1000u) != 0u) {  // TexGenNum: diffTexGen
            tex_gens();
        }
        if ((f & 0x1u) != 0u) {  // MatColor
            material_colours();
        }
        if ((f & 0x2u) != 0u || ((f >> 4u) & 15u) != 0u) {  // ColorChan or LightObjNum: diffLight
            channels();
        }
        if (((f >> 20u) & 15u) != 0u) {  // TevStageNum: diffTevStage, then diffTevStageIndirect
            for (unsigned i = 0; i < stages; ++i) {
                stage_env(i);
            }
            if ((f & 0x8000000u) != 0u) {
                for (unsigned i = 0; i < stages; ++i) {
                    ind[i] = material_indirect(m.tev[i]);
                }
            }
        }
        if ((f & 0x1000000u) != 0u) {  // TevReg: diffTevReg
            tev_registers();
        }
        if ((f & 0x10000000u) != 0u && (m.loads & material::kLoadsFog) != 0u) {  // Fog
            material_fog();
        }
        if ((f & 0x20000000u) != 0u) {  // Blend: diffBlend
            blend_and_z();
        }
        if ((f & 0x2000000u) != 0u) {  // KonstColor: GenMode_3Param
            gen.num_texgens = static_cast<std::uint8_t>(m.tex_gens & 15u);
            gen.num_tev_stages = static_cast<std::uint8_t>(((m.tev_stages - 1u) & 15u) + 1u);
            gen.num_ind_stages = static_cast<std::uint8_t>(m.ind_stages & 7u);
        }
        return;
    }
    if ((m.loads & material::kLoadsTev) != 0u) {
        textures();
        for (unsigned i = 0; i < stages; i += 2u) {
            for (unsigned j = i; j < i + 2u; ++j) {
                // J3D's order: a null coordinate reads as 0.
                const material::TevStage& s = m.tev[j];
                set_order(tev[j].order, static_cast<std::uint8_t>(s.tex_coord >= 8u ? 0u : s.tex_coord), s.tex_map,
                          s.channel);
            }
        }
        for (unsigned i = 0; i < m.tev_stages; i += 2u) {
            coord_scale(m.tev[i].tex_coord);
            if (i + 1u < m.tev_stages || m.tev[i + 1u].tex_map != 0xFFu) {
                coord_scale(m.tev[i + 1u].tex_coord);
            }
        }
        tev_registers();
        for (unsigned i = 0; i < stages; ++i) {
            stage_env(i);
            ind[i] = material_indirect(m.tev[i]);
        }
        if ((m.loads & material::kLoadsKonstSelect) != 0u) {
            for (unsigned i = 0; i < 16u; ++i) {
                tev[i].kcsel = static_cast<std::uint8_t>(m.tev[i].konst_color & 31u);
                tev[i].kasel = static_cast<std::uint8_t>(m.tev[i].konst_alpha & 31u);
            }
            for (unsigned t = 0; t < 4u; ++t) {
                for (unsigned c = 0; c < 4u; ++c) {
                    swap[t][c] = static_cast<std::uint8_t>(m.swap_table[t][c] & 3u);
                }
            }
        }
    }
    if ((m.loads & material::kLoadsIndirect) != 0u) {
        for (unsigned i = 0; i < m.ind_stages && i < 3u; ++i) {
            const auto& mt = m.ind_matrix[i];
            for (unsigned w = 0; w < 3u; ++w) {
                for (unsigned row = 0; row < 2u; ++row) {
                    ind_matrix[i].m[row][w] =
                        sign11(static_cast<std::uint32_t>(static_cast<std::int32_t>(mt.m[row][w] * 1024.0f)));
                }
            }
            const auto scale = static_cast<std::int8_t>(mt.scale + 17);
            ind_matrix[i].scale = static_cast<std::int8_t>((static_cast<std::uint8_t>(scale) & 63u) - 17);
        }
        for (unsigned i = 0; i < m.ind_stages && i < 4u; i += 2u) {
            for (unsigned j = i; j < i + 2u; ++j) {
                ind_order[j].scale_s = static_cast<std::uint8_t>(m.ind_order[j].scale_s & 15u);
                ind_order[j].scale_t = static_cast<std::uint8_t>(m.ind_order[j].scale_t & 15u);
            }
        }
        for (unsigned i = 0; i < 4u; ++i) {
            coord_scale(m.ind_order[i].coord);
        }
        for (unsigned i = 0; i < 4u; ++i) {
            ind_order[i].map = static_cast<std::uint8_t>(m.ind_order[i].map & 7u);
            ind_order[i].coord = static_cast<std::uint8_t>(m.ind_order[i].coord & 7u);
        }
    }
    if ((m.loads & material::kLoadsFog) != 0u) {
        material_fog();
    }
    if (m.tex_gens != 0u) {
        tex_gens();
    }
    if ((m.loads & material::kLoadsChannels) != 0u) {
        material_colours();
        if ((m.loads & material::kLoadsAmbient) != 0u) {
            for (unsigned i = 0; i < 2u; ++i) {
                for (unsigned c = 0; c < 4u; ++c) {
                    ambient_color[i][c] = m.ambient_color[i][c];
                }
            }
        }
        channels();
    }
    alpha.comp0 = static_cast<CompareFunc>(m.alpha_compare.comp0 & 7u);
    alpha.ref0 = m.alpha_compare.ref0;
    alpha.logic = static_cast<AlphaTestLogic>(m.alpha_compare.op & 3u);
    alpha.comp1 = static_cast<CompareFunc>(m.alpha_compare.comp1 & 7u);
    alpha.ref1 = m.alpha_compare.ref1;
    blend_and_z();
    pe.early_z = (m.z_compare_location & 1u) != 0u;
    gen.num_texgens = static_cast<std::uint8_t>(m.tex_gens & 15u);
    gen.num_color_chans = static_cast<std::uint8_t>(m.channels & 7u);
    gen.num_tev_stages = static_cast<std::uint8_t>(((m.tev_stages - 1u) & 15u) + 1u);
    gen.cull = kCull[m.cull & 3u];
    gen.num_ind_stages = static_cast<std::uint8_t>(m.ind_stages & 7u);
}

std::uint32_t ind_cmd_word(const IndirectStage& s) {
    return static_cast<std::uint32_t>(s.stage) | static_cast<std::uint32_t>(s.format) << 2u |
           static_cast<std::uint32_t>(s.bias) << 4u | static_cast<std::uint32_t>(s.alpha) << 7u |
           static_cast<std::uint32_t>(s.matrix) << 9u | static_cast<std::uint32_t>(s.wrap_s) << 13u |
           static_cast<std::uint32_t>(s.wrap_t) << 16u | static_cast<std::uint32_t>(s.lod) << 19u |
           static_cast<std::uint32_t>(s.prev) << 20u;
}

std::uint32_t tev_color_word(const TevStageConfig& s) {
    return static_cast<std::uint32_t>(s.color_d) | static_cast<std::uint32_t>(s.color_c) << 4u |
           static_cast<std::uint32_t>(s.color_b) << 8u | static_cast<std::uint32_t>(s.color_a) << 12u |
           static_cast<std::uint32_t>(s.color_bias) << 16u | static_cast<std::uint32_t>(s.color_sub) << 18u |
           static_cast<std::uint32_t>(s.color_clamp) << 19u | static_cast<std::uint32_t>(s.color_scale) << 20u |
           static_cast<std::uint32_t>(s.color_dest) << 22u;
}

std::uint32_t tev_alpha_word(const TevStageConfig& s) {
    return static_cast<std::uint32_t>(s.ras_swap) | static_cast<std::uint32_t>(s.tex_swap) << 2u |
           static_cast<std::uint32_t>(s.alpha_d) << 4u | static_cast<std::uint32_t>(s.alpha_c) << 7u |
           static_cast<std::uint32_t>(s.alpha_b) << 10u | static_cast<std::uint32_t>(s.alpha_a) << 13u |
           static_cast<std::uint32_t>(s.alpha_bias) << 16u | static_cast<std::uint32_t>(s.alpha_sub) << 18u |
           static_cast<std::uint32_t>(s.alpha_clamp) << 19u | static_cast<std::uint32_t>(s.alpha_scale) << 20u |
           static_cast<std::uint32_t>(s.alpha_dest) << 22u;
}

std::uint32_t ind_ref_word(const NativeDrawState& state) {
    std::uint32_t word = 0u;
    for (unsigned i = 0; i < 4u; ++i) {
        word |= (static_cast<std::uint32_t>(state.ind_order[i].map) |
                 static_cast<std::uint32_t>(state.ind_order[i].coord) << 3u) << (6u * i);
    }
    return word;
}

std::uint32_t ind_scale_word(const NativeDrawState& state, unsigned pair) {
    const auto& a = state.ind_order[2u * pair];
    const auto& b = state.ind_order[2u * pair + 1u];
    return static_cast<std::uint32_t>(a.scale_s) | static_cast<std::uint32_t>(a.scale_t) << 4u |
           static_cast<std::uint32_t>(b.scale_s) << 8u | static_cast<std::uint32_t>(b.scale_t) << 12u;
}

std::uint32_t channel_word(const XfChannelCtrl& c) {
    return static_cast<std::uint32_t>(c.material_from_vertex) | static_cast<std::uint32_t>(c.lighting_enable) << 1u |
           static_cast<std::uint32_t>(c.light_mask & 15u) << 2u | static_cast<std::uint32_t>(c.ambient_from_vertex) << 6u |
           static_cast<std::uint32_t>(c.diffuse_func) << 7u | static_cast<std::uint32_t>(c.atten_func) << 9u |
           static_cast<std::uint32_t>(c.light_mask >> 4u) << 11u;
}

std::uint32_t texgen_word(const XfTexGen& g) {
    return static_cast<std::uint32_t>(g.generate_stq) << 1u | static_cast<std::uint32_t>(g.input_form_abc1) << 2u |
           static_cast<std::uint32_t>(g.type) << 4u | static_cast<std::uint32_t>(g.source_row) << 7u |
           static_cast<std::uint32_t>(g.emboss_source) << 12u | static_cast<std::uint32_t>(g.emboss_light) << 15u;
}

std::uint32_t post_texgen_word(const XfPostTexGen& p) {
    return static_cast<std::uint32_t>(p.matrix_index) | static_cast<std::uint32_t>(p.normalize) << 8u;
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

std::uint32_t line_point_word(const NativeDrawState& s) {
    return static_cast<std::uint32_t>(s.line_point.line_width) | static_cast<std::uint32_t>(s.line_point.point_size) << 8u |
           static_cast<std::uint32_t>(s.line_point.line_offset) << 16u |
           static_cast<std::uint32_t>(s.line_point.point_offset) << 19u;
}

std::uint32_t coord_s_word(const NativeDrawState& s, unsigned c) {
    const auto& k = s.coord[c & 7u];
    return static_cast<std::uint32_t>(k.size_minus_one[0]) | static_cast<std::uint32_t>(k.bias[0]) << 16u |
           static_cast<std::uint32_t>(k.wrap[0]) << 17u | static_cast<std::uint32_t>(k.line_offset) << 18u |
           static_cast<std::uint32_t>(k.point_offset) << 19u;
}

std::uint32_t coord_t_word(const NativeDrawState& s, unsigned c) {
    const auto& k = s.coord[c & 7u];
    return static_cast<std::uint32_t>(k.size_minus_one[1]) | static_cast<std::uint32_t>(k.bias[1]) << 16u |
           static_cast<std::uint32_t>(k.wrap[1]) << 17u;
}

std::uint32_t tex_mode0_word(const TextureMap& t) {
    const TexMode& m = t.mode;
    return static_cast<std::uint32_t>(m.wrap_s) | static_cast<std::uint32_t>(m.wrap_t) << 2u |
           static_cast<std::uint32_t>(m.mag_filter) << 4u | static_cast<std::uint32_t>(m.min_filter) << 5u |
           static_cast<std::uint32_t>(!t.edge_lod) << 8u |
           static_cast<std::uint32_t>(static_cast<std::uint8_t>(m.lod_bias_x32)) << 9u |
           static_cast<std::uint32_t>(m.max_aniso) << 19u | static_cast<std::uint32_t>(t.bias_clamp) << 21u;
}

std::uint32_t tex_mode1_word(const TextureMap& t) {
    return static_cast<std::uint32_t>(t.mode.min_lod_x16) | static_cast<std::uint32_t>(t.mode.max_lod_x16) << 8u;
}

std::uint32_t tex_image0_word(const TextureMap& t) {
    return static_cast<std::uint32_t>(t.image.width - 1u) | static_cast<std::uint32_t>(t.image.height - 1u) << 10u |
           static_cast<std::uint32_t>(t.image.format) << 20u;
}

std::uint32_t tlut_word(const TextureMap& t) {
    return static_cast<std::uint32_t>(t.tlut.tmem_offset) | static_cast<std::uint32_t>(t.tlut.format) << 10u;
}

namespace {

// GXCopyTex's block counts: a format's tile is 2^row_shift texels wide.
void copy_tiles(std::uint8_t format, unsigned& row_shift, unsigned& column_shift) {
    switch (format) {
    case 0x00: case 0x08: case 0x0E: case 0x20: case 0x30:  // I4, C4, CMPR, R4, Z4
        row_shift = 3;
        column_shift = 3;
        break;
    case 0x01: case 0x02: case 0x09: case 0x11: case 0x22: case 0x27: case 0x28: case 0x29: case 0x2A: case 0x39:
    case 0x3A:  // I8, IA4, C8, Z8, RA4, A8, R8, G8, B8, Z8M, Z8L
        row_shift = 3;
        column_shift = 2;
        break;
    case 0x03: case 0x04: case 0x05: case 0x06: case 0x0A: case 0x13: case 0x16: case 0x23: case 0x2B: case 0x2C:
    case 0x3C:  // IA8, RGB565, RGB5A3, RGBA8, C14X2, Z16, Z24X8, RA8, RG8, GB8, Z16L
        row_shift = 2;
        column_shift = 2;
        break;
    default:
        row_shift = 0;
        column_shift = 0;
        break;
    }
}

}  // namespace

EfbCopyParams efb_copy_params(const NativeDrawState& state, const state::Op& op) {
    const std::uint8_t* a = op.a;
    const bool display = a[0] != 0u;
    EfbCopyParams p{};
    const auto left = static_cast<std::uint32_t>(op.f[0]), top = static_cast<std::uint32_t>(op.f[1]);
    const auto width = static_cast<std::uint32_t>(op.f[2]), height = static_cast<std::uint32_t>(op.f[3]);
    p.src_x = static_cast<std::uint16_t>(left & 0x3FFu);
    p.src_y = static_cast<std::uint16_t>(top & 0x3FFu);
    p.src_width = static_cast<std::uint16_t>(((width - 1u) & 0x3FFu) + 1u);
    p.src_height = static_cast<std::uint16_t>(((height - 1u) & 0x3FFu) + 1u);
    p.dest_addr = (static_cast<std::uint32_t>(op.image >> 5u) & 0xFFFFFFu) << 5u;
    p.y_scale = state.copy_setup.y_scale;
    p.clamp_top = (a[4] & 1u) != 0u;
    p.clamp_bottom = (a[5] & 1u) != 0u;
    p.clear = a[1] != 0u;
    p.frame_to_field = static_cast<std::uint8_t>(a[6] & 3u);
    p.copy_to_xfb = display;
    if (display) {
        p.dest_stride = (((op.size[0] * 2u) >> 5u) & 0x3FFu) << 5u;
        p.gamma = static_cast<std::uint8_t>(a[8] & 3u);
        p.scale_y = (a[7] & 1u) != 0u;
    } else {
        const std::uint8_t format = a[2];
        unsigned row_shift = 0, column_shift = 0;
        copy_tiles(format, row_shift, column_shift);
        const std::uint32_t w = op.size[0] != 0u ? op.size[0] : 1u;
        const std::uint32_t row_tiles = (w + (1u << row_shift) - 1u) >> row_shift;
        const std::uint32_t blocks = format == 0x06u || format == 0x16u ? 2u : 1u;
        p.dest_stride = ((row_tiles * blocks) & 0x3FFu) << 5u;
        const std::uint32_t pe_format = format == 0x13u ? 0xBu : format & 0xFu;
        p.target_format = static_cast<std::uint8_t>((((pe_format >> 3u) & 1u) << 3u) | (pe_format & 7u));
        p.half_scale = (a[3] & 1u) != 0u;
        p.intensity = format == 0x00u || format == 0x01u || format == 0x02u || format == 0x03u || format == 0x26u;
    }
    for (unsigned i = 0; i < 4u; ++i) {
        p.clear_color[i] = state.copy_setup.clear_color[i];
    }
    p.clear_z = state.copy_setup.clear_z;
    for (unsigned i = 0; i < 7u; ++i) {
        p.vfilter[i] = state.copy_setup.vfilter[i];
    }
    return p;
}

std::uint8_t copy_pixel_format(const NativeDrawState& state, const state::Op& op) {
    return op.a[0] == 0u && (op.a[2] & 0x10u) != 0u ? 3u : state.pe.pixel_format;
}

std::uint32_t palette_address(const NativeDrawState& state, unsigned map) {
    const TextureMap& t = state.texture[map & 7u];
    if (t.image.format != TexFormat::C4 && t.image.format != TexFormat::C8 && t.image.format != TexFormat::C14X2) {
        return 0u;
    }
    return state.palette_place[t.tlut.tmem_offset & 1023u];
}

std::uint32_t konst_rgba(const NativeDrawState& state, unsigned k) {
    const std::uint8_t* c = state.konst[k & 3u];
    return static_cast<std::uint32_t>(c[0]) << 24u | static_cast<std::uint32_t>(c[1]) << 16u |
           static_cast<std::uint32_t>(c[2]) << 8u | c[3];
}

std::string draw_state_differences(const NativeDrawState& s, const NativeDrawState& e) {
    std::ostringstream out;
    const auto field = [&](const char* name, unsigned have, unsigned want) {
        if (have != want) {
            out << ' ' << name << '=' << have << "/" << want;
        }
    };
    for (unsigned i = 0; i < 16u; ++i) {
        const TevStageConfig& a = s.tev[i];
        const TevStageConfig& b = e.tev[i];
        const unsigned have[] = {
            static_cast<unsigned>(a.color_a), static_cast<unsigned>(a.color_b), static_cast<unsigned>(a.color_c),
            static_cast<unsigned>(a.color_d), static_cast<unsigned>(a.color_bias), a.color_sub, a.color_clamp,
            static_cast<unsigned>(a.color_scale), a.color_dest, static_cast<unsigned>(a.alpha_a),
            static_cast<unsigned>(a.alpha_b), static_cast<unsigned>(a.alpha_c), static_cast<unsigned>(a.alpha_d),
            static_cast<unsigned>(a.alpha_bias), a.alpha_sub, a.alpha_clamp, static_cast<unsigned>(a.alpha_scale),
            a.alpha_dest, a.ras_swap, a.tex_swap, a.order.texmap, a.order.texcoord, a.order.tex_enable,
            a.order.ras_channel, a.kcsel, a.kasel, ind_cmd_word(s.ind[i])};
        const unsigned want[] = {
            static_cast<unsigned>(b.color_a), static_cast<unsigned>(b.color_b), static_cast<unsigned>(b.color_c),
            static_cast<unsigned>(b.color_d), static_cast<unsigned>(b.color_bias), b.color_sub, b.color_clamp,
            static_cast<unsigned>(b.color_scale), b.color_dest, static_cast<unsigned>(b.alpha_a),
            static_cast<unsigned>(b.alpha_b), static_cast<unsigned>(b.alpha_c), static_cast<unsigned>(b.alpha_d),
            static_cast<unsigned>(b.alpha_bias), b.alpha_sub, b.alpha_clamp, static_cast<unsigned>(b.alpha_scale),
            b.alpha_dest, b.ras_swap, b.tex_swap, b.order.texmap, b.order.texcoord, b.order.tex_enable,
            b.order.ras_channel, b.kcsel, b.kasel, ind_cmd_word(e.ind[i])};
        static constexpr const char* kNames[] = {
            "ca", "cb", "cc", "cd", "cbias", "csub", "cclamp", "cscale", "cdest", "aa", "ab", "ac", "ad", "abias",
            "asub", "aclamp", "ascale", "adest", "rasswap", "texswap", "texmap", "texcoord", "texen", "ras", "kcsel",
            "kasel", "indcmd"};
        for (unsigned k = 0; k < std::size(have); ++k) {
            if (have[k] != want[k]) {
                out << " tev" << i << '.' << kNames[k] << '=' << have[k] << '/' << want[k];
            }
        }
    }
    for (unsigned t = 0; t < 4u; ++t) {
        for (unsigned c = 0; c < 4u; ++c) {
            field("swap", s.swap[t][c], e.swap[t][c]);
        }
    }
    field("fogtype", static_cast<unsigned>(s.fog_type), static_cast<unsigned>(e.fog_type));
    field("ztexop", s.z_texture.op, e.z_texture.op);
    field("ztextype", s.z_texture.type, e.z_texture.type);
    field("ztexbias", s.z_texture.bias, e.z_texture.bias);
    field("indref", ind_ref_word(s), ind_ref_word(e));
    field("indscale0", ind_scale_word(s, 0u), ind_scale_word(e, 0u));
    field("indscale1", ind_scale_word(s, 1u), ind_scale_word(e, 1u));
    for (unsigned i = 0; i < 3u; ++i) {
        for (unsigned row = 0; row < 2u; ++row) {
            for (unsigned c = 0; c < 3u; ++c) {
                field("indmtx", static_cast<unsigned>(s.ind_matrix[i].m[row][c] & 0x7FF),
                      static_cast<unsigned>(e.ind_matrix[i].m[row][c] & 0x7FF));
            }
        }
        field("indmtxscale", static_cast<unsigned>(s.ind_matrix[i].scale & 63), static_cast<unsigned>(e.ind_matrix[i].scale & 63));
    }
    for (unsigned r = 0; r < 4u; ++r) {
        for (unsigned c = 0; c < 4u; ++c) {
            field("tevcolor", static_cast<unsigned>(s.tev_color[r][c] & 0x7FF), static_cast<unsigned>(e.tev_color[r][c] & 0x7FF));
            field("konst", s.konst[r][c], e.konst[r][c]);
        }
    }
    field("foga", std::bit_cast<std::uint32_t>(s.fog.a), std::bit_cast<std::uint32_t>(e.fog.a));
    field("fogc", std::bit_cast<std::uint32_t>(s.fog.c), std::bit_cast<std::uint32_t>(e.fog.c));
    field("fogbm", s.fog.b_magnitude, e.fog.b_magnitude);
    field("fogbs", s.fog.b_shift, e.fog.b_shift);
    field("fogproj", s.fog.projection, e.fog.projection);
    for (unsigned i = 0; i < 4u; ++i) {
        field("channel", channel_word(s.channel[i]), channel_word(e.channel[i]));
    }
    for (unsigned i = 0; i < 2u; ++i) {
        field("matcolor", rgba_word(s.material_color[i]), rgba_word(e.material_color[i]));
        field("ambcolor", rgba_word(s.ambient_color[i]), rgba_word(e.ambient_color[i]));
    }
    for (unsigned i = 0; i < 8u; ++i) {
        field("texgen", texgen_word(s.texgen[i]), texgen_word(e.texgen[i]));
        field("posttexgen", post_texgen_word(s.post_texgen[i]), post_texgen_word(e.post_texgen[i]));
        field("coords", coord_s_word(s, i), coord_s_word(e, i));
        field("coordt", coord_t_word(s, i), coord_t_word(e, i));
    }
    for (unsigned i = 0; i < 6u; ++i) {
        field("projection", std::bit_cast<std::uint32_t>(s.projection[i]), std::bit_cast<std::uint32_t>(e.projection[i]));
        field("viewport", std::bit_cast<std::uint32_t>(s.viewport[i]), std::bit_cast<std::uint32_t>(e.viewport[i]));
    }
    field("projtype", s.projection_type, e.projection_type);
    field("mtxa", matrix_index_a(s), matrix_index_a(e));
    field("mtxb", matrix_index_b(s), matrix_index_b(e));
    field("linepoint", line_point_word(s), line_point_word(e));
    field("scissorx0", static_cast<unsigned>(s.scissor.x0), static_cast<unsigned>(e.scissor.x0));
    field("scissory0", static_cast<unsigned>(s.scissor.y0), static_cast<unsigned>(e.scissor.y0));
    field("scissorx1", static_cast<unsigned>(s.scissor.x1), static_cast<unsigned>(e.scissor.x1));
    field("scissory1", static_cast<unsigned>(s.scissor.y1), static_cast<unsigned>(e.scissor.y1));
    field("scissoroffx", static_cast<unsigned>(s.scissor_offset.x), static_cast<unsigned>(e.scissor_offset.x));
    field("scissoroffy", static_cast<unsigned>(s.scissor_offset.y), static_cast<unsigned>(e.scissor_offset.y));
    for (unsigned c = 0; c < 3u; ++c) {
        field("fogcolor", s.fog.color[c], e.fog.color[c]);
    }
    field("texgens", s.gen.num_texgens, e.gen.num_texgens);
    field("chans", s.gen.num_color_chans, e.gen.num_color_chans);
    field("tevs", s.gen.num_tev_stages, e.gen.num_tev_stages);
    field("cull", static_cast<unsigned>(s.gen.cull), static_cast<unsigned>(e.gen.cull));
    field("inds", s.gen.num_ind_stages, e.gen.num_ind_stages);
    field("ztest", s.z.test_enable, e.z.test_enable);
    field("zfunc", static_cast<unsigned>(s.z.func), static_cast<unsigned>(e.z.func));
    field("zupdate", s.z.update_enable, e.z.update_enable);
    field("blend", s.blend.blend_enable, e.blend.blend_enable);
    field("logic", s.blend.logic_op_enable, e.blend.logic_op_enable);
    field("dither", s.blend.dither, e.blend.dither);
    field("cupdate", s.blend.color_update, e.blend.color_update);
    field("aupdate", s.blend.alpha_update, e.blend.alpha_update);
    field("dst", static_cast<unsigned>(s.blend.dst_factor), static_cast<unsigned>(e.blend.dst_factor));
    field("src", static_cast<unsigned>(s.blend.src_factor), static_cast<unsigned>(e.blend.src_factor));
    field("sub", s.blend.subtract, e.blend.subtract);
    field("logicmode", s.blend.logic_mode, e.blend.logic_mode);
    field("dstalpha", s.blend.const_alpha_enable, e.blend.const_alpha_enable);
    field("dstalphavalue", s.blend.const_alpha, e.blend.const_alpha);
    field("pixfmt", s.pe.pixel_format, e.pe.pixel_format);
    field("zfmt", s.pe.z_format, e.pe.z_format);
    field("earlyz", s.pe.early_z, e.pe.early_z);
    field("ref0", s.alpha.ref0, e.alpha.ref0);
    field("ref1", s.alpha.ref1, e.alpha.ref1);
    field("comp0", static_cast<unsigned>(s.alpha.comp0), static_cast<unsigned>(e.alpha.comp0));
    field("comp1", static_cast<unsigned>(s.alpha.comp1), static_cast<unsigned>(e.alpha.comp1));
    field("alogic", static_cast<unsigned>(s.alpha.logic), static_cast<unsigned>(e.alpha.logic));
    return out.str();
}

}  // namespace galaxy::gx
