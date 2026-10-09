#pragma once

// Canonicalized, hashable shader and pipeline keys.
//
// Canonicalization rule: every field that does not affect code generation for
// the *enabled* stage count is forced to zero before hashing — disabled TEV
// stages, unused texgens, and don't-care blend fields must not fragment the
// cache.  GxBackend builds keys via the build_* helpers; raw construction is
// reserved for tests.
//
// All key types are padding-free (asserted) so byte-wise FNV-1a hashing and
// defaulted equality are well-defined.
//
// No Windows/D3D12 includes — unit-testable offline.

#include "galaxy/gx/gx_bitfields.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace galaxy::gx {

// Every key type below is padding-free (asserted), so a byte comparison is a
// total, well-defined equality and is exactly the relation the defaulted
// member-wise `operator==` computed. It is also cheaper on its own terms: a
// defaulted comparison on a struct with many members compiles to one
// compare-and-branch per member with a data-dependent early exit -- roughly a
// hundred dependent branches for the 280-byte PixelShaderKey -- whereas memcmp
// lowers to a single `repe cmpsb`. These operators sit on the per-dirty-flush
// path in GxBackend::flush_draw_state (build_*_key() result vs the stored key)
// and inside PipelineCache::published_.find (PsoKey).
//
// Note this is an instruction-count argument, not a frame-level one. An earlier
// version of this comment called these comparisons "the measured dominant cost of
// a busy frame"; that came from `[gx-frame-timing]`'s `*_us` fields, which
// gx_backend.cpp:2109-2126 documents as upper bounds carrying the profiler's own
// overhead, and the claim was withdrawn. The comparison is strictly cheaper than
// what it replaced; how much it moves a frame is unmeasured.
//
// Only `operator==` changes here. `hash()` is deliberately untouched: it feeds
// the on-disk shader/PSO caches, so altering it would invalidate persisted state
// for no gain.
[[nodiscard]] inline bool key_bytes_equal(
    const void* left,
    const void* right,
    std::size_t size) noexcept {
    return std::memcmp(left, right, size) == 0;
}

class GxState;

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

// 64x64 -> 64 multiply mix (SplitMix64's finalizer stage). Used only for the
// composite PsoKey hash below: a PsoKey is three already-mixed 64-bit values,
// so a byte-serial FNV walk over its 32 bytes costs ~32 dependent
// multiply-xors purely to re-mix entropy that is already there. Folding the
// three words costs a handful of independent multiplies instead. This is an
// in-memory lookup hash only — it never enters a cache file, a pipeline-library
// entry name or an ABI — so changing it cannot invalidate persisted state.
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
    std::uint32_t ksel;             // kcsel(4:0) | kasel(9:5) |
                                    // resolved ras swap swizzle r/g/b/a
                                    // (11:10,13:12,15:14,17:16) | resolved
                                    // tex swap swizzle (19:18..25:24).  The
                                    // swizzles come from the KSEL swap TABLES
                                    // selected by this stage's ras_swap /
                                    // tex_swap — baking the resolved channels
                                    // keys the shader correctly without
                                    // adding the table registers themselves
                                    // (Dolphin PixelShaderGen uid does the
                                    // same).

    [[nodiscard]] bool operator==(const TevStagePacked& other) const noexcept {
        return key_bytes_equal(this, &other, sizeof(*this));
    }
};
static_assert(sizeof(TevStagePacked) == 16);

struct PixelShaderKey {
    std::uint8_t num_tev_stages;    // 1-16
    std::uint8_t num_texgens;
    std::uint8_t num_chans;
    std::uint8_t fog_type;          // FogType (params are uniforms, type is key)
    std::uint8_t alpha_comp0;       // CompareFunc (refs are uniforms)
    std::uint8_t alpha_comp1;
    std::uint8_t alpha_logic;       // AlphaTestLogic
    std::uint8_t flags;             // bit0 late_ztest, bit1 z_texture,
                                    // bit2 dst_alpha (GXSetDstAlpha on RGBA6
                                    // EFB with alpha_update), bits3-4 ztex op,
                                    // bits5-6 ztex format, bit7 dual-source
                                    // TEV source alpha → PS exports TEV alpha
                                    // on SV_Target1 for SRC1 blend factors
    std::uint8_t output_flags;      // bits0-2 EFB format, bit3 RGBA6 dither
    std::uint8_t num_ind_stages;    // 0-4
    // Legacy 0..15 keys retain their original shader source. New dither keys
    // use 0xff and read reciprocal scale from the per-draw PS constants.
    std::uint8_t efb_scale_minus_one;
    std::uint8_t reserved;
    std::uint32_t ind_cmd[4][4];    // BP 0x10-0x1F, grouped as uint4s
    std::uint32_t ind_ref;          // BP 0x27
    std::uint32_t ind_scale[2];     // BP 0x25-0x26
    std::uint32_t ind_reserved;
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
                                    // bit2 dual-tex (post-matrix) enable
    std::uint8_t texmtx_idx_mask;   // VCD_LO bits 1-8: per-vertex texture
                                    // matrix index present for texgen n
                                    // (masked to active texgens — affects VS
                                    // codegen: per-vertex idx vs MatrixIndexA/B)
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

// Fixed-function PSO state — affects pipeline creation only, never HLSL
// codegen.  Kept apart from the shader keys so one compiled shader pair is
// shared across blend/depth variants.
struct RenderStateKey {
    std::uint32_t blend_bits;       // bit27 dual-source alpha, bit28 constant
                                    // EFB alpha overwrite; PE_CMODE0/1 fields
                                    // when blending/logic disabled)
    std::uint16_t zmode_bits;       // canonicalized BP 0x40
    std::uint8_t cull;              // CullMode
    std::uint8_t pixfmt;            // PeControl pixel format (dst-alpha cap)
    std::uint8_t primitive_topology; // D3D12_PRIMITIVE_TOPOLOGY_TYPE value
    std::uint8_t reserved[7];

