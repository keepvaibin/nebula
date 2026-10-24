#pragma once

// GX register file (CP, XF, BP) with decoded views and dirty tracking. Written by FifoParser,
// read by GxBackend.

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

    // LOAD_CP_REG (opcode 0x08).
    void load_cp(std::uint8_t reg, std::uint32_t value);

    // LOAD_XF_REG (opcode 0x10): `count` consecutive words starting at `base`.
    void load_xf(
        std::uint16_t base,
        const std::uint32_t* values,
        std::uint16_t count);

    // LOAD_INDX A-D (opcodes 0x20-0x38): XF write of an array element the parser read.
    // `source` = array slot << 16 | element index, which names the matrix in the load
    // signature (the array base alternates between buffers).
    void load_xf_indexed(
        std::uint16_t xf_addr,
        const std::uint32_t* values,
        std::uint16_t count,
        std::uint32_t source = 0u);

    // LOAD_BP_REG (opcode 0x61): payload = register(31:24) | value(23:0). A BP 0xFE write
    // mask applies to the next write only.
    void load_bp(std::uint32_t command);

    [[nodiscard]] std::uint32_t cp(std::uint8_t reg) const {
        return cp_[reg];
    }
    [[nodiscard]] std::uint32_t xf(std::uint16_t addr) const {
        return xf_[addr & 0x1FFFu];
    }
    [[nodiscard]] std::uint32_t bp(std::uint8_t reg) const {
        return bp_[reg];
    }
    // XF memory (0x2000 words), for bulk palette copies.
    [[nodiscard]] const std::uint32_t* xf_raw() const {
        return xf_.data();
    }

    // XF matrix palette write tracking. GxBackend::flush_draw_state keeps the previous palette
    // snapshot while nothing in it changed; otherwise it copies all 6656 bytes
    // (xf_raw()[0 .. kMatrixPaletteWords)) into a fresh ring allocation, which has no prior
    // contents. Chunks are 32 words (128 bytes), a whole number of the float4s the shader
    // indexes (mtx_palette[base + (w >> 2)]), so no chunk boundary splits an element.
    static constexpr std::uint32_t kMatrixPaletteWords = 0x0680u;  // XF 0x0000-0x067F
    static constexpr std::uint32_t kMatrixPaletteChunkWords = 32u;
    static constexpr std::uint32_t kMatrixPaletteChunkCount =
        kMatrixPaletteWords / kMatrixPaletteChunkWords;  // 52

    // One bit per written chunk, set by XF writes and cleared by the consumer after its
    // snapshot, both on the thread that owns the register file. Starts clear: the consumer
    // tracks separately which chunks its snapshot holds.
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

    // Hash of the registers that decide which guest memory draws read; cached until a CP or BP
    // write changes one of them.
    [[nodiscard]] std::uint64_t dependency_shape_hash() const noexcept;
    // The words hashed above. A cache shared across GxState instances must compare these after
    // a hash match.
    using DependencyShapeWords = std::array<std::uint32_t, 143>;
    [[nodiscard]] const DependencyShapeWords& dependency_shape_words() const noexcept {
        (void)dependency_shape_hash();
        return dependency_shape_words_;
    }

    // Advances whenever the dependency shape may have changed. Meaningful only within one
    // instance: copies can share a revision with different registers. After wrapping it stays
    // zero, which callers treat as changed on every query.
    [[nodiscard]] std::uint64_t dependency_shape_revision() const noexcept {
        return dependency_shape_revision_;
    }

    // Parser input for the next BP write; not part of current draw shape.
    [[nodiscard]] std::uint32_t pending_bp_write_mask() const noexcept {
        return bp_mask_;
    }

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
    // The scissor box in EFB pixels (640 x 528) with the scissor offset applied.
    [[nodiscard]] EfbScissor efb_scissor() const {
        return galaxy::gx::efb_scissor(
            scissor(), decode_scissor_offset(bp_[bp::kScissorOffset]), 640, 528);
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

    // TEV registers 0xE0-0xE7: RA/BG pairs. Bit 23 of a write selects the konst color bank
    // instead of the blending registers, so both banks are kept: PREV, REG0-REG2 and K0-K3.
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

    [[nodiscard]] std::uint32_t consume_dirty() {
        const std::uint32_t d = dirty_;
        dirty_ = 0;
        return d;
    }
    void mark_all_dirty() { dirty_ = ~0u; }

    // LOAD_XF_REG / LOAD_INDX commands that wrote matrix or light memory
    // (below 0x1000), changed or not. The same object loads the same sequence
    // every frame, so a count since its draw-identity marker names each of its
    // transform states.
    [[nodiscard]] std::uint32_t matrix_write_count() const noexcept { return matrix_writes_; }
    // Load signature: a hash of the destinations (and, for LOAD_INDX, the
    // array slot and element) of those writes since the last reset. Two
    // records with the same count but a different sequence of loads differ
    // here. Updated only while tracking is on (draw-identity scenes).
    void set_matrix_load_tracking(bool on) noexcept { track_matrix_loads_ = on; }
    void reset_matrix_load_signature() noexcept { matrix_load_signature_ = kMatrixLoadSignatureSeed; }
    [[nodiscard]] std::uint64_t matrix_load_signature() const noexcept { return matrix_load_signature_; }
    static constexpr std::uint64_t kMatrixLoadSignatureSeed = 0x6D6C6F6164736967ull;

private:
    void note_matrix_load(std::uint16_t base, std::uint16_t count, std::uint32_t source) noexcept {
        std::uint64_t h = matrix_load_signature_ ^
            ((static_cast<std::uint64_t>(source) << 32u) | (static_cast<std::uint64_t>(count) << 16u) | base);
        h *= 0x9E3779B97F4A7C15ull;
        matrix_load_signature_ = h ^ (h >> 29u);
    }
    bool track_matrix_loads_ = false;
    std::uint64_t matrix_load_signature_ = kMatrixLoadSignatureSeed;
    friend class GxBackendMemorySnapshotTestAccess;
    void invalidate_dependency_shape() noexcept {
        dependency_shape_hash_valid_ = false;
        if (dependency_shape_revision_ != 0u) ++dependency_shape_revision_;
    }
    // Writes a validated transfer and sets its dirty bits; private because it checks nothing.
    void apply_validated_xf(std::uint16_t base, const std::uint32_t* values,
        std::uint16_t count);
    // A transfer with unknown addresses: applies only its known words.
    void apply_known_xf(std::uint16_t base, const std::uint32_t* values,
        std::uint16_t count);
    std::array<std::uint32_t, 0x100> cp_{};
    std::array<std::uint32_t, 0x2000> xf_{};
    std::array<std::uint32_t, 0x100> bp_{};
    // BP read/modify/write latch shared by the normal and konst TEV banks.
    std::array<std::uint32_t, 8> tev_ra_bg_latch_{};
    // Konstant color bank shadowed from BP 0xE0-0xE7 writes with bit 23 set.
    std::array<std::uint32_t, 8> konst_ra_bg_{};
    // Write mask armed by BP 0xFE for the next BP write only.
    std::uint32_t bp_mask_ = 0x00FFFFFFu;
    std::uint32_t dirty_ = ~0u;
    // Palette chunks written since the consumer's last snapshot (see xf_palette_dirty()).
    std::uint64_t xf_palette_dirty_ = 0u;
    std::uint32_t matrix_writes_ = 0u;
    mutable std::uint64_t dependency_shape_hash_ = 0u;
    mutable DependencyShapeWords dependency_shape_words_{};
    mutable bool dependency_shape_hash_valid_ = false;
    std::uint64_t dependency_shape_revision_ = 1u;
};

// Whether a CP, XF or BP write the state does not model, or a TMEM load, throws GxFatalError.
// By default it is skipped (its length is known, so the stream stays in step) and logged once
// per register; tests make it fatal with set_unknown_gx_commands_fatal(true).
[[nodiscard]] bool unknown_gx_commands_fatal() noexcept;

void set_unknown_gx_commands_fatal(bool fatal) noexcept;
}  // namespace galaxy::gx
