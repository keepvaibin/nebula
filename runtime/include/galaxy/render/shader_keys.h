#pragma once

// Canonical, hashable shader and pipeline keys.
//
// Every field that cannot affect code generation (disabled TEV stages, unused texgens, don't-care
// blend fields) is zeroed, so leftover register state does not fragment the caches. RenderCore
// builds keys with the build_* functions; only tests construct them directly.

#include "galaxy/render/draw_state.h"
#include "galaxy/render/gx_bitfields.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace galaxy::render {

// Every key type is padding-free (asserted), so comparing bytes is exact member-wise equality.
[[nodiscard]] inline bool key_bytes_equal(
    const void* left,
    const void* right,
    std::size_t size) noexcept {
    return std::memcmp(left, right, size) == 0;
}


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

// 64-bit hash of a key whose size is a multiple of eight bytes, a word per step. The shader caches persist it.
[[nodiscard]] inline std::uint64_t hash_key_words(const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::uint64_t hash = mix64(size);
    for (std::size_t i = 0; i < size; i += 8u) {
        std::uint64_t word;
        std::memcpy(&word, bytes + i, sizeof(word));
        hash = mix64(hash ^ word);
    }
    return hash;
}

// One TEV stage of a material's program, typed. Fields no input of the stage reads are zero
// (build_pixel_shader_key), so leftover state does not split the caches.
struct KeyStage {
    std::uint8_t color_in[4];       // TevColorArg a, b, c, d: d + lerp(a, b, c), or a compare
    std::uint8_t color_bias;        // TevBias (3: compare)
    std::uint8_t color_sub;         // subtract, or the compare's function
    std::uint8_t color_clamp;
    std::uint8_t color_scale;       // TevScale, or the compare's width
    std::uint8_t color_dest;        // 0 PREV, 1-3 REG0-2
    std::uint8_t alpha_in[4];       // TevAlphaArg a, b, c, d
    std::uint8_t alpha_bias;
    std::uint8_t alpha_sub;
    std::uint8_t alpha_clamp;
    std::uint8_t alpha_scale;
    std::uint8_t alpha_dest;
    std::uint8_t tex_map;           // 0-7
    std::uint8_t tex_coord;         // 0-7
    std::uint8_t tex_enable;
    std::uint8_t channel;           // rasterized input: 0/1 colour channels, 5 alpha bump, 6 normalized, 7 zero
    std::uint8_t konst_color;       // konst selectors (0-31)
    std::uint8_t konst_alpha;
    std::uint8_t ras_swizzle[4];    // the component (0-3) the rasterized colour's r, g, b, a take
    std::uint8_t tex_swizzle[4];    // the texel's
    // The stage's indirect texture coordinate: all zero for none.
    std::uint8_t ind_stage;         // indirect stage 0-3
    std::uint8_t ind_format;        // offset bits: 0 eight, 1 five, 2 four, 3 three
    std::uint8_t ind_bias;          // bit per component s, t, u
    std::uint8_t ind_alpha;         // bump alpha from: 0 none, 1 s, 2 t, 3 u
    std::uint8_t ind_matrix;        // bits 0-1: 0 none, 1-3 matrix; bits 2-3: 0 static, 1 s-dynamic, 2 t-dynamic
    std::uint8_t ind_wrap_s;        // 0 none, 1-5 256 to 16, 6 zero
    std::uint8_t ind_wrap_t;
    std::uint8_t ind_lod;
    std::uint8_t ind_prev;          // add the previous stage's coordinate
    std::uint8_t reserved[3];

    [[nodiscard]] bool operator==(const KeyStage& other) const noexcept {
        return key_bytes_equal(this, &other, sizeof(*this));
    }
};
static_assert(sizeof(KeyStage) == 44);
static_assert(std::has_unique_object_representations_v<KeyStage>);

[[nodiscard]] constexpr bool indirect_active(const KeyStage& stage) noexcept {
    return (stage.ind_stage | stage.ind_format | stage.ind_bias | stage.ind_alpha | stage.ind_matrix |
            stage.ind_wrap_s | stage.ind_wrap_t | stage.ind_lod | stage.ind_prev) != 0u;
}

