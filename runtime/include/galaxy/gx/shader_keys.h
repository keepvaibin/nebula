#pragma once

// Canonical, hashable shader and pipeline keys.
//
// Every field that cannot affect code generation (disabled TEV stages, unused texgens, don't-care
// blend fields) is zeroed, so leftover register state does not fragment the caches. GxBackend
// builds keys with the build_* functions; only tests construct them directly.

#include "galaxy/gx/gx_bitfields.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace galaxy::gx {

// Every key type is padding-free (asserted), so comparing bytes is exact member-wise equality.
[[nodiscard]] inline bool key_bytes_equal(
    const void* left,
    const void* right,
    std::size_t size) noexcept {
    return std::memcmp(left, right, size) == 0;
}

class GxState;

// 64-bit FNV-1a. The shader key hashes and shader checksums in the on-disk shader and PSO
// caches use it, so changing it invalidates those caches.
[[nodiscard]] inline std::uint64_t fnv1a64(
    const void* data,
    std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::uint64_t hash = 0xCBF29CE484222325ull;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 0x100000001B3ull;
    }
    return hash;
}

// SplitMix64 finalizer, for the in-memory PsoKey hash, which is never persisted. A PsoKey holds
// already-mixed hashes, so folding its words is cheaper than a byte-wise FNV walk.
[[nodiscard]] constexpr std::uint64_t mix64(std::uint64_t value) noexcept {
    value ^= value >> 30;
    value *= 0xBF58476D1CE4E5B9ull;
    value ^= value >> 27;
    value *= 0x94D049BB133111EBull;
    value ^= value >> 31;
    return value;
}

[[nodiscard]] constexpr std::uint64_t combine64(
    std::uint64_t hash,
    std::uint64_t value) noexcept {
    return mix64(hash ^ mix64(value + 0x9E3779B97F4A7C15ull));
}

// One TEV stage, bit-packed exactly as the uber-shader consumes it
// (uber_constants.h stage_config words share this encoding).
struct TevStagePacked {
    std::uint32_t color_env;        // BP 0xC0+2n value (24 bits used)
    std::uint32_t alpha_env;        // BP 0xC1+2n value (24 bits used)
    std::uint32_t order;            // decoded TevOrder half, repacked low 12
    std::uint32_t ksel;             // kcsel(4:0) | kasel(9:5) | ras swap swizzle r/g/b/a
                                    // (bits 11:10, 13:12, 15:14, 17:16) | tex swap swizzle (bits
                                    // 19:18 to 25:24), resolved from the swap tables the stage selects

    [[nodiscard]] bool operator==(const TevStagePacked& other) const noexcept {
        return key_bytes_equal(this, &other, sizeof(*this));
    }
};
static_assert(sizeof(TevStagePacked) == 16);

