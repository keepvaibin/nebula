// The drawing state as typed values (drawing_state.h): the fixed-function, texture and vertex state the native draws
// read. The material, indirect, alpha-compare and fog state is the native materials' own (the game folds it into them).

#include "galaxy/render/drawing_state.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iterator>

namespace galaxy::render {
namespace {

// NEBULA_CULL_* (the game's winding) as the renderer's flipped one: the game's back faces are the renderer's front.
constexpr CullMode kCull[4] = {CullMode::None, CullMode::Front, CullMode::Back, CullMode::All};

// `op` NEBULA_BLEND_*: replace 0, add 1, subtract 2.
void set_blend(BlendMode& b, std::uint8_t op, std::uint8_t source, std::uint8_t destination) {
    b.blend_enable = op == 1u || op == 2u;
    b.subtract = op == 2u;
    b.src_factor = static_cast<BlendFactor>(source <= 9u ? source : 0u);  // NEBULA_FACTOR_*
    b.dst_factor = static_cast<BlendFactor>(destination <= 9u ? destination : 0u);
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

}  // namespace

namespace {

// Field groups and what a change in each invalidates.
struct Group {
    std::size_t offset, size;
    std::uint32_t dirty;
};
#define NEBULA_GROUP(field, flags) Group{offsetof(NativeDrawState, field), sizeof(NativeDrawState::field), flags}
enum : unsigned {
    kGen, kZ, kBlend, kFrame, kChannel, kMaterialColor, kAmbientColor, kTexgen, kPostTexgen, kProjection,
    kProjectionType, kViewport, kPositionMatrix, kTextureMatrix, kLinePoint, kCoord, kTexture, kGroupCount
};
const Group kGroups[kGroupCount] = {
    NEBULA_GROUP(gen, kStateDirtyVertexShader | kStateDirtyRenderState | kStateDirtyVertexConstants),
    NEBULA_GROUP(z, kStateDirtyRenderState),
    NEBULA_GROUP(blend, kStateDirtyRenderState),
    NEBULA_GROUP(target_alpha, kStateDirtyRenderState),
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
    case state::kTexCoordGen: return bit(kTexgen) | bit(kPostTexgen) | bit(kTextureMatrix);
    case state::kProjection: return bit(kProjection) | bit(kProjectionType);
    case state::kViewport: return bit(kViewport);
    case state::kTargetAlpha: return bit(kFrame);
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

void NativeDrawState::apply_pass(const shapes::PassState& pass) {
    tracked(*this, bit(kBlend) | bit(kZ) | bit(kGen) | bit(kPositionMatrix) | bit(kLinePoint) | bit(kCoord), [&] {
        position_matrix = static_cast<std::uint8_t>(pass.position_matrix % 10u);
        line_point.line_width = pass.line_width;
        line_point.line_divisor = pass.line_divisor;
        line_point.point_size = pass.point_size;
        line_point.point_divisor = pass.point_divisor;
        for (unsigned t = 0; t < 8u; ++t) {
            coord[t].line_offset = ((pass.line_offsets >> t) & 1u) != 0u;
            coord[t].point_offset = ((pass.point_offsets >> t) & 1u) != 0u;
        }
        set_blend(blend, pass.blend_op, pass.blend_src, pass.blend_dst);
        blend.color_update = pass.color_write != 0u;
        blend.alpha_update = pass.alpha_write != 0u;
        set_z(z, pass.depth_test, pass.depth_func, pass.depth_write);
        stencil_test = pass.stencil_func;
        stencil_op = pass.stencil_op;
        gen.cull = kCull[pass.cull & 3u];
    });
}

void NativeDrawState::apply(const state::Op& op) {
    std::uint32_t groups = op_groups(op.kind);
    tracked(*this, groups, [&] { set_op(op); });
}

void NativeDrawState::set_op(const state::Op& op) {
    const std::uint8_t* a = op.a;
    switch (op.kind) {
    case state::kTargetAlpha:
        target_alpha = a[0] != 0u;
        break;
    case state::kTexCoordGen: {
        TexGen made = material::tex_gen(a[1], a[2]);
        if (a[5] != 0u && made.type != TexGenType::EmbossMap) {
            made.emboss_source = 0u;  // a coordinate that is not embossed has no emboss source
        }
        texgen[a[0] & 7u] = made;
        post_texgen[a[0] & 7u] = material::post_tex_gen(a[4], a[3]);
        if (a[5] != 0u) {
            texture_matrix[a[0] & 7u] = static_cast<std::uint8_t>(a[6] <= 20u ? a[6] : 20u);
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
    case state::kScissor: {
        // Left, top, width, height in frame units: the box's corners, inclusive.
        const auto left = static_cast<std::int32_t>(std::lround(op.f[0]));
        const auto top = static_cast<std::int32_t>(std::lround(op.f[1]));
        scissor = ScissorRect{left, top, left + static_cast<std::int32_t>(std::lround(op.f[2])) - 1,
                              top + static_cast<std::int32_t>(std::lround(op.f[3])) - 1};
        break;
    }
    default:
        break;
    }
}

}  // namespace galaxy::render