// An indirect stage's texture map and coordinate and its coordinate's scale (a power-of-two divisor's log2, 0-9).
struct KeyIndirectOrder {
    std::uint8_t map;
    std::uint8_t coord;
    std::uint8_t scale_s;
    std::uint8_t scale_t;
};
static_assert(sizeof(KeyIndirectOrder) == 4);

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
// A blend under constant destination alpha 0 drawn single-source (a reactive draw: RenderCore's motion keys): the
// alpha channel's own blend writes the 0 (factors zero), the colour blends at the TEV alpha.
inline constexpr std::uint32_t kRenderStateZeroAlpha = 1u << 24u;
// The effects layer (ray-traced scenes, from the game's effects to the water's composite; RendererD3D12's layer
// window): a reactive draw also blends its colour into render target 2 as it blends into the EFB (the effects' own
// light, premultiplied) and the EFB's destination factor into render target 3 (the share of what lay behind them they
// let through), so the water's and clear surfaces' composites go under them exactly. Single-source, its factors never
// reading the EFB.
inline constexpr std::uint8_t kPsKeyLayer = 0x20u;
inline constexpr std::uint32_t kRenderStateLayerTarget = 1u << 23u;
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
// A scene draw while the scene is ray traced (kEnhancedRtReflections, kEnhancedRtLighting or kEnhancedRtPhysical): the
// shader keeps the branches only ray tracing takes (an enhanced key's header bits, a clear ice draw's tint). Without it
// they are left out.
inline constexpr std::uint8_t kPsKeyRtPaths = 0x40u;
// Curved surfaces (RenderConfig::curved_surfaces, curved_surfaces.h): a perspective scene triangle draw of a model the
// game registered. The vertex stage also passes the eye-space position, displacement and crease normals and the
// places that pick one per edge (RenderVertex::curve);
// the pipeline adds the Phong tessellation stages and draws three-point patches: RenderStateKey::primitive_topology
// is kTopologyPatch with the vertex output's variant in its high bits, which picks the hull and domain shaders
// (ShaderGenerator::curved_stage_source).
inline constexpr std::uint8_t kVsKeyCurved = 0x80u;
// VertexShaderKey::features: code a vertex shader carries only for the frames that run it. Interpolated redraws blend
// each vertex with the matching draw's of the frame before (kVertexBlendDef): the vertex shaders of a frame recorded for
// them. Physical ray tracing de-bakes vertex colours (VertexConstants::flags bits 2, 3 and 6): the vertex shaders of its
// frames.
inline constexpr std::uint32_t kVsFeatureVertexBlend = 0x1u;
inline constexpr std::uint32_t kVsFeatureDebake = 0x2u;
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
                                    // bit4 enhanced lighting, bit5 effects layer
    KeyIndirectOrder ind_order[4];  // zeroed past num_ind_stages
    std::uint32_t ind_reserved;     // enhanced lighting material, else zero
    KeyStage stages[16];            // zeroed past num_tev_stages

    [[nodiscard]] bool operator==(const PixelShaderKey& other) const noexcept {
        return key_bytes_equal(this, &other, sizeof(*this));
    }
    [[nodiscard]] std::uint64_t hash() const {
        return hash_key_words(this, sizeof(*this));
    }
};
static_assert(sizeof(PixelShaderKey) == 12 + 4 * sizeof(KeyIndirectOrder) + 4 + 16 * sizeof(KeyStage));
static_assert(sizeof(PixelShaderKey) % 8u == 0u);
static_assert(std::has_unique_object_representations_v<PixelShaderKey>);

// A texture coordinate generator, typed. Fields its type does not read are zero (build_vertex_shader_key).
struct KeyTexGen {
    std::uint8_t projection;        // 1: s, t, q (projected); 0: s, t
    std::uint8_t input_abc1;        // the input has an implicit w of 1
    std::uint8_t type;              // TexGenType
    std::uint8_t source;            // the input row: position, normal, colours, texture coordinates 0-7
    std::uint8_t emboss_source;     // an emboss map's texture coordinate
    std::uint8_t emboss_light;      // and its light
    std::uint8_t normalize;         // a regular coordinate normalized before its post-transform
    std::uint8_t reserved;
};
static_assert(sizeof(KeyTexGen) == 8);