// Per-pixel lighting (RenderConfig::per_pixel_lighting): set together on a
// lit triangle draw's vertex and pixel keys by the backend.
inline constexpr std::uint8_t kVsKeyPerPixelLighting = 0x08u;
inline constexpr std::uint8_t kPsKeyPerPixelLighting = 0x01u;
// Motion vectors (temporal scenes): set together on the vertex and pixel
// keys of an opaque triangle draw, whose pipeline carries the motion-vector
// target as render target 1 (RenderStateKey blend_bits bit 29).
inline constexpr std::uint8_t kVsKeyMotion = 0x10u;
inline constexpr std::uint8_t kPsKeyMotion = 0x02u;
inline constexpr std::uint32_t kRenderStateMotionTarget = 1u << 29u;
// Reactive mask (temporal scenes): a blended triangle draw also writes its
// alpha (max-blended, capped at 0.9) to an R8 render target 1.
inline constexpr std::uint8_t kPsKeyReactive = 0x04u;
inline constexpr std::uint32_t kRenderStateReactiveTarget = 1u << 30u;
// Lit channels with spotlight or specular attenuation evaluate the XF attenuation polynomials
// on finite operands (cosine and distance at most 1e18), so a zero coefficient nulls an
// out-of-range cosine as on the hardware, where a light whose direction the game never set
// still lights. The pixel bit marks the per-pixel lighting keys of such draws.
inline constexpr std::uint8_t kVsKeyFiniteLightAttn = 0x20u;
inline constexpr std::uint8_t kPsKeyPplFiniteLightAttn = 0x08u;
// Enhanced lighting (RenderConfig::enhanced_lighting): set together on the
// vertex and pixel keys of a perspective scene triangle draw that is lit or
// environment-mapped. The vertex stage passes the per-pixel lighting
// interpolants (normal from the position matrix when the draw is unlit); the
// pixel key's ind_reserved word carries the material (kEnhanced* below).
inline constexpr std::uint8_t kVsKeyEnhancedLighting = 0x40u;
inline constexpr std::uint8_t kPsKeyEnhancedLighting = 0x10u;
// Curved surfaces (RenderConfig::curved_surfaces, curved_surfaces.h): a perspective scene triangle draw of a model the
// game registered. The vertex stage also passes the eye-space position and the displacement normal (GxVertexOut::bend);
// the pipeline adds the Phong tessellation stages and draws three-point patches: RenderStateKey::primitive_topology
// is kTopologyPatch with the vertex output's variant in its high bits, which picks the hull and domain shaders
// (ShaderGenerator::curved_stage_source).
inline constexpr std::uint8_t kVsKeyCurved = 0x80u;
inline constexpr std::uint8_t kTopologyTypeMask = 0x0Fu;
inline constexpr std::uint8_t kTopologyPatch = 4u;  // D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH
inline constexpr std::uint8_t kCurvedPerPixel = 0x10u;  // the per-pixel lighting interpolants
inline constexpr std::uint8_t kCurvedMotion = 0x20u;    // the motion interpolants
[[nodiscard]] constexpr unsigned curved_variant(std::uint8_t primitive_topology) noexcept {
    return (primitive_topology >> 4u) & 3u;
}
// PixelShaderKey::ind_reserved under kPsKeyEnhancedLighting: bits 0-7 the
// texcoords generated from normals (environment maps), bit 8 colour channel 0
// is lit with a diffuse light function, bit 9 the surface class whose
// highlights the ray tracer's reflections own (environment-mapped metal,
// indirect water), bits 10-17 colour channel 0's lights, 18-19 its diffuse
// function, bit 20 spot attenuation, bit 21 ambient and bit 22 material from
// the vertex colour, bits 24-31 the tuning (GALAXY_ENHANCED_LIGHTING_TUNE)
// hash, zero by default.
inline constexpr std::uint32_t kEnhancedEnvCoordMask = 0xFFu;
inline constexpr std::uint32_t kEnhancedLit = 1u << 8u;
inline constexpr std::uint32_t kEnhancedReflective = 1u << 9u;
inline constexpr unsigned kEnhancedLightShift = 10u;
inline constexpr unsigned kEnhancedDiffuseShift = 18u;
inline constexpr std::uint32_t kEnhancedSpot = 1u << 20u;
inline constexpr std::uint32_t kEnhancedAmbientVertex = 1u << 21u;
inline constexpr std::uint32_t kEnhancedMaterialVertex = 1u << 22u;
inline constexpr unsigned kEnhancedTuneShift = 24u;
inline constexpr std::uint32_t kEnhancedRtReflections = 1u << 16u;
inline constexpr std::uint32_t kEnhancedRtLighting = 1u << 17u;
// Ray-traced lighting in the game's units (GALAXY_RT_PHYSICAL): lit draws show their full-light colour.
inline constexpr std::uint32_t kEnhancedRtPhysical = 1u << 31u;
// Bits 20-25 the material table's roughness (/63), 26-29 its metalness (/15), bit 30 the table names
// the draw's texture (enhanced_table_material): the enhanced highlights take its lobe.
inline constexpr unsigned kEnhancedTableRoughShift = 20u;
inline constexpr unsigned kEnhancedTableMetalShift = 26u;
inline constexpr std::uint32_t kEnhancedTableKnown = 1u << 30u;
inline constexpr std::uint32_t kEnhancedView = 1u << 18u;
inline constexpr std::uint32_t kEnhancedViewChannel = 1u << 19u;

