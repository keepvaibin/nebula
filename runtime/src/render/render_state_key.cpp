// A draw's render-state key from its drawing state (render_state_key.h).

#include "galaxy/render/render_state_key.h"

#include "galaxy/render/pso_key.h"

#include "galaxy/render/drawing_state.h"

#include <algorithm>
#include <cstring>

namespace galaxy::render {

RenderStateKey build_render_state_key(const NativeDrawState& state) {
    RenderStateKey key{};
    std::memset(&key, 0, sizeof(key));

    const bool target_alpha = state.target_alpha;
    const BlendMode bm = effective_blend(state.blend, target_alpha);

    // An scene target without alpha reads destination alpha as one. These factors are normalized before
    // hashing, as Aurora does in populate_pipeline_config. pipeline_cache.cpp applies the same
    // mapping to the D3D12 PSO.
    // The pipeline key's three bits per factor are the game's codes, as the pipelines decode them: a colour factor names
    // the other side's colour (source 2 = destination colour, destination 2 = source colour).
    const auto key_code = [](BlendFactor factor) {
        switch (factor) {
        case BlendFactor::DstColor: return BlendFactor::SrcColor;
        case BlendFactor::InvDstColor: return BlendFactor::InvSrcColor;
        default: return factor;
        }
    };
    const auto blend_factor = [has_alpha = target_alpha, key_code](
                                  BlendFactor factor) {
        factor = key_code(factor);
        if (!has_alpha) {
            if (factor == BlendFactor::DstAlpha) {
                return BlendFactor::One;
            }
            if (factor == BlendFactor::InvDstAlpha) {
                return BlendFactor::Zero;
            }
        }
        return factor;
    };

    // Canonicalize blend_bits: with blending off, zero all blend fields so they do not fragment the cache.
    if (bm.blend_enable) {
        // Encode: dst_factor(7:5) | src_factor(4:2) | subtract(1) | blend_enable(0)
        key.blend_bits =
            (1u)                                                                |
            ((static_cast<std::uint32_t>(bm.subtract ? BlendFactor::One : blend_factor(bm.src_factor)) & 0x7u) << 2u) |
            ((static_cast<std::uint32_t>(bm.subtract ? BlendFactor::One : blend_factor(bm.dst_factor)) & 0x7u) << 5u) |
            (bm.subtract ? (1u << 1u) : 0u);
    }
    // scene target write masks (cmode0 bits 3/4): bit25 = color_update, bit26 = alpha_update. Z-clear and
    // z-prepass draws write depth with the colour update off.
    if (bm.color_update) {
        key.blend_bits |= (1u << 25u);
    }
    if (bm.alpha_update && target_alpha) {
        key.blend_bits |= (1u << 26u);
    }

    // zmode_bits: test_enable(0) | func(3:1) | update_enable(4) | stencil test(6:5) | stencil op(8:7)
    const ZMode zm = state.z;
    // A disabled compare passes unconditionally, so the function field is leftover state then.
    key.zmode_bits = static_cast<std::uint16_t>(
        (zm.test_enable   ? 1u : 0u)                                            |
        (zm.test_enable
             ? ((static_cast<std::uint32_t>(zm.func) & 0x7u) << 1u) : 0u)       |
        (zm.update_enable ? (1u << 4u) : 0u) |
        ((state.stencil_test & 0x3u) << 5u) |
        (state.stencil_test != 0u ? (state.stencil_op & 0x3u) << 7u : 0u));

    key.cull = static_cast<std::uint8_t>(state.gen.cull);

    // A texture target has alpha to read and write, the scene none.
    key.pixfmt = target_alpha ? 1u : 0u;
    // The backend overwrites this per draw before the PSO lookup. Triangle is the safe default for
    // tests and callers that build only render-state keys.
    key.primitive_topology = 3u; // D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE

    return key;
}

}  // namespace galaxy::render