// A lighting channel, typed. An unlit channel keeps only its material source (build_vertex_shader_key).
struct KeyChannel {
    std::uint8_t material_vertex;   // material colour from the vertex
    std::uint8_t lighting;
    std::uint8_t light_mask;        // lights 0-7
    std::uint8_t ambient_vertex;    // ambient colour from the vertex
    std::uint8_t diffuse;           // 0 none, 1 signed, 2 clamped
    std::uint8_t attenuation;       // 0 none, 1 specular, 2 directional, 3 spotlight
    std::uint8_t reserved[2];
};
static_assert(sizeof(KeyChannel) == 8);

struct VertexShaderKey {
    std::uint8_t num_texgens;
    std::uint8_t num_chans;
    std::uint8_t flags;             // bit0 per-vertex posmtx, bit1 lighting,
                                    // bit2 dual-tex (post-matrix) enable,
                                    // bit3 per-pixel lighting outputs, bit4 motion,
                                    // bit5 finite light attenuation,
                                    // bit6 enhanced lighting, bit7 curved surface
    std::uint8_t texmtx_idx_mask;   // texgen n has a per-vertex texture matrix index (masked to active texgens)
    KeyTexGen texgen[8];            // zeroed past num_texgens
    KeyChannel channel[4];          // colour 0, colour 1, alpha 0, alpha 1 (zeroed past num_chans)
    std::uint32_t features;         // kVsFeature*

    [[nodiscard]] bool operator==(const VertexShaderKey& other) const noexcept {
        return key_bytes_equal(this, &other, sizeof(*this));
    }
    [[nodiscard]] std::uint64_t hash() const {
        return hash_key_words(this, sizeof(*this));
    }
};
static_assert(sizeof(VertexShaderKey) == 4 + 8 * sizeof(KeyTexGen) + 4 * sizeof(KeyChannel) + 4);
static_assert(sizeof(VertexShaderKey) % 8u == 0u);
static_assert(std::has_unique_object_representations_v<VertexShaderKey>);

[[nodiscard]] constexpr XfTexGen texgen_of(const KeyTexGen& g) noexcept {
    return XfTexGen{g.projection != 0u, g.input_abc1 != 0u, static_cast<TexGenType>(g.type), g.source,
                    g.emboss_source, g.emboss_light};
}
[[nodiscard]] constexpr XfChannelCtrl channel_of(const KeyChannel& c) noexcept {
    return XfChannelCtrl{c.material_vertex != 0u, c.lighting != 0u, c.light_mask, c.ambient_vertex != 0u, c.diffuse,
                         c.attenuation};
}

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
// vertex shader writes no curve outputs; RenderCore::draw_topology draws a stand-in's triangles as a list).
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

// A PsoKey with its hash computed once. `key` must not change afterwards; RenderCore replaces the
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
    const NativeDrawState& state, unsigned efb_scale, bool true_color);
[[nodiscard]] inline PixelShaderKey build_pixel_shader_key(const NativeDrawState& state, unsigned efb_scale) {
    return build_pixel_shader_key(state, efb_scale, true);
}
// The same with the vertices' matrix slots given (bit 0 a position matrix per vertex, bits 1-8 texture matrices 0-7:
// GX's VCD bits 0-8, or a native shape's) in place of the VCD's.
[[nodiscard]] VertexShaderKey build_vertex_shader_key(const NativeDrawState& state, std::uint32_t matrix_bits);
[[nodiscard]] RenderStateKey build_render_state_key(const NativeDrawState& state);

