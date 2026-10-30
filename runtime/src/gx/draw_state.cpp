// The drawing state as typed values (draw_state.h). Each op and each material field sets what the GX function or the
// material display list it stands for set on the console (native_state.cpp and native_material.cpp write the same
// state as register words for the readers that have not moved yet).

#include "galaxy/gx/draw_state.h"

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
    const std::uint32_t f = m.diff_flags;
    if (f != 0u) {
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

std::string draw_state_differences(const NativeDrawState& s, const NativeDrawState& e) {
    std::ostringstream out;
    const auto field = [&](const char* name, unsigned have, unsigned want) {
        if (have != want) {
            out << ' ' << name << '=' << have << "/" << want;
        }
    };
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
