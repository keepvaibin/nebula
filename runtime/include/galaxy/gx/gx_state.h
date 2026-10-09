#pragma once

// GxState: the full emulated GX register file (CP / XF / BP) plus decoded
// views and dirty tracking.  Mutated only by FifoParser register loads;
// read by GxBackend to build shader keys, render state, and constants.
//
// No Windows/D3D12 includes — unit-testable offline.

#include "galaxy/gx/gx_bitfields.h"

#include <array>
#include <cstdint>

namespace galaxy::gx {

class GxState {
public:
    // Dirty bits accumulate across register loads and are consumed once per
    // draw to decide which keys/constants need rebuilding.
    enum DirtyBits : std::uint32_t {
        kDirtyTev = 1u << 0,          // TEV env/order/ksel/gen_mode stage count
        kDirtyRenderState = 1u << 1,  // blend / zmode / cull / scissor / pixfmt
        kDirtyTex = 1u << 2,          // tex image / mode / tlut registers
        kDirtyXfShader = 1u << 3,     // texgen / channel control (VS key)
        kDirtyXfMatrices = 1u << 4,   // matrix memory (palette re-upload)
        kDirtyVcd = 1u << 5,          // VCD/VAT vertex layout
        kDirtyTevConstants = 1u << 6, // TEV regs / konst / fog / alpha refs
        kDirtyVsConstants = 1u << 7,  // projection / channels / matrix indices
    };

    // --- Mutators (called by FifoParser) -----------------------------------

    // LOAD_CP_REG (opcode 0x08).
    void load_cp(std::uint8_t reg, std::uint32_t value);

    // LOAD_XF_REG (opcode 0x10): `count` consecutive words starting at `base`.
    void load_xf(
        std::uint16_t base,
        const std::uint32_t* values,
        std::uint16_t count);

    // LOAD_INDX A-D (opcodes 0x20-0x38): indexed XF write resolved by the
    // parser from the guest-side array (SMG matrix skinning path).
    void load_xf_indexed(
        std::uint16_t xf_addr,
        const std::uint32_t* values,
        std::uint16_t count);

    // LOAD_BP_REG (opcode 0x61): payload = register(31:24) | value(23:0).
    // Honors the one-shot BP 0xFE write mask for the immediately following
    // write, per hardware semantics.
    void load_bp(std::uint32_t command);

    // --- Raw access ---------------------------------------------------------

    [[nodiscard]] std::uint32_t cp(std::uint8_t reg) const {
        return cp_[reg];
    }
    [[nodiscard]] std::uint32_t xf(std::uint16_t addr) const {
        return xf_[addr & 0x1FFFu];
    }
    [[nodiscard]] std::uint32_t bp(std::uint8_t reg) const {
        return bp_[reg];
    }
    // Raw XF memory base (0x2000 words) — used for bulk palette snapshots
    // (memcpy instead of a per-word loop on the per-draw hot path).
    [[nodiscard]] const std::uint32_t* xf_raw() const {
        return xf_.data();
    }

    // --- XF matrix-palette write tracking -----------------------------------
    //
    // Tracks which parts of the XF matrix palette the guest has written since
    // the renderer last took a snapshot of it. `GxBackend::flush_draw_state`
    // consumes this as a single question — *did anything in the palette move?* —
    // and the answer decides whether the frame's existing GPU ring allocation
    // for the palette can be reused or a fresh one is needed.
    //
    // Be precise about what this buys, because an earlier version of this
    // comment claimed more: the snapshot itself is always the **whole** 6656
    // bytes (`xf_raw()[0 .. kMatrixPaletteWords)`). A fresh ring allocation has
    // no inherited contents, so its head and tail have to be written even when
    // only one 4-row matrix changed. The saving is the *allocation*, not the
    // copy: SMG re-loads matrices per object via LOAD_INDX, but a draw whose
    // matrices did not move can keep the previous snapshot's GPU address and
    // skip both the allocation and the memcpy.
    //
    // Granularity is 32 words: 128 bytes, which is a whole number of the 16-byte
    // float4s the shader indexes (`mtx_palette[base + (w >> 2)]`), so a chunk
    // boundary can never split an element the shader reads. That property is
    // what keeps this safe to extend to a span-limited copy later if one is ever
    // justified; nothing in the tree copies a partial span today.
    static constexpr std::uint32_t kMatrixPaletteWords = 0x0680u;  // XF 0x0000-0x067F
    static constexpr std::uint32_t kMatrixPaletteChunkWords = 32u;
    static constexpr std::uint32_t kMatrixPaletteChunkCount =
        kMatrixPaletteWords / kMatrixPaletteChunkWords;  // 52

