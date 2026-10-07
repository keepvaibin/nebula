// gx_state.cpp — GxState register file and decoded views.
//
// Mutators are called exclusively by FifoParser; consumers are GxBackend
// methods that build shader keys, pipeline state, and constant buffers.
//
// Unknown CP/XF/BP state writes are fatal. A self-delimiting command is still
// an unsupported state transition; accepting it would continue with stale or
// incomplete rendering state.

#include "galaxy/gx/gx_state.h"

#include "galaxy/gx/fifo_parser.h"
#include "galaxy/gx/uber_constants.h"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstring>

namespace galaxy::gx {

namespace {

[[noreturn]] void throw_unknown_cp_write(
    std::uint8_t reg,
    std::uint32_t value) {
    char message[192]{};
    std::snprintf(
        message,
        sizeof(message),
        "[GxState] unknown CP state write: opcode=0x%02X "
        "register=0x%02X value=0x%08X fifo-offset=unavailable",
        static_cast<unsigned>(op::kLoadCpReg),
        static_cast<unsigned>(reg),
        static_cast<unsigned>(value));
    throw GxFatalError(message, 0u, op::kLoadCpReg);
}

[[noreturn]] void throw_unknown_xf_write(
    std::uint8_t opcode,
    std::uint16_t base,
    std::uint16_t count,
    std::uint32_t address,
    std::uint32_t value) {
    char message[256]{};
    std::snprintf(
        message,
        sizeof(message),
        "[GxState] unknown XF state write: opcode=0x%02X "
        "register=0x%04X value=0x%08X transfer-base=0x%04X "
        "transfer-count=%u fifo-offset=unavailable",
        static_cast<unsigned>(opcode),
        static_cast<unsigned>(address),
        static_cast<unsigned>(value),
        static_cast<unsigned>(base),
        static_cast<unsigned>(count));
    throw GxFatalError(message, 0u, opcode);
}

[[noreturn]] void throw_unknown_bp_write(
    std::uint8_t reg,
    std::uint32_t value,
    std::uint32_t command) {
    char message[224]{};
    std::snprintf(
        message,
        sizeof(message),
        "[GxState] unknown BP state write: opcode=0x%02X "
        "register=0x%02X value=0x%06X command=0x%08X "
        "fifo-offset=unavailable",
        static_cast<unsigned>(op::kLoadBpReg),
        static_cast<unsigned>(reg),
        static_cast<unsigned>(value),
        static_cast<unsigned>(command));
    throw GxFatalError(message, 0u, op::kLoadBpReg);
}

[[nodiscard]] constexpr bool in_u16_range(
    std::uint16_t value,
    std::uint16_t begin,
    std::uint16_t end) noexcept {
    return value >= begin && value < end;
}

[[nodiscard]] constexpr bool is_classified_xf_address(
    std::uint16_t address) noexcept {
    // Matrix/light memory ranges consumed by the native vertex path.
    if (in_u16_range(address, xf::kPosMatricesBase, 0x0100u) ||
        in_u16_range(
            address,
            xf::kNormalMatricesBase,
            static_cast<std::uint16_t>(xf::kNormalMatricesBase + 0x0060u)) ||
        in_u16_range(
            address,
            xf::kPostMatricesBase,
            static_cast<std::uint16_t>(xf::kPostMatricesBase + 0x0100u)) ||
        in_u16_range(
            address,
            xf::kLightsBase,
            static_cast<std::uint16_t>(xf::kLightsBase + 0x0080u))) {
        return true;
    }

    // XFMEM_ERROR..XFMEM_VTXSPECS (0x1000-0x1008): real-hardware status/
    // diagnostic registers with no rendering-state effect (see
    // xf::kStatusRegsBase). Classified as inert, not stubbed: stored below
    // but deliberately left out of every dirty_ bit below.
    if (in_u16_range(
            address, xf::kStatusRegsBase, xf::kNumChannels)) {
        return true;
    }

    // Register ranges with native consumers and dirty-state handling below.
    return in_u16_range(address, xf::kNumChannels, 0x1013u) ||
        in_u16_range(
            address, xf::kUnknownGroup1Base, xf::kViewportBase) ||
        in_u16_range(address, xf::kViewportBase, 0x1020u) ||
        in_u16_range(address, xf::kProjectionBase, 0x1027u) ||
        address == xf::kNumTexGens ||
        in_u16_range(address, xf::kTexGenBase, 0x1048u) ||
        in_u16_range(address, xf::kPostTexGenBase, 0x1058u);
}

void validate_xf_write(
    std::uint8_t opcode,
    std::uint16_t base,
    const std::uint32_t* values,
    std::uint16_t count) {
    // Validate the complete transaction before touching xf_ or dirty_.  This
    // also rejects the old 0x1FFF address wraparound behavior.
    const std::uint32_t end = static_cast<std::uint32_t>(base) + count;
    static_assert(xf::kPostMatricesBase + 0x0100u == xf::kLightsBase);
    // The post-matrix and light banks are adjacent and equally classified.
    // Most matrix uploads can validate their entire span without revisiting
    // the same range checks for every word. Keep the scalar error path for
    // mixed/invalid spans so it still identifies the first offending word.
    if (end <= 0x0100u ||
        (base >= xf::kNormalMatricesBase &&
         end <= xf::kNormalMatricesBase + 0x0060u) ||
        (base >= xf::kPostMatricesBase &&
         end <= xf::kLightsBase + 0x0080u)) {
        return;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t address = static_cast<std::uint32_t>(base) + i;
        if (address > 0xFFFFu ||
            !is_classified_xf_address(static_cast<std::uint16_t>(address))) {
            throw_unknown_xf_write(
                opcode,
                base,
                count,
                address,
                values[i]);
        }
    }
}

[[nodiscard]] constexpr bool is_classified_bp_register(
    std::uint8_t reg) noexcept {
    // These ranges exactly mirror the functional or explicitly retained raw
    // cases in load_bp below. Holes are intentionally absent.
    return reg <= 0x04u ||
        (reg >= bp::kIndMtxBase && reg < bp::kIndMtxBase + bp::kIndMtxCount) ||
        reg == bp::kIndMask ||
        (reg >= bp::kIndCmdBase && reg < bp::kIndCmdBase + 0x10u) ||
        (reg >= bp::kScissorTopLeft && reg <= bp::kSuLpSize) ||
        (reg >= bp::kPerf0TriBase && reg < bp::kPerf0TriBase + 2u) ||
        (reg >= bp::kRas1Ss0 && reg <= 0x59u) ||
        (reg >= bp::kTlutSrcAddr && reg <= bp::kBusClock1) ||
        (reg >= 0x80u && reg <= 0xFDu) ||
        reg == bp::kBpMask;
}

// Keep this predicate paired with dependency_shape_hash(). These are its
// complete BP inputs, not the larger set affecting visual state. Uniform,
// blend, scissor and copy writes must still execute, but cannot change the
// cached hash of guest-memory dependency shape.
[[nodiscard]] constexpr bool affects_dependency_shape(std::uint8_t reg) noexcept {
    if (reg == bp::kGenMode || reg == bp::kIndRef ||
        reg == bp::kTlutSrcAddr || reg == bp::kTlutDest ||
        (reg >= bp::kTevOrderBase && reg < bp::kTevOrderBase + 8u) ||
        (reg >= bp::kIndCmdBase && reg < bp::kIndCmdBase + kMaxTevStages)) {
        return true;
    }
    // Texture banks are 0x80..0x9f and 0xa0..0xbf, four maps per bank.
    if (reg < bp::kTexMode0Base || reg >= bp::kTexMode0Base + 0x40u) {
        return false;
    }
    const unsigned offset = (reg - bp::kTexMode0Base) & 0x1fu;
    return offset < 12u || (offset >= 20u && offset < 28u);
}

}  // namespace

// ---------------------------------------------------------------------------
// LOAD_CP_REG (opcode 0x08)
// ---------------------------------------------------------------------------
void GxState::load_cp(std::uint8_t reg, std::uint32_t value) {
    std::uint32_t dirty = 0u;
    if (reg == cp::kUnknown00 || reg == cp::kUnknown10 ||
        reg == cp::kUnknown20) {
        dirty = 0u;
    } else if (reg == cp::kMatrixIndexA || reg == cp::kMatrixIndexB) {
        dirty = kDirtyVsConstants;
    } else if (reg == cp::kVcdLo || reg == cp::kVcdHi ||
               (reg >= cp::kVatABase && reg < cp::kVatABase + 8u) ||
               (reg >= cp::kVatBBase && reg < cp::kVatBBase + 8u) ||
               (reg >= cp::kVatCBase && reg < cp::kVatCBase + 8u) ||
               (reg >= cp::kArrayBaseBase &&
                reg < cp::kArrayBaseBase + 16u) ||
               (reg >= cp::kArrayStrideBase &&
                reg < cp::kArrayStrideBase + 16u)) {
        dirty = kDirtyVcd;
    } else {
        throw_unknown_cp_write(reg, value);
    }

    if (cp_[reg] != value) {
        cp_[reg] = value;
        if ((dirty & kDirtyVcd) != 0u) {
            invalidate_dependency_shape();
        }
        dirty_ |= dirty;
    }
}

// ---------------------------------------------------------------------------
// LOAD_XF_REG (opcode 0x10)
// ---------------------------------------------------------------------------
void GxState::load_xf(
    std::uint16_t base,
    const std::uint32_t* values,
    std::uint16_t count) {
    validate_xf_write(op::kLoadXfReg, base, values, count);
    apply_validated_xf(base, values, count);
}

void GxState::apply_validated_xf(
    std::uint16_t base,
    const std::uint32_t* values,
    std::uint16_t count) {
    // Complete validation guarantees every accepted low-bank address is
    // matrix/light memory. Its only dirty effect is the matrix palette bit.
    // Preserve sequential reads/stores: values may alias this register bank.
    if (base < xf::kStatusRegsBase) {
        bool changed = false;
        for (std::uint16_t i = 0; i < count; ++i) {
            const std::uint16_t addr = static_cast<std::uint16_t>(base + i);
            if (xf_[addr] != values[i]) {
                xf_[addr] = values[i];
                changed = true;
            }
        }
        if (changed) dirty_ |= kDirtyXfMatrices;
        return;
    }
    for (std::uint16_t i = 0; i < count; ++i) {
        const std::uint16_t addr =
            static_cast<std::uint16_t>(base + i);
        if (xf_[addr] == values[i]) {
            continue;
        }
        xf_[addr] = values[i];

        // XF misc scalars: num channels, ambient/material colors
        // 0x1009-0x100D
        if (addr >= xf::kNumChannels &&
            addr <= xf::kMaterialColorBase + 1u) {
            dirty_ |= kDirtyVsConstants;
            if (addr == xf::kNumChannels) {
                dirty_ |= kDirtyXfShader;
            }
        }
        // Channel control  0x100E-0x1011  (4 registers: color0/1, alpha0/1)
        if (addr >= xf::kChannelCtrlBase &&
            addr < xf::kChannelCtrlBase + 4u) {
            dirty_ |= kDirtyXfShader | kDirtyVsConstants;
        }
        // Dual texture transform enable changes VS code generation.
        if (addr == xf::kDualTexTrans) {
            dirty_ |= kDirtyXfShader;
        }
        // Projection  0x1020-0x1026
        if (addr >= xf::kProjectionBase && addr < xf::kProjectionBase + 7u) {
            dirty_ |= kDirtyVsConstants;
        }
        // XF viewport width/height drive native line/point expansion. The
        // renderer consumes all six words dynamically, while b0 must refresh
        // when the first two change so raster widths remain exact.
        if (addr >= xf::kViewportBase && addr < xf::kViewportBase + 2u) {
            dirty_ |= kDirtyVsConstants;
        }
        // NumTexGens 0x103F  +  TexGen  0x1040-0x1047  +  PostTexGen  0x1050-0x1057
        if (addr == xf::kNumTexGens ||
            (addr >= xf::kTexGenBase && addr < xf::kTexGenBase + 8u) ||
            (addr >= xf::kPostTexGenBase && addr < xf::kPostTexGenBase + 8u)) {
            dirty_ |= kDirtyXfShader | kDirtyVsConstants;
        }
    }
}

// ---------------------------------------------------------------------------
// LOAD_INDX A-D (opcodes 0x20-0x38): indexed XF write
// ---------------------------------------------------------------------------
void GxState::load_xf_indexed(
    std::uint16_t xf_addr,
    const std::uint32_t* values,
    std::uint16_t count) {
    // This mutator does not receive the A-D selector; kLoadIndxA identifies
    // the LOAD_INDX opcode family in fatal metadata.
    validate_xf_write(op::kLoadIndxA, xf_addr, values, count);
    // Already validated with the indexed opcode's exact failure metadata.
    // Reuse application without a second complete classification pass.
    apply_validated_xf(xf_addr, values, count);
}

// ---------------------------------------------------------------------------
// LOAD_BP_REG (opcode 0x61)
// ---------------------------------------------------------------------------
void GxState::load_bp(std::uint32_t command) {
    const auto reg = static_cast<std::uint8_t>(command >> 24);
    const std::uint32_t val = command & 0x00FFFFFFu;

    // Reject before applying/resetting the one-shot mask or mutating any
    // register bank. Continuing with stale state is never a valid fallback.
    if (!is_classified_bp_register(reg)) {
        throw_unknown_bp_write(reg, val, command);
    }

    // 0xFE — one-shot write mask register (arms but does not write to bp_[]).
    if (reg == bp::kBpMask) {
        bp_mask_ = val;
        return;
    }

    // Apply the current write mask.
    // TEV color registers 0xE0-0xE7: data bit 23 routes the write to the
    // KONSTANT bank INSTEAD of the blending register (hardware semantics;
    // Dolphin BPStructs does the same).  Previously konst writes ALSO landed
    // in bp_[], so every J3D TevKColor write clobbered C0/C1/A0 — the
    // blended 3D materials (out = lerp(C1, C0, TEXC), a = A0*TEXA) then
    // produced transparent garbage.
    if (reg >= 0xE0u && reg <= 0xE7u) {
        const std::size_t index = reg - 0xE0u;
        const std::uint32_t next =
            (tev_ra_bg_latch_[index] & ~bp_mask_) | (val & bp_mask_);
        bp_mask_ = 0x00FFFFFFu;
        tev_ra_bg_latch_[index] = next;
        auto& bank = (next & (1u << 23)) != 0u
            ? konst_ra_bg_[index] : bp_[reg];
        if (bank == next) {
            return;
        }
        bank = next;
        dirty_ |= kDirtyTevConstants;
        return;
    }

    const std::uint32_t previous = bp_[reg];
    const std::uint32_t next = (previous & ~bp_mask_) | (val & bp_mask_);
    // Reset mask to full-pass after each use.
    bp_mask_ = 0x00FFFFFFu;
    if (previous == next) {
        return;
    }
    bp_[reg] = next;
    if (affects_dependency_shape(reg)) {
        invalidate_dependency_shape();
    }

    // --------------- Dirty bit classification --------------------------------

    // Display-copy filter/sample-pattern registers (0x01-0x04) are latched
    // copy state. The current renderer's non-AA XFB path consumes BP 0x53/0x54
    // filter weights; keep these named and stored so they are not treated as
    // unknown hardware writes.
    if (reg >= bp::kDisplayCopyFilterBase &&
        reg < bp::kDisplayCopyFilterBase + 4u) {
        return;
    }
    // Indirect texture matrices 0x06-0x0E: three 2x3 offset matrices.
    if (reg >= bp::kIndMtxBase &&
        reg < bp::kIndMtxBase + bp::kIndMtxCount) {
        // Coefficients and exponent are ind_mtx[] uniforms; stage matrix
        // selection remains shader state in kIndCmdBase.
        dirty_ |= kDirtyTevConstants;
        return;
    }
    // Scissor is dynamic and read directly for every draw.
    if (reg == bp::kScissorTopLeft || reg == bp::kScissorBottomRight) {
        return;
    }
    // Line/point size is consumed by the geometry-stage tail of b0.
    if (reg == bp::kSuLpSize) {
        dirty_ |= kDirtyVsConstants;
        return;
    }
    // Perf-query triangle/quad counters: stored but never consulted.
    if (reg >= bp::kPerf0TriBase && reg < bp::kPerf0TriBase + 2u) {
        return;
    }
    // Indirect texture scale/reference.
    if (reg >= bp::kRas1Ss0 && reg <= bp::kIndRef) {
        dirty_ |= kDirtyTev;
        return;
    }
    // BP 0x00 gen_mode — but this is listed separately below.
    // Indirect texture stage commands  0x10-0x1F
    if (reg >= bp::kIndCmdBase && reg < bp::kIndCmdBase + 0x10u) {
        dirty_ |= kDirtyTev;
        return;
    }
    if (reg == bp::kIndMask) {
        return;
    }
    // TEV order (TREF)  0x28-0x2F
    if (reg >= bp::kTevOrderBase && reg < bp::kTevOrderBase + 8u) {
        dirty_ |= kDirtyTev;
        return;
    }
    // Texture-coordinate S/T sizes feed tex_dims[].zw and VS constants.
    // Neither shader key contains these sizes. Preserve constant uploads
    // without rebuilding a shader key or looking up the same pipeline.
    if (reg >= bp::kTexCoordSizeBase &&
        reg < bp::kTexCoordSizeBase + bp::kTexCoordSizeCount) {
        dirty_ |= kDirtyTevConstants | kDirtyVsConstants;
        return;
    }
    // TEV color env  0xC0-0xCF (even: color_env, odd: alpha_env, interleaved,
    // 16 stages = 32 registers)
    if (reg >= bp::kTevColorEnvBase && reg < bp::kTevColorEnvBase + 0x20u) {
        dirty_ |= kDirtyTev;
        return;
    }

    switch (reg) {
    // Gen mode
    case bp::kGenMode:
        dirty_ |= kDirtyTev | kDirtyXfShader | kDirtyRenderState |
                  kDirtyVsConstants;
        break;

    // Blend / PE
    case bp::kBlendMode:   // 0x41
        dirty_ |= kDirtyRenderState | kDirtyTev | kDirtyTevConstants;
        break;
    case bp::kConstAlpha:  // 0x42
        dirty_ |= kDirtyTevConstants;
        // The enable flag affects shader outputs and blend routing; the
        // replacement value is a uniform even while replacement is enabled.
        if (((previous ^ next) & (1u << 8u)) != 0u) {
            dirty_ |= kDirtyRenderState | kDirtyTev;
        }
        break;
    case bp::kPeControl:   // 0x43
        dirty_ |= kDirtyRenderState | kDirtyTev;
        break;
    case bp::kFieldMask:   // 0x44
        dirty_ |= kDirtyRenderState;
        break;

    // Z mode
    case bp::kZMode:       // 0x40
        dirty_ |= kDirtyRenderState;
        break;

    // PE events
    case bp::kPeDone:      // 0x45
    case bp::kBusClock0:   // 0x46
    case bp::kPeToken:     // 0x47
    case bp::kPeTokenInt:  // 0x48
        break;

    // Alpha compare  0x49 (= kCopySrcTopLeft, but that constant is 0x49 only
    // in the copy block — let's use the actual address kAlphaCompare = 0xF3)
    case bp::kAlphaCompare:  // 0xF3
        dirty_ |= kDirtyTevConstants;
        if (((previous ^ next) & 0x00ff0000u) != 0u) {
            dirty_ |= kDirtyTev;
        }
        break;

    // EFB copy block  0x49-0x54 (src rect, dest addr/stride, y-scale, clear,
    // execute, filter)
    case 0x49u: case 0x4Au: case 0x4Bu: case 0x4Cu: case 0x4Du:
    case 0x4Eu: case 0x4Fu: case 0x50u: case 0x51u: case 0x52u:
    case 0x53u: case 0x54u:
        break;
    case bp::kCopyClearBoundingBox1:  // 0x55
    case bp::kCopyClearBoundingBox2:  // 0x56
    case bp::kCopyClearPixelPerf:     // 0x57
    case bp::kRevBits:                // 0x58
        break;

    // Scissor offset  0x59
    case bp::kScissorOffset:
        break;

    // TLUT load  0x64-0x65
    case bp::kTlutSrcAddr:  // 0x64
    case bp::kTlutDest:     // 0x65
        dirty_ |= kDirtyTex;
        break;

    // GXInvalidateTexAll signal  0x66-0x67
    case bp::kTexInvalidate:
    case bp::kPerf1:
        dirty_ |= kDirtyTex;
        break;

    case bp::kFieldMode:
        dirty_ |= kDirtyTev;
        break;
    case bp::kBusClock1:
        break;

    // Texture units  maps 0-3: 0x80-0x9F, maps 4-7: 0xA0-0xBF (with
    // kTexHighBankOffset = 0x20 offset).  Covers mode0, mode1, image0-3, tlut.
    case 0x80u: case 0x81u: case 0x82u: case 0x83u:  // TexMode0 maps 0-3
    case 0x84u: case 0x85u: case 0x86u: case 0x87u:  // TexMode1 maps 0-3
    case 0x88u: case 0x89u: case 0x8Au: case 0x8Bu:  // TexImage0 maps 0-3
    case 0x8Cu: case 0x8Du: case 0x8Eu: case 0x8Fu:  // TexImage1 maps 0-3
    case 0x90u: case 0x91u: case 0x92u: case 0x93u:  // TexImage2 maps 0-3
    case 0x94u: case 0x95u: case 0x96u: case 0x97u:  // TexImage3 maps 0-3
    case 0x98u: case 0x99u: case 0x9Au: case 0x9Bu:  // TexTlut maps 0-3
    case 0x9Cu: case 0x9Du: case 0x9Eu: case 0x9Fu:  // (padding / mode2)
    // Maps 4-7 at +0x20:
    case 0xA0u: case 0xA1u: case 0xA2u: case 0xA3u:
    case 0xA4u: case 0xA5u: case 0xA6u: case 0xA7u:
    case 0xA8u: case 0xA9u: case 0xAAu: case 0xABu:
    case 0xACu: case 0xADu: case 0xAEu: case 0xAFu:
    case 0xB0u: case 0xB1u: case 0xB2u: case 0xB3u:
    case 0xB4u: case 0xB5u: case 0xB6u: case 0xB7u:
    case 0xB8u: case 0xB9u: case 0xBAu: case 0xBBu:
    case 0xBCu: case 0xBDu: case 0xBEu: case 0xBFu:
        dirty_ |= kDirtyTex;
        break;

    // TEV register colors  0xE0-0xE7 (blending-register RA/BG pairs; konst
    // writes never reach here — bit 23 routed them to konst_ra_bg_ above).
    case 0xE0u: case 0xE1u: case 0xE2u: case 0xE3u:
    case 0xE4u: case 0xE5u: case 0xE6u: case 0xE7u:
        dirty_ |= kDirtyTevConstants;
        break;

    // Fog range-adjust parameters  0xE8-0xED
    case 0xE8u: case 0xE9u: case 0xEAu: case 0xEBu: case 0xECu: case 0xEDu:
        dirty_ |= kDirtyTevConstants;
        break;

    // Fog parameters  0xEE-0xF2
    case bp::kFogParam0: case bp::kFogParam1: case bp::kFogParam2:
    case bp::kFogColor:
        dirty_ |= kDirtyTevConstants;
        break;
    case bp::kFogParam3:
        dirty_ |= kDirtyTevConstants;
        if (((previous ^ next) & 0x00e00000u) != 0u) {
            dirty_ |= kDirtyTev;
        }
        break;

    // Z env  0xF4-0xF5
    case bp::kTevZEnv0:
        dirty_ |= kDirtyTevConstants; // depth bias, not format/operation
        break;
    case bp::kTevZEnv1:
        dirty_ |= kDirtyTev;
        break;

    // TEV KSEL  0xF6-0xFD  (2 stages per register, 8 registers for 16 stages)
    case 0xF6u: case 0xF7u: case 0xF8u: case 0xF9u:
    case 0xFAu: case 0xFBu: case 0xFCu: case 0xFDu:
        dirty_ |= kDirtyTev;
        break;

    // Explicitly classified raw/no-dirty registers reach this case. Unknown
    // addresses were rejected before the write mask or register file changed.
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// tev_stage: assemble per-stage config from four BP reads.
// ---------------------------------------------------------------------------
TevStageConfig GxState::tev_stage(unsigned stage) const {
    // Color env:  BP 0xC0 + 2*stage
    const std::uint32_t color_env =
        bp_[bp::kTevColorEnvBase + 2u * stage];
    // Alpha env:  BP 0xC1 + 2*stage
    const std::uint32_t alpha_env =
        bp_[bp::kTevAlphaEnvBase + 2u * stage];
    // TREF order register: BP 0x28 + stage/2, bit-half = stage & 1
    const std::uint32_t tref_value =
        bp_[bp::kTevOrderBase + (stage / 2u)];
    const unsigned tref_half = stage & 1u;
    // KSEL: BP 0xF6 + stage/2; even/odd selects within the register.
    const TevKSel ksel =
        decode_tev_ksel(bp_[bp::kTevKSelBase + (stage / 2u)]);
    const std::uint8_t kcsel =
        ((stage & 1u) == 0u) ? ksel.kcsel_even : ksel.kcsel_odd;
    const std::uint8_t kasel =
        ((stage & 1u) == 0u) ? ksel.kasel_even : ksel.kasel_odd;

    return assemble_tev_stage(
        color_env, alpha_env,
        tref_value, tref_half,
        kcsel, kasel);
}

// ---------------------------------------------------------------------------
// TEV register / konst color banks (BP 0xE0-0xE7, RA/BG pairs).
// ---------------------------------------------------------------------------
namespace {
// Assembles one RA/BG register pair into R(8)|G(8)|B(8)|A(8).
// BP TEV color fields are 11-bit (S10 for the blending registers, plain
// 8-bit for konst): RA = red[10:0], alpha[22:12]; BG = blue[10:0],
// green[22:12].  Clamp to the displayable 0-255 range.
std::int32_t sign_extend_11(std::uint32_t v) {
    const std::int32_t raw = static_cast<std::int32_t>(v & 0x7FFu);
    return (raw & 0x400) != 0 ? raw - 0x800 : raw;
}

std::array<std::int32_t, 4> unpack_tev_srgba(
    std::uint32_t ra,
    std::uint32_t bg) {
    return {
        sign_extend_11(ra),
        sign_extend_11(bg >> 12),
        sign_extend_11(bg),
        sign_extend_11(ra >> 12),
    };
}

std::uint32_t pack_tev_rgba(std::uint32_t ra, std::uint32_t bg) {
    const auto clamp8 = [](std::uint32_t v) {
        return std::min<std::uint32_t>(v & 0x7FFu, 0xFFu);
    };
    return (clamp8(ra) << 24) |
           (clamp8(bg >> 12) << 16) |
           (clamp8(bg) << 8) |
           clamp8(ra >> 12);
}
}  // namespace

std::array<std::array<std::int32_t, 4>, 4>
GxState::tev_register_values() const {
    // TEV blending registers are signed 11-bit values. Dolphin keeps them as
    // int4 registers, masks A/B/C inputs to 8-bit, and preserves D raw.
    return {
        unpack_tev_srgba(bp_[bp::kTevRegisterBase + 0u],
                         bp_[bp::kTevRegisterBase + 1u]),
        unpack_tev_srgba(bp_[bp::kTevRegisterBase + 2u],
                         bp_[bp::kTevRegisterBase + 3u]),
        unpack_tev_srgba(bp_[bp::kTevRegisterBase + 4u],
                         bp_[bp::kTevRegisterBase + 5u]),
        unpack_tev_srgba(bp_[bp::kTevRegisterBase + 6u],
                         bp_[bp::kTevRegisterBase + 7u]),
    };
}

std::array<std::uint32_t, 4> GxState::tev_register_colors() const {
    // The blending registers live in pairs: 0xE0/0xE1 = PREV-RA/BG,
    // 0xE2/0xE3 = REG0-RA/BG, etc.  Assemble each pair into a packed RGBA
    // word (red comes from RA[10:0] — returning the raw RA word made every
    // consumer read R from the always-zero top byte: the boot100 "no red
    // anywhere" screen).
    return {
        pack_tev_rgba(bp_[bp::kTevRegisterBase + 0u],
                      bp_[bp::kTevRegisterBase + 1u]),
        pack_tev_rgba(bp_[bp::kTevRegisterBase + 2u],
                      bp_[bp::kTevRegisterBase + 3u]),
        pack_tev_rgba(bp_[bp::kTevRegisterBase + 4u],
                      bp_[bp::kTevRegisterBase + 5u]),
        pack_tev_rgba(bp_[bp::kTevRegisterBase + 6u],
                      bp_[bp::kTevRegisterBase + 7u]),
    };
}

std::array<std::uint32_t, 4> GxState::tev_konst_colors() const {
    // konst_ra_bg_ mirrors the eight RA/BG words that were written with
    // bit 23 set; assemble pairs exactly like the blending registers.
    return {
        pack_tev_rgba(konst_ra_bg_[0], konst_ra_bg_[1]),
        pack_tev_rgba(konst_ra_bg_[2], konst_ra_bg_[3]),
        pack_tev_rgba(konst_ra_bg_[4], konst_ra_bg_[5]),
        pack_tev_rgba(konst_ra_bg_[6], konst_ra_bg_[7]),
    };
}

// ---------------------------------------------------------------------------
// Texture image / mode / TLUT decoded views.
// ---------------------------------------------------------------------------

// BP texture register layout for map N (N < 4):
//   TexMode0  = kTexMode0Base  + N   (0x80-0x83)
//   TexMode1  = kTexMode1Base  + N   (0x84-0x87)
//   TexImage0 = kTexImage0Base + N   (0x88-0x8B)
//   TexImage3 = kTexImage3Base + N   (0x94-0x97)
//   TexTlut   = kTexTlutBase   + N   (0x98-0x9B)
// Maps 4-7 repeat with a +kTexHighBankOffset (0x20) offset.

TexImage GxState::tex_image(unsigned map) const {
    const unsigned bank_offset =
        (map >= 4u) ? bp::kTexHighBankOffset : 0u;
    const unsigned slot = map & 3u;
    return decode_tex_image(
        bp_[bp::kTexImage0Base + bank_offset + slot],
        bp_[bp::kTexImage3Base + bank_offset + slot]);
}

TexMode GxState::tex_mode(unsigned map) const {
    const unsigned bank_offset =
        (map >= 4u) ? bp::kTexHighBankOffset : 0u;
    const unsigned slot = map & 3u;
    return decode_tex_mode(
        bp_[bp::kTexMode0Base + bank_offset + slot],
        bp_[bp::kTexMode1Base + bank_offset + slot]);
}

TlutRef GxState::tex_tlut(unsigned map) const {
    const unsigned bank_offset =
        (map >= 4u) ? bp::kTexHighBankOffset : 0u;
    const unsigned slot = map & 3u;
    return decode_tlut_ref(
        bp_[bp::kTexTlutBase + bank_offset + slot]);
}

// ---------------------------------------------------------------------------
// Projection: XF 0x1020-0x1026 (six float coefficients + type word).
// ---------------------------------------------------------------------------
Projection GxState::projection() const {
    Projection p{};
    for (unsigned i = 0; i < 6u; ++i) {
        const std::uint32_t raw = xf_[xf::kProjectionBase + i];
        // Raw XF words store IEEE 754 single-precision floats in the same
        // bit pattern — std::bit_cast gives us the host float with no UB.
        p.coeffs[i] = std::bit_cast<float>(raw);
    }
    // Word 6: 0 = perspective, 1 = orthographic.
    p.orthographic = (xf_[xf::kProjectionBase + 6u] != 0u);
    return p;
}

std::uint64_t GxState::dependency_shape_hash() const noexcept {
    if (dependency_shape_hash_valid_) {
        return dependency_shape_hash_;
    }
    const GxState& state = *this;
    const auto fnv1a_mix_u32 = [](std::uint64_t hash, std::uint32_t value) noexcept {
        for (unsigned shift = 0; shift < 32u; shift += 8u) {
            hash ^= (value >> shift) & 0xFFu;
            hash *= 1099511628211ull;
        }
        return hash;
    };

    std::uint64_t hash = 1469598103934665603ull;

    // Dependency capture only needs the shape of guest-memory reads. Camera,
    // lighting, and matrix values change constantly but do not alter which
    // vertex arrays, display lists, TLUTs, or texture ranges are snapshotted.
    hash = fnv1a_mix_u32(hash, state.cp(cp::kVcdLo));
    hash = fnv1a_mix_u32(hash, state.cp(cp::kVcdHi));
    for (std::uint8_t fmt = 0; fmt < 8u; ++fmt) {
        hash = fnv1a_mix_u32(
            hash,
            state.cp(static_cast<std::uint8_t>(cp::kVatABase + fmt)));
        hash = fnv1a_mix_u32(
            hash,
            state.cp(static_cast<std::uint8_t>(cp::kVatBBase + fmt)));
        hash = fnv1a_mix_u32(
            hash,
            state.cp(static_cast<std::uint8_t>(cp::kVatCBase + fmt)));
    }
    for (std::uint8_t attr = 0; attr < 16u; ++attr) {
        hash = fnv1a_mix_u32(
            hash,
            state.cp(static_cast<std::uint8_t>(cp::kArrayBaseBase + attr)));
        hash = fnv1a_mix_u32(
            hash,
            state.cp(static_cast<std::uint8_t>(cp::kArrayStrideBase + attr)));
    }

    hash = fnv1a_mix_u32(hash, state.bp(bp::kGenMode));
    for (std::uint8_t reg = bp::kTevOrderBase;
         reg < bp::kTevOrderBase + 8u;
         ++reg) {
        hash = fnv1a_mix_u32(hash, state.bp(reg));
    }
    hash = fnv1a_mix_u32(hash, state.bp(bp::kIndRef));
    for (std::uint8_t reg = bp::kIndCmdBase;
         reg < bp::kIndCmdBase + kMaxTevStages;
         ++reg) {
        hash = fnv1a_mix_u32(hash, state.bp(reg));
    }
    hash = fnv1a_mix_u32(hash, state.bp(bp::kTlutSrcAddr));
    hash = fnv1a_mix_u32(hash, state.bp(bp::kTlutDest));

    for (std::uint8_t bank = 0; bank <= bp::kTexHighBankOffset;
         bank += bp::kTexHighBankOffset) {
        for (std::uint8_t slot = 0; slot < 4u; ++slot) {
            hash = fnv1a_mix_u32(
                hash,
                state.bp(static_cast<std::uint8_t>(
                    bp::kTexMode0Base + bank + slot)));
            hash = fnv1a_mix_u32(
                hash,
                state.bp(static_cast<std::uint8_t>(
                    bp::kTexMode1Base + bank + slot)));
            hash = fnv1a_mix_u32(
                hash,
                state.bp(static_cast<std::uint8_t>(
                    bp::kTexImage0Base + bank + slot)));
            hash = fnv1a_mix_u32(
                hash,
                state.bp(static_cast<std::uint8_t>(
                    bp::kTexImage3Base + bank + slot)));
            hash = fnv1a_mix_u32(
                hash,
                state.bp(static_cast<std::uint8_t>(
                    bp::kTexTlutBase + bank + slot)));
        }
    }

    dependency_shape_hash_ = hash;
    dependency_shape_hash_valid_ = true;
    return hash;
}

}  // namespace galaxy::gx