    // Field-wise, deliberately NOT the byte-wise `key_bytes_equal` form.
    //
    // The decisive reason is the hash, not the padding. `PsoKey::hash()` — which
    // is what `published_`, `in_flight_` and the frame PSO memo actually bucket on
    // — mixes `blend_bits`, `zmode_bits` and the packed cull/pixfmt/topology word,
    // and deliberately **excludes** `reserved`. Equality must partition the same
    // way its hash does, so folding `reserved` into `operator==` would break the
    // unordered-container contract: two keys with identical meaningful fields would
    // hash equal and compare unequal, making an entry unreachable and turning a
    // guaranteed hit into a miss plus a redundant descriptor rebuild.
    //
    // That defect is currently masked rather than absent: `valid_pso_key()`
    // (`pipeline_cache.cpp`) is the only source of disk-loaded keys and rejects any
    // nonzero `reserved` byte, and every in-memory construction path starts from a
    // value-initialised object. So the bytes are zero in practice. Relying on a
    // validation rule to keep a comparison correct is the fragile part, and it is
    // not worth ~15 bytes of SIMD width to keep that coupling.
    [[nodiscard]] bool operator==(const RenderStateKey& other) const noexcept {
        return blend_bits == other.blend_bits &&
            zmode_bits == other.zmode_bits &&
            cull == other.cull &&
            pixfmt == other.pixfmt &&
            primitive_topology == other.primitive_topology;
    }
};
static_assert(sizeof(RenderStateKey) == 16);
static_assert(std::has_unique_object_representations_v<RenderStateKey>);

struct PsoKey {
    std::uint64_t vs_hash;
    std::uint64_t ps_hash;
    RenderStateKey render_state;

    // Compares the same fields `hash()` mixes, and nothing else. A whole-struct
    // byte compare would also fold in `render_state.reserved`, which `hash()`
    // excludes — an equality must not be finer than its hash, or the entry becomes
    // unreachable in `published_`/`in_flight_`/the frame memo.
    [[nodiscard]] bool operator==(const PsoKey& other) const noexcept {
        return vs_hash == other.vs_hash &&
            ps_hash == other.ps_hash &&
            render_state == other.render_state;
    }
    // Field-wise fold instead of a byte walk over sizeof(*this). `reserved` is
    // always zero on every construction path — including disk-loaded persisted
    // keys, which valid_pso_key() rejects unless every reserved byte is zero —
    // so this partitions exactly the same equivalence classes as the defaulted
    // operator== while replacing ~32 dependent multiply-xors with a handful of
    // independent ones. Callers that need the value more than once per request
    // should hold a MemoizedPsoKey instead of re-deriving it.
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
                 << 16));
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

// A PsoKey plus its hash, kept together so the hot lookup path never re-derives
// hash() from the same key more than once. Equal keys always produce equal
// hashes, so a stale memo can only ever be wrong if the key it wraps was
// mutated after memoization; the only writer (GxBackend::flush_draw_state)
// assigns the whole struct and then re-memoizes.
struct MemoizedPsoKey {
    PsoKey key{};
    std::uint64_t hash = key.hash();

    MemoizedPsoKey() = default;
    explicit MemoizedPsoKey(const PsoKey& value)
        : key(value), hash(value.hash()) {}
    // Same result as the constructor above, for callers that already had to
    // compute the hash. PipelineCache::get's one-entry memo compares hashes
    // before keys, so it holds the value before the probe is built. Two
    // constructors rather than one optional argument so every call site states
    // which it means and a mismatched hash cannot be passed silently.
    MemoizedPsoKey(const PsoKey& value, std::uint64_t precomputed_hash)
        : key(value), hash(precomputed_hash) {}

    void assign(const PsoKey& value) {
        key = value;
        hash = value.hash();
    }

    // Equality is decided by the key alone. hash is a pure function of key, so
    // including it could not change the outcome; excluding it keeps a
    // default-constructed probe comparable against a populated entry.
    [[nodiscard]] bool operator==(const MemoizedPsoKey& other) const {
        return key == other.key;
    }
};

struct MemoizedPsoKeyHasher {
    [[nodiscard]] std::size_t operator()(const MemoizedPsoKey& value) const {
        return static_cast<std::size_t>(value.hash);
    }
};

// Key builders — the only sanctioned construction path outside tests.  Each
// applies the canonicalization rule against the current register state.
[[nodiscard]] PixelShaderKey build_pixel_shader_key(
    const GxState& state,
    unsigned efb_scale);
[[nodiscard]] VertexShaderKey build_vertex_shader_key(const GxState& state);
[[nodiscard]] RenderStateKey build_render_state_key(const GxState& state);

}  // namespace galaxy::gx