    // One bit per touched chunk. Re-armed per mutation and cleared by the
    // consumer once it has snapshotted the palette; both run on the same thread
    // that mutates the register file, so the bits need no atomics.
    // Default-constructed (all zero) is the correct initial state: nothing has
    // been snapshotted yet, so every chunk is unknown and a snapshot is owed.
    [[nodiscard]] constexpr std::uint64_t xf_palette_dirty() const noexcept {
        return xf_palette_dirty_;
    }
    static constexpr void clear_xf_palette_bits(
        std::uint64_t& mask, std::uint32_t first, std::uint32_t last) noexcept {
        const std::uint64_t span =
            last + 1u >= 64u
                ? ~std::uint64_t{0}
                : ((std::uint64_t{1} << (last + 1u)) - 1u);
        const std::uint64_t below =
            first >= 64u
                ? ~std::uint64_t{0}
                : ((std::uint64_t{1} << first) - 1u);
        mask &= ~(span & ~below);
    }
    void clear_xf_palette_dirty(std::uint32_t first, std::uint32_t last) noexcept {
        clear_xf_palette_bits(xf_palette_dirty_, first, last);
    }

    // Exact dependency-key hash. Cached with this parser-owned register file;
    // copies retain both banks and cache, CP/BP mutations invalidate it.
    [[nodiscard]] std::uint64_t dependency_shape_hash() const noexcept;
    // Exactly the normalized words hashed above. Cross-owner caches must
    // compare these words after their hash admission; a hash is not identity.
    using DependencyShapeWords = std::array<std::uint32_t, 143>;
    [[nodiscard]] const DependencyShapeWords& dependency_shape_words() const noexcept {
        (void)dependency_shape_hash();
        return dependency_shape_words_;
    }

    // Local reuse while this exact register owner is mutated by its parser.
    // Copies/assignments can carry equal revisions for different banks: never
    // use this as a cross-owner or persistent cache key. Zero is saturated and
    // requires recomputation on every query, avoiding generation wrap reuse.
    [[nodiscard]] std::uint64_t dependency_shape_revision() const noexcept {
        return dependency_shape_revision_;
    }

    // Parser input for the next BP write; not part of current draw shape.
    [[nodiscard]] std::uint32_t pending_bp_write_mask() const noexcept {
        return bp_mask_;
    }

    // --- Decoded views ------------------------------------------------------

    [[nodiscard]] GenMode gen_mode() const {
        return decode_gen_mode(bp_[bp::kGenMode]);
    }
    [[nodiscard]] ZMode z_mode() const {
        return decode_z_mode(bp_[bp::kZMode]);
    }
    [[nodiscard]] BlendMode blend_mode() const {
        return decode_blend(bp_[bp::kBlendMode], bp_[bp::kConstAlpha]);
    }
    [[nodiscard]] PeControl pe_control() const {
        return decode_pe_control(bp_[bp::kPeControl]);
    }
    [[nodiscard]] ScissorRect scissor() const {
        return decode_scissor(
            bp_[bp::kScissorTopLeft], bp_[bp::kScissorBottomRight]);
    }
    [[nodiscard]] AlphaCompare alpha_compare() const {
        return decode_alpha_compare(bp_[bp::kAlphaCompare]);
    }
    [[nodiscard]] FogParams fog() const {
        return decode_fog(
            bp_[bp::kFogParam0], bp_[bp::kFogParam1], bp_[bp::kFogParam2],
            bp_[bp::kFogParam3], bp_[bp::kFogColor]);
    }

    [[nodiscard]] TevOrder tev_order(unsigned stage) const {
        return decode_tev_order(
            bp_[bp::kTevOrderBase + (stage / 2)], stage & 1u);
    }
    [[nodiscard]] TevKSel tev_ksel(unsigned pair) const {
        return decode_tev_ksel(bp_[bp::kTevKSelBase + pair]);
    }

    // Assembles the complete per-stage configuration from the color/alpha
    // env, TREF half, and KSEL half registers.
    [[nodiscard]] TevStageConfig tev_stage(unsigned stage) const;

    // TEV registers 0xE0-0xE7: RA/BG pairs.  Bit 23 of the RA word routes the
    // write to the konstant color bank instead of the blending registers, so
    // both banks are tracked; index 0 = PREV/REG0..2, konst index 0..3.
    [[nodiscard]] std::array<std::array<std::int32_t, 4>, 4>
        tev_register_values() const;
    [[nodiscard]] std::array<std::uint32_t, 4> tev_register_colors() const;
    [[nodiscard]] std::array<std::uint32_t, 4> tev_konst_colors() const;