// A typed stage in the words the uber pixel shader (stage_config, ind_cmd, ind_misc) and the ray tracer's TEV
// (RtTevStage, rt_tev.hlsli) decode on the GPU: GX's TEV field positions, until those programs read typed stages.
[[nodiscard]] constexpr std::uint32_t tev_color_bits(const KeyStage& s) noexcept {
    return std::uint32_t{s.color_in[3]} | std::uint32_t{s.color_in[2]} << 4u | std::uint32_t{s.color_in[1]} << 8u |
           std::uint32_t{s.color_in[0]} << 12u | std::uint32_t{s.color_bias} << 16u | std::uint32_t{s.color_sub} << 18u |
           std::uint32_t{s.color_clamp} << 19u | std::uint32_t{s.color_scale} << 20u | std::uint32_t{s.color_dest} << 22u;
}
[[nodiscard]] constexpr std::uint32_t tev_alpha_bits(const KeyStage& s) noexcept {
    return std::uint32_t{s.alpha_in[3]} << 4u | std::uint32_t{s.alpha_in[2]} << 7u | std::uint32_t{s.alpha_in[1]} << 10u |
           std::uint32_t{s.alpha_in[0]} << 13u | std::uint32_t{s.alpha_bias} << 16u | std::uint32_t{s.alpha_sub} << 18u |
           std::uint32_t{s.alpha_clamp} << 19u | std::uint32_t{s.alpha_scale} << 20u | std::uint32_t{s.alpha_dest} << 22u;
}
[[nodiscard]] constexpr std::uint32_t tev_order_bits(const KeyStage& s) noexcept {
    return std::uint32_t{s.tex_map} | std::uint32_t{s.tex_coord} << 3u | std::uint32_t{s.tex_enable} << 6u |
           std::uint32_t{s.channel} << 7u;
}
// Konst selects (bits 0-4, 5-9) and the swizzles, two bits a component: the rasterized colour's from bit 10, the
// texel's from bit 18.
[[nodiscard]] constexpr std::uint32_t tev_select_bits(const KeyStage& s) noexcept {
    std::uint32_t bits = std::uint32_t{s.konst_color} | std::uint32_t{s.konst_alpha} << 5u;
    for (unsigned c = 0; c < 4u; ++c) {
        bits |= std::uint32_t{s.ras_swizzle[c]} << (10u + 2u * c) | std::uint32_t{s.tex_swizzle[c]} << (18u + 2u * c);
    }
    return bits;
}
[[nodiscard]] constexpr std::uint32_t ind_command_bits(const KeyStage& s) noexcept {
    return std::uint32_t{s.ind_stage} | std::uint32_t{s.ind_format} << 2u | std::uint32_t{s.ind_bias} << 4u |
           std::uint32_t{s.ind_alpha} << 7u | std::uint32_t{s.ind_matrix} << 9u | std::uint32_t{s.ind_wrap_s} << 13u |
           std::uint32_t{s.ind_wrap_t} << 16u | std::uint32_t{s.ind_lod} << 19u | std::uint32_t{s.ind_prev} << 20u;
}
// The indirect stages' maps and coordinates, six bits each.
[[nodiscard]] constexpr std::uint32_t ind_order_bits(const PixelShaderKey& key) noexcept {
    std::uint32_t bits = 0u;
    for (unsigned i = 0; i < 4u; ++i) {
        bits |= (std::uint32_t{key.ind_order[i].map} | std::uint32_t{key.ind_order[i].coord} << 3u) << (6u * i);
    }
    return bits;
}
// A typed generator and channel in the words the uber vertex shader (uber_texgen, uber_post_texgen, uber_channel)
// and the ray tracer's shading records decode: GX's XF field positions.
[[nodiscard]] constexpr std::uint32_t texgen_bits(const KeyTexGen& g) noexcept {
    return std::uint32_t{g.projection} << 1u | std::uint32_t{g.input_abc1} << 2u | std::uint32_t{g.type} << 4u |
           std::uint32_t{g.source} << 7u | std::uint32_t{g.emboss_source} << 12u | std::uint32_t{g.emboss_light} << 15u;
}
[[nodiscard]] constexpr std::uint32_t post_texgen_bits(const KeyTexGen& g) noexcept {
    return std::uint32_t{g.normalize} << 8u;
}
[[nodiscard]] constexpr std::uint32_t channel_bits(const KeyChannel& c) noexcept {
    return std::uint32_t{c.material_vertex} | std::uint32_t{c.lighting} << 1u | (std::uint32_t{c.light_mask} & 15u) << 2u |
           std::uint32_t{c.ambient_vertex} << 6u | std::uint32_t{c.diffuse} << 7u | std::uint32_t{c.attenuation} << 9u |
           (std::uint32_t{c.light_mask} >> 4u) << 11u;
}

// Indirect stages 2 * pair and 2 * pair + 1's coordinate scales, four bits each (s, t).
[[nodiscard]] constexpr std::uint32_t ind_scale_bits(const PixelShaderKey& key, unsigned pair) noexcept {
    const KeyIndirectOrder& a = key.ind_order[2u * pair];
    const KeyIndirectOrder& b = key.ind_order[2u * pair + 1u];
    return std::uint32_t{a.scale_s} | std::uint32_t{a.scale_t} << 4u | std::uint32_t{b.scale_s} << 8u |
           std::uint32_t{b.scale_t} << 12u;
}

}  // namespace galaxy::render
