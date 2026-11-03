// A draw's render-state key from its drawing state (render_state_key.h).

#include "galaxy/render/render_state_key.h"

#include "galaxy/render/pso_key.h"

#include <atomic>

#include "galaxy/render/drawing_state.h"

#include <algorithm>
#include <cstring>

namespace galaxy::render {

namespace {

// game blending uses the material-computed source alpha. The scene target alpha that is eventually stored may
// be quantized to the target format or replaced by the state. Source-alpha blend factors
// therefore need a separate export whenever they cannot safely read color0.a.
[[nodiscard]] bool blend_reads_src_alpha(const BlendMode& bm) {
    return bm.blend_enable && !bm.subtract &&
        (static_cast<std::uint32_t>(bm.src_factor) == 4u ||
         static_cast<std::uint32_t>(bm.src_factor) == 5u ||
         static_cast<std::uint32_t>(bm.dst_factor) == 4u ||
         static_cast<std::uint32_t>(bm.dst_factor) == 5u);
}

[[nodiscard]] bool needs_dual_src_alpha(
    bool frame_alpha,
    const BlendMode& bm) {
    if (!blend_reads_src_alpha(bm)) {
        return false;
    }
    const bool dst_alpha =
        frame_alpha && bm.const_alpha_enable && bm.alpha_update;
    return dst_alpha || frame_alpha;
}

}  // namespace

namespace {
std::atomic<bool> s_dual_source_blending{true};
}  // namespace

void set_dual_source_blending(bool available) noexcept {
    s_dual_source_blending.store(available, std::memory_order_relaxed);
}

bool dual_source_blending() noexcept {
    return s_dual_source_blending.load(std::memory_order_relaxed);
}

RenderStateKey for_device_blending(RenderStateKey key) noexcept {
    if (!dual_source_blending() && (key.blend_bits & (1u << 27u)) != 0u) {
        const bool constant_alpha = (key.blend_bits & (1u << 28u)) != 0u;
        key.blend_bits &= ~((1u << 27u) | (1u << 28u));
        if (constant_alpha) {
            key.blend_bits |= kRenderStateZeroAlpha;
        }
    }
    return key;
}

RenderStateKey build_render_state_key(const NativeDrawState& state) {
    RenderStateKey key{};
    std::memset(&key, 0, sizeof(key));

    const bool frame_alpha = state.frame_alpha;
    const BlendMode bm = effective_blend(state.blend, frame_alpha);

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
    const auto blend_factor = [has_alpha = frame_alpha, key_code](
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

    // Canonicalize blend_bits: with blending and logic ops disabled, zero all blend fields so they
    // do not fragment the cache.
    if (bm.blend_enable) {
        // Encode: dst_factor(7:5) | src_factor(4:2) | subtract(1) | blend_enable(0)
        key.blend_bits =
            (1u)                                                                |
            ((static_cast<std::uint32_t>(bm.subtract ? BlendFactor::One : blend_factor(bm.src_factor)) & 0x7u) << 2u) |
            ((static_cast<std::uint32_t>(bm.subtract ? BlendFactor::One : blend_factor(bm.dst_factor)) & 0x7u) << 5u) |
            (bm.subtract ? (1u << 1u) : 0u);
    } else if (bm.logic_op_enable) {
        // Encode logic op: bit8 = logic_op_enable, bits 12-9 = logic_mode
        key.blend_bits =
            (1u << 8u) |
            ((static_cast<std::uint32_t>(bm.logic_mode) & 0xFu) << 9u);
    }
    // scene target write masks (cmode0 bits 3/4): bit25 = color_update, bit26 = alpha_update. Z-clear and
    // z-prepass draws write depth with the colour update off.
    if (bm.color_update) {
        key.blend_bits |= (1u << 25u);
    }
    if (bm.alpha_update && frame_alpha) {
        key.blend_bits |= (1u << 26u);
    }
    // bit27 = dual-source material alpha. The PSO uses
    // SRC1_ALPHA/INV_SRC1_ALPHA in place of SrcAlpha/InvSrcAlpha, so the blender reads material alpha
    // while color0.a stays the physical scene target alpha.
    if (needs_dual_src_alpha(frame_alpha, bm)) {
        key.blend_bits |= (1u << 27u);
    }
    // Constant scene target alpha replacement is independent of source-alpha routing.
    // The pipeline reads this bit only inside its blend-enabled branch; the
    // pixel shader applies constant destination alpha itself.
    if (bm.blend_enable && frame_alpha && bm.const_alpha_enable &&
        bm.alpha_update) {
        key.blend_bits |= (1u << 28u);
    }

    // zmode_bits: test_enable(0) | func(3:1) | update_enable(4)
    const ZMode zm = state.z;
    // A disabled compare passes unconditionally, so the function field is leftover state then.
    key.zmode_bits = static_cast<std::uint16_t>(
        (zm.test_enable   ? 1u : 0u)                                            |
        (zm.test_enable
             ? ((static_cast<std::uint32_t>(zm.func) & 0x7u) << 1u) : 0u)       |
        (zm.update_enable ? (1u << 4u) : 0u));

    key.cull = static_cast<std::uint8_t>(state.gen.cull);

    // The frame keeps alpha (destination alpha) or not.
    key.pixfmt = frame_alpha ? 1u : 0u;
    // The backend overwrites this per draw before the PSO lookup. Triangle is the safe default for
    // tests and callers that build only render-state keys.
    key.primitive_topology = 3u; // D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE

    return key;
}

}  // namespace galaxy::render