    [[nodiscard]] TexImage tex_image(unsigned map) const;
    [[nodiscard]] TexMode tex_mode(unsigned map) const;
    [[nodiscard]] TlutRef tex_tlut(unsigned map) const;

    // Snapshot of the latched copy registers combined with the PE_COPY_EXECUTE
    // command value that triggered the copy.
    [[nodiscard]] EfbCopyParams efb_copy(std::uint32_t exec_command) const {
        EfbCopyParams p = decode_efb_copy(
            bp_[bp::kCopySrcTopLeft], bp_[bp::kCopySrcSize],
            bp_[bp::kCopyDestAddr], bp_[bp::kCopyDestStride],
            bp_[bp::kCopyYScale], bp_[bp::kCopyClearAR],
            bp_[bp::kCopyClearGB], bp_[bp::kCopyClearZ], exec_command);
        p.filter0_raw = bp_[bp::kCopyFilter0];
        p.filter1_raw = bp_[bp::kCopyFilter1];
        return p;
    }

    [[nodiscard]] Projection projection() const;
    [[nodiscard]] XfTexGen tex_gen(unsigned index) const {
        return decode_xf_texgen(xf_[xf::kTexGenBase + index]);
    }
    [[nodiscard]] XfPostTexGen post_tex_gen(unsigned index) const {
        return decode_xf_post_texgen(xf_[xf::kPostTexGenBase + index]);
    }
    [[nodiscard]] XfChannelCtrl channel_ctrl(unsigned channel) const {
        return decode_xf_channel_ctrl(xf_[xf::kChannelCtrlBase + channel]);
    }

    [[nodiscard]] VertexDescriptor vertex_desc(std::uint8_t vtxfmt) const {
        return decode_vertex_descriptor(
            cp_[cp::kVcdLo], cp_[cp::kVcdHi],
            cp_[cp::kVatABase + vtxfmt], cp_[cp::kVatBBase + vtxfmt],
            cp_[cp::kVatCBase + vtxfmt]);
    }
    [[nodiscard]] std::uint32_t array_base(unsigned attr) const {
        return cp_[cp::kArrayBaseBase + attr];
    }
    [[nodiscard]] std::uint32_t array_stride(unsigned attr) const {
        return cp_[cp::kArrayStrideBase + attr];
    }

    // --- Dirty tracking -----------------------------------------------------

    [[nodiscard]] std::uint32_t consume_dirty() {
        const std::uint32_t d = dirty_;
        dirty_ = 0;
        return d;
    }
    void mark_all_dirty() { dirty_ = ~0u; }

private:
    friend class GxBackendMemorySnapshotTestAccess;
    void invalidate_dependency_shape() noexcept {
        dependency_shape_hash_valid_ = false;
        if (dependency_shape_revision_ != 0u) ++dependency_shape_revision_;
    }
    // Both public XF mutators validate the entire transfer before this shared
    // write/dirty pass. Keep unchecked application inaccessible to callers.
    void apply_validated_xf(std::uint16_t base, const std::uint32_t* values,
        std::uint16_t count);
    std::array<std::uint32_t, 0x100> cp_{};
    std::array<std::uint32_t, 0x2000> xf_{};
    std::array<std::uint32_t, 0x100> bp_{};
    // BP read/modify/write latch shared by the normal and konst TEV banks.
    std::array<std::uint32_t, 8> tev_ra_bg_latch_{};
    // Konstant color bank shadowed from BP 0xE0-0xE7 writes with bit 23 set.
    std::array<std::uint32_t, 8> konst_ra_bg_{};
    // One-shot write mask armed by BP 0xFE, applied to the next
    // BP write only.
    std::uint32_t bp_mask_ = 0x00FFFFFFu;
    std::uint32_t dirty_ = ~0u;
    // Bits 0..kMatrixPaletteChunkCount-1: chunks of the XF matrix palette the
    // guest has written since the last consumer snapshot. See
    // xf_palette_dirty() for what the consumer does with them.
    std::uint64_t xf_palette_dirty_ = 0u;
    mutable std::uint64_t dependency_shape_hash_ = 0u;
    mutable DependencyShapeWords dependency_shape_words_{};
    mutable bool dependency_shape_hash_valid_ = false;
    std::uint64_t dependency_shape_revision_ = 1u;
};

}  // namespace galaxy::gx