struct PixelShaderKey {
    std::uint8_t num_tev_stages;    // 1-16
    std::uint8_t num_texgens;
    std::uint8_t num_chans;
    std::uint8_t fog_type;          // FogType (params are uniforms, type is key)
    std::uint8_t alpha_comp0;       // CompareFunc (refs are uniforms)
    std::uint8_t alpha_comp1;
    std::uint8_t alpha_logic;       // AlphaTestLogic
    std::uint8_t flags;             // bit0 late_ztest, bit1 z_texture, bit2 dst_alpha,
                                    // bits3-4 ztex op, bits5-6 ztex format, bit7 dual-source
                                    // (TEV alpha on SV_Target1 for SRC1 blend factors)
    std::uint8_t output_flags;      // bits0-2 EFB format, bit3 RGBA6 dither, bit4 RGBA6 with
                                    // 8-bit colour (no 6-bit colour, no dither; alpha stays 6-bit),
                                    // bit5 glow: colour times ztex_params.z (additive scene draws
                                    // while ray-traced lighting is on), bit6 lit translucent: colour
                                    // times texture 7 (the ray-traced lighting) at pixel x ztex_params.z,
                                    // bit7 with bit6: an opaque draw after the trace (its light at most
                                    // 1.15: the surface behind it may be an emitter)
    std::uint8_t num_ind_stages;    // 0-4
    // 0xff for dither keys, which read the EFB scale from the per-draw PS constants, else 0.
    // shader_gen also accepts a fixed scale minus one (0-15).
    std::uint8_t efb_scale_minus_one;
    std::uint8_t reserved;          // bit0 per-pixel lighting, bit1 motion, bit2 reactive,
                                    // bit3 per-pixel finite light attenuation,
                                    // bit4 enhanced lighting
    std::uint32_t ind_cmd[4][4];    // BP 0x10-0x1F, grouped as uint4s
    std::uint32_t ind_ref;          // BP 0x27
    std::uint32_t ind_scale[2];     // BP 0x25-0x26
    std::uint32_t ind_reserved;     // enhanced lighting material, else zero
    TevStagePacked stages[16];      // zeroed past num_tev_stages

    [[nodiscard]] bool operator==(const PixelShaderKey& other) const noexcept {
        return key_bytes_equal(this, &other, sizeof(*this));
    }
    [[nodiscard]] std::uint64_t hash() const {
        return fnv1a64(this, sizeof(*this));
    }
};
static_assert(sizeof(PixelShaderKey) == 12 + 4 * 4 * 4 + 4 * 4 +
                                       16 * sizeof(TevStagePacked));
static_assert(std::has_unique_object_representations_v<PixelShaderKey>);

struct VertexShaderKey {
    std::uint8_t num_texgens;
    std::uint8_t num_chans;
    std::uint8_t flags;             // bit0 per-vertex posmtx, bit1 lighting,
                                    // bit2 dual-tex (post-matrix) enable,
                                    // bit3 per-pixel lighting outputs, bit4 motion,
                                    // bit5 finite light attenuation,
                                    // bit6 enhanced lighting, bit7 curved surface
    std::uint8_t texmtx_idx_mask;   // VCD_LO bits 1-8: texgen n has a per-vertex texture
                                    // matrix index (masked to active texgens)
    std::uint32_t texgen[8];        // raw XF 0x1040+n (zeroed past num_texgens)
    std::uint32_t post_texgen[8];   // raw XF 0x1050+n (dual-tex transforms)
    std::uint32_t channel[4];       // raw XF 0x100E+n: color0, color1,
                                    // alpha0, alpha1 (zeroed past num_chans)

    [[nodiscard]] bool operator==(const VertexShaderKey& other) const noexcept {
        return key_bytes_equal(this, &other, sizeof(*this));
    }
    [[nodiscard]] std::uint64_t hash() const {
        return fnv1a64(this, sizeof(*this));
    }
};
static_assert(sizeof(VertexShaderKey) == 4 + 8 * 4 + 8 * 4 + 4 * 4);
static_assert(std::has_unique_object_representations_v<VertexShaderKey>);

// RenderStateKey::samples_log2 bit 2: the draw renders into the ray tracer's HDR scene EFB
// (RGBA16F) instead of the RGBA8 EFB. Ray tracing runs without MSAA, so the bits never combine.
inline constexpr std::uint8_t kEfbVariantHdr = 4u;
inline constexpr std::uint8_t kEfbVariantSamplesMask = 3u;

// Fixed-function pipeline state, kept apart from the shader keys so one compiled shader pair
// serves every blend and depth variant.
struct RenderStateKey {
    std::uint32_t blend_bits;       // see build_render_state_key; bit27 dual-source alpha,
                                    // bit28 constant EFB alpha overwrite
    std::uint16_t zmode_bits;       // canonicalized BP 0x40
    std::uint8_t cull;              // CullMode
    std::uint8_t pixfmt;            // PeControl pixel format (dst-alpha cap)
    std::uint8_t primitive_topology; // D3D12_PRIMITIVE_TOPOLOGY_TYPE value (bits 0-3; kVsKeyCurved: bits 4-5)
    std::uint8_t samples_log2;      // the EFB's variant: bits 0-1 its samples per pixel (MSAA) as a power of
                                    // two (0 for one), bit 2 kEfbVariantHdr
    std::uint8_t reserved[6];

    // Field-wise, not byte-wise: PsoKey::hash() leaves out `reserved`, and equality must not be
    // finer than the hash, or an entry could become unreachable.
    [[nodiscard]] bool operator==(const RenderStateKey& other) const noexcept {
        return blend_bits == other.blend_bits &&
            zmode_bits == other.zmode_bits &&
            cull == other.cull &&
            pixfmt == other.pixfmt &&
            primitive_topology == other.primitive_topology &&
            samples_log2 == other.samples_log2;
    }
};
static_assert(sizeof(RenderStateKey) == 16);
static_assert(std::has_unique_object_representations_v<RenderStateKey>);

// The render state an uber stand-in draws with: a curved surface's patches as the plain triangles they are (the uber
// vertex shader writes no curve outputs; GxBackend::draw_topology draws a stand-in's triangles as a list).
[[nodiscard]] constexpr RenderStateKey flat_render_state(RenderStateKey rs) noexcept {
    if ((rs.primitive_topology & kTopologyTypeMask) == kTopologyPatch) {
        rs.primitive_topology = 3u;  // D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE
    }
    return rs;
}

struct PsoKey {
    std::uint64_t vs_hash;
    std::uint64_t ps_hash;
    RenderStateKey render_state;

    // Compares exactly the fields hash() mixes (see RenderStateKey::operator==).
    [[nodiscard]] bool operator==(const PsoKey& other) const noexcept {
        return vs_hash == other.vs_hash &&
            ps_hash == other.ps_hash &&
            render_state == other.render_state;
    }
    // Folds the fields with mix64, leaving out `reserved`. Callers that need the hash more than
    // once per lookup hold a MemoizedPsoKey.
    [[nodiscard]] std::uint64_t hash() const {
        std::uint64_t hash = mix64(vs_hash);
        hash = combine64(hash, ps_hash);
        hash = combine64(hash, render_state.blend_bits);
        hash = combine64(hash, render_state.zmode_bits);
        hash = combine64(
            hash,
            static_cast<std::uint64_t>(render_state.cull) |
                (static_cast<std::uint64_t>(render_state.pixfmt) << 8) |
                (static_cast<std::uint64_t>(render_state.primitive_topology)
                 << 16) |
                (static_cast<std::uint64_t>(render_state.samples_log2) << 24));
        return hash;
    }
};
static_assert(sizeof(PsoKey) == 32);
static_assert(std::has_unique_object_representations_v<PsoKey>);

struct PsoKeyHasher {
    [[nodiscard]] std::size_t operator()(const PsoKey& key) const {
        return static_cast<std::size_t>(key.hash());
    }
};

// A PsoKey with its hash computed once. `key` must not change afterwards; GxBackend replaces the
// whole object instead.
struct MemoizedPsoKey {
    PsoKey key{};
    std::uint64_t hash = key.hash();

    MemoizedPsoKey() = default;
    explicit MemoizedPsoKey(const PsoKey& value)
        : key(value), hash(value.hash()) {}
    // For callers that already hold the hash (PipelineCache::get compares hashes before keys).
    // `precomputed_hash` must equal value.hash().
    MemoizedPsoKey(const PsoKey& value, std::uint64_t precomputed_hash)
        : key(value), hash(precomputed_hash) {}

    // Compares the key only; the hash follows from it.
    [[nodiscard]] bool operator==(const MemoizedPsoKey& other) const {
        return key == other.key;
    }
};

struct MemoizedPsoKeyHasher {
    [[nodiscard]] std::size_t operator()(const MemoizedPsoKey& value) const {
        return static_cast<std::size_t>(value.hash);
    }
};

// `true_color` selects an RGBA6 EFB's colour precision: 8-bit, or the hardware's 6-bit colour
// with its 2x2 dither. The renderer always uses 8-bit: the 6-bit colour and dither only band and
// stipple the picture, and at raised internal resolution the dither shows as a dot grid. Only
// shader_gen_tests requests the dither.
[[nodiscard]] PixelShaderKey build_pixel_shader_key(
    const GxState& state, unsigned efb_scale, bool true_color);
[[nodiscard]] inline PixelShaderKey build_pixel_shader_key(const GxState& state, unsigned efb_scale) {
    return build_pixel_shader_key(state, efb_scale, true);
}
[[nodiscard]] VertexShaderKey build_vertex_shader_key(const GxState& state);
[[nodiscard]] RenderStateKey build_render_state_key(const GxState& state);

}  // namespace galaxy::gx
