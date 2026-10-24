// GX register file: CP/XF/BP register loads, dirty tracking and decoded register views.

#include "galaxy/gx/gx_state.h"

#include "galaxy/crash_notes.h"
#include "galaxy/gx/fifo_parser.h"
#include "galaxy/gx/uber_constants.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstdio>

namespace galaxy::gx {

namespace {

std::atomic<bool> s_unknown_commands_fatal{false};

}  // namespace

bool unknown_gx_commands_fatal() noexcept {
    return s_unknown_commands_fatal.load(std::memory_order_relaxed);
}

void set_unknown_gx_commands_fatal(bool fatal) noexcept {
    s_unknown_commands_fatal.store(fatal, std::memory_order_relaxed);
}
namespace {

// One bit per register already reported; the parsers run on two threads.
std::array<std::atomic<std::uint64_t>, 4> s_bp_reported{};
std::array<std::atomic<std::uint64_t>, 4> s_cp_reported{};
std::array<std::atomic<std::uint64_t>, 1024> s_xf_reported{};
std::array<std::atomic<std::uint64_t>, 1> s_tmem_reported{};

// Throws when unknown commands are fatal; otherwise logs the first of each register.
template <std::size_t Words>
void reject(const char* message, std::uint8_t opcode,
            std::array<std::atomic<std::uint64_t>, Words>& reported, std::uint32_t index) {
    if (unknown_gx_commands_fatal()) throw GxFatalError(message, 0u, opcode);
    index = std::min<std::uint32_t>(index, Words * 64u - 1u);
    const std::uint64_t bit = std::uint64_t{1} << (index & 63u);
    if ((reported[index >> 6u].fetch_or(bit, std::memory_order_relaxed) & bit) != 0u) return;
    std::fprintf(stderr, "%s\n", message);
    crash_notes::note(message);
}

// Bit span covering palette chunks `first`..`last` inclusive. Callers guarantee
// last < 64 (52 chunks exist) and first <= last, so the shift is never wide.
[[nodiscard]] constexpr std::uint64_t xf_palette_span_bits(
    std::uint32_t first, std::uint32_t last) noexcept {
    return ((std::uint64_t{1} << (last + 1u)) - 1u) &
        ~((std::uint64_t{1} << first) - 1u);
}

void reject_unknown_cp_write(
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
    reject(message, op::kLoadCpReg, s_cp_reported, reg);
}

void reject_unknown_xf_write(
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
    reject(message, opcode, s_xf_reported, address);
}

void reject_unknown_bp_write(
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
    reject(message, op::kLoadBpReg, s_bp_reported, reg);
}

void reject_tmem_load(std::uint32_t value, std::uint32_t command) {
    char message[224]{};
    std::snprintf(
        message,
        sizeof(message),
        "[GxState] TMEM load not emulated: opcode=0x%02X register=0x%02X "
        "value=0x%06X command=0x%08X fifo-offset=unavailable",
        static_cast<unsigned>(op::kLoadBpReg),
        static_cast<unsigned>(bp::kTexLoadStart),
        static_cast<unsigned>(value),
        static_cast<unsigned>(command));
    reject(message, op::kLoadBpReg, s_tmem_reported, 0u);
}

[[nodiscard]] constexpr bool in_u16_range(
    std::uint16_t value,
    std::uint16_t begin,
    std::uint16_t end) noexcept {
    return value >= begin && value < end;
}

[[nodiscard]] constexpr bool is_classified_xf_address(
    std::uint16_t address) noexcept {
    // Matrix and light memory.
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

    // XFMEM_ERROR..XFMEM_VTXSPECS (0x1000-0x1008): status and diagnostic registers with no
    // rendering effect (see xf::kStatusRegsBase). Stored, never dirty.
    if (in_u16_range(
            address, xf::kStatusRegsBase, xf::kNumChannels)) {
        return true;
    }

    // Registers with consumers and dirty handling in apply_validated_xf.
    return in_u16_range(address, xf::kNumChannels, 0x1013u) ||
        in_u16_range(
            address, xf::kUnknownGroup1Base, xf::kViewportBase) ||
        in_u16_range(address, xf::kViewportBase, 0x1020u) ||
        in_u16_range(address, xf::kProjectionBase, 0x1027u) ||
        address == xf::kNumTexGens ||
        in_u16_range(address, xf::kTexGenBase, 0x1048u) ||
        in_u16_range(address, xf::kPostTexGenBase, 0x1058u);
}

// True when every address of the transfer is known. Otherwise each unknown address goes to
// reject() and the caller applies only the known words.
[[nodiscard]] bool validate_xf_write(
    std::uint8_t opcode,
    std::uint16_t base,
    const std::uint32_t* values,
    std::uint16_t count) {
    // Runs before xf_ or dirty_ change. Addresses past 0x1FFF are rejected, not wrapped.
    const std::uint32_t end = static_cast<std::uint32_t>(base) + count;
    static_assert(xf::kPostMatricesBase + 0x0100u == xf::kLightsBase);
    // The post-matrix and light banks are adjacent and classified alike, so most matrix
    // uploads validate their whole span at once. Other spans are checked word by word.
    if (end <= 0x0100u ||
        (base >= xf::kNormalMatricesBase &&
         end <= xf::kNormalMatricesBase + 0x0060u) ||
        (base >= xf::kPostMatricesBase &&
         end <= xf::kLightsBase + 0x0080u)) {
        return true;
    }
    bool all_known = true;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t address = static_cast<std::uint32_t>(base) + i;
        if (address > 0xFFFFu ||
            !is_classified_xf_address(static_cast<std::uint16_t>(address))) {
            reject_unknown_xf_write(
                opcode,
                base,
                count,
                address,
                values[i]);
            all_known = false;
        }
    }
    return all_known;
}

[[nodiscard]] constexpr bool is_classified_bp_register(
    std::uint8_t reg) noexcept {
    // Exactly the registers load_bp handles; the gaps between these ranges are unknown.
    return reg <= 0x04u ||
        (reg >= bp::kIndMtxBase && reg < bp::kIndMtxBase + bp::kIndMtxCount) ||
        reg == bp::kIndMask ||
        (reg >= bp::kIndCmdBase && reg < bp::kIndCmdBase + 0x10u) ||
        (reg >= bp::kScissorTopLeft && reg <= bp::kSuLpSize) ||
        (reg >= bp::kPerf0TriBase && reg < bp::kPerf0TriBase + 2u) ||
        (reg >= bp::kRas1Ss0 && reg <= 0x59u) ||
        reg == bp::kTexLoadStart ||
        (reg >= bp::kTlutSrcAddr && reg <= bp::kBusClock1) ||
        (reg >= 0x80u && reg <= 0xFDu) ||
        reg == bp::kBpMask;
}

// The BP inputs of dependency_shape_hash(); the two must stay in step. Other writes (uniforms,
// blend, scissor, copies) cannot change which guest memory a draw reads.
[[nodiscard]] constexpr bool affects_dependency_shape(
    std::uint8_t reg, std::uint32_t changed) noexcept {
    // Which maps are sampled depends on the TEV input selectors and the z-texture operation,
    // not on combiner arithmetic, swaps or the z format.
    if (reg >= bp::kTevColorEnvBase && reg < bp::kTevColorEnvBase + 0x20u) {
        return (changed & ((reg & 1u) != 0u ? 0xFFF0u : 0xFFFFu)) != 0u;
    }
    if (reg == bp::kTevZEnv1) {
        return (changed & 0xCu) != 0u;
    }
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

// Dirty bits of a write to `reg`; evaluated at compile time into kBpWriteMetadata.
[[nodiscard]] constexpr std::uint32_t bp_dirty_flags_for_table(
    std::uint8_t reg) {
    std::uint32_t flags = 0u;

    // Display-copy sample pattern 0x01-0x04: stored only; copies filter with the BP 0x53/0x54
    // weights.
    if (reg >= bp::kDisplayCopyFilterBase &&
        reg < bp::kDisplayCopyFilterBase + 4u) {
        return flags;
    }
    // Indirect texture matrices 0x06-0x0E: three 2x3 offset matrices.
    if (reg >= bp::kIndMtxBase &&
        reg < bp::kIndMtxBase + bp::kIndMtxCount) {
        // Coefficients and exponent are ind_mtx[] uniforms; stage matrix
        // selection remains shader state in kIndCmdBase.
        flags |= GxState::kDirtyTevConstants;
        return flags;
    }
    // Scissor is dynamic and read directly for every draw.
    if (reg == bp::kScissorTopLeft || reg == bp::kScissorBottomRight) {
        return flags;
    }
    // Line/point size is consumed by the geometry-stage tail of b0.
    if (reg == bp::kSuLpSize) {
        flags |= GxState::kDirtyVsConstants;
        return flags;
    }
    // Perf-query triangle/quad counters: stored but never consulted.
    if (reg >= bp::kPerf0TriBase && reg < bp::kPerf0TriBase + 2u) {
        return flags;
    }
    // Indirect texture scale/reference.
    if (reg >= bp::kRas1Ss0 && reg <= bp::kIndRef) {
        flags |= GxState::kDirtyTev;
        return flags;
    }
    // Indirect texture stage commands 0x10-0x1F
    if (reg >= bp::kIndCmdBase && reg < bp::kIndCmdBase + 0x10u) {
        flags |= GxState::kDirtyTev;
        return flags;
    }
    if (reg == bp::kIndMask) {
        return flags;
    }
    // TEV order (TREF)  0x28-0x2F
    if (reg >= bp::kTevOrderBase && reg < bp::kTevOrderBase + 8u) {
        flags |= GxState::kDirtyTev;
        return flags;
    }
    // Texture-coordinate S/T sizes feed tex_dims[].zw and VS constants. Neither shader key
    // includes them, so their uploads do not rebuild a shader key or look up a pipeline.
    if (reg >= bp::kTexCoordSizeBase &&
        reg < bp::kTexCoordSizeBase + bp::kTexCoordSizeCount) {
        flags |= GxState::kDirtyTevConstants | GxState::kDirtyVsConstants;
        return flags;
    }
    // TEV color/alpha env 0xC0-0xDF: even registers color, odd alpha, 16 stages.
    if (reg >= bp::kTevColorEnvBase && reg < bp::kTevColorEnvBase + 0x20u) {
        flags |= GxState::kDirtyTev;
        return flags;
    }

    switch (reg) {
    case bp::kGenMode:
        flags |= GxState::kDirtyTev | GxState::kDirtyXfShader | GxState::kDirtyRenderState |
                  GxState::kDirtyVsConstants;
        break;

    case bp::kBlendMode:   // 0x41
        // Fixed-function blend state. load_bp adds kDirtyTev when a bit the pixel shader reads
        // changes: enable, subtract, dither, alpha update, or a factor reading source alpha.
        flags |= GxState::kDirtyRenderState;
        break;
    case bp::kConstAlpha:  // 0x42
        // The value is a uniform; load_bp adds shader and render state when the enable changes.
        flags |= GxState::kDirtyTevConstants;
        break;
    case bp::kPeControl:   // 0x43
        flags |= GxState::kDirtyRenderState | GxState::kDirtyTev;
        break;
    case bp::kFieldMask:   // 0x44
        flags |= GxState::kDirtyRenderState;
        break;

    case bp::kZMode:       // 0x40
        flags |= GxState::kDirtyRenderState;
        break;

    case bp::kPeDone:      // 0x45
    case bp::kBusClock0:   // 0x46
    case bp::kPeToken:     // 0x47
    case bp::kPeTokenInt:  // 0x48
        break;

    case bp::kAlphaCompare:  // 0xF3
        flags |= GxState::kDirtyTevConstants;
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

    case bp::kScissorOffset:
        break;

    // GXTexModeSync's empty TMEM load (see load_bp).
    case bp::kTexLoadStart:
        break;

    case bp::kTlutSrcAddr:  // 0x64
    case bp::kTlutDest:     // 0x65
        flags |= GxState::kDirtyTex;
        break;

    // GXInvalidateTexAll signal  0x66-0x67
    case bp::kTexInvalidate:
    case bp::kPerf1:
        flags |= GxState::kDirtyTex;
        break;

    case bp::kFieldMode:
        flags |= GxState::kDirtyTev;
        break;
    case bp::kBusClock1:
        break;

    // Texture units: maps 0-3 at 0x80-0x9F, maps 4-7 at 0xA0-0xBF (kTexHighBankOffset
    // is 0x20). Covers mode0, mode1, image0-3 and tlut.
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
        flags |= GxState::kDirtyTex;
        break;

    // TEV registers 0xE0-0xE7 (RA/BG pairs). load_bp handles their writes before the table
    // lookup, since bit 23 selects the konst bank.
    case 0xE0u: case 0xE1u: case 0xE2u: case 0xE3u:
    case 0xE4u: case 0xE5u: case 0xE6u: case 0xE7u:
        flags |= GxState::kDirtyTevConstants;
        break;

    // Fog range-adjust parameters  0xE8-0xED
    case 0xE8u: case 0xE9u: case 0xEAu: case 0xEBu: case 0xECu: case 0xEDu:
        flags |= GxState::kDirtyTevConstants;
        break;

    // Fog parameters  0xEE-0xF2
    case bp::kFogParam0: case bp::kFogParam1: case bp::kFogParam2:
    case bp::kFogColor:
        flags |= GxState::kDirtyTevConstants;
        break;
    case bp::kFogParam3:
        flags |= GxState::kDirtyTevConstants;
        break;

    // Z env  0xF4-0xF5
    case bp::kTevZEnv0:
        flags |= GxState::kDirtyTevConstants; // depth bias, not format/operation
        break;
    case bp::kTevZEnv1:
        flags |= GxState::kDirtyTev;
        break;

    // TEV KSEL  0xF6-0xFD  (2 stages per register, 8 registers for 16 stages)
    case 0xF6u: case 0xF7u: case 0xF8u: case 0xF9u:
    case 0xFAu: case 0xFBu: case 0xFCu: case 0xFDu:
        flags |= GxState::kDirtyTev;
        break;

    // Stored without dirty bits. Unknown registers never reach the table.
    default:
        break;
    }
    return flags;
}

struct BpWriteMetadata {
    std::uint32_t flags;
    std::uint32_t dependency_mask;
};
constexpr auto kBpWriteMetadata = [] {
    std::array<BpWriteMetadata, 256> metadata{};
    for (unsigned reg = 0u; reg < metadata.size(); ++reg) {
        const auto index = static_cast<std::uint8_t>(reg);
        auto& info = metadata[reg];
        if (!is_classified_bp_register(index)) {
            continue;
        }
        info.flags = bp_dirty_flags_for_table(index);
        for (unsigned bit = 0u; bit < 24u; ++bit) {
            if (affects_dependency_shape(index, 1u << bit)) {
                info.dependency_mask |= 1u << bit;
            }
        }
    }
    return metadata;
}();

}  // namespace

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
        reject_unknown_cp_write(reg, value);
        return;
    }

    if (cp_[reg] != value) {
        if (reg == cp::kVcdLo && ((cp_[reg] ^ value) & 1u) != 0u) {
            dirty |= kDirtyVsConstants;
        }
        cp_[reg] = value;
        if ((dirty & kDirtyVcd) != 0u) {
            invalidate_dependency_shape();
        }
        dirty_ |= dirty;
    }
}

void GxState::load_xf(
    std::uint16_t base,
    const std::uint32_t* values,
    std::uint16_t count) {
    if (validate_xf_write(op::kLoadXfReg, base, values, count)) {
        if (track_matrix_loads_ && base < xf::kStatusRegsBase) [[unlikely]] {
            note_matrix_load(base, count, 0xFFFFFFFFu);
        }
        apply_validated_xf(base, values, count);
    } else {
        apply_known_xf(base, values, count);
    }
}

void GxState::apply_known_xf(
    std::uint16_t base,
    const std::uint32_t* values,
    std::uint16_t count) {
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t address = static_cast<std::uint32_t>(base) + i;
        if (address <= 0xFFFFu && is_classified_xf_address(static_cast<std::uint16_t>(address))) {
            apply_validated_xf(static_cast<std::uint16_t>(address), values + i, 1u);
        }
    }
}

void GxState::apply_validated_xf(
    std::uint16_t base,
    const std::uint32_t* values,
    std::uint16_t count) {
    // Below 0x1000 every validated address is matrix or light memory, whose only dirty bit is
    // kDirtyXfMatrices. Words are written in order because `values` may alias xf_.
    if (base < xf::kStatusRegsBase) {
        ++matrix_writes_;
        bool changed = false;
        for (std::uint16_t i = 0; i < count; ++i) {
            const std::uint16_t addr = static_cast<std::uint16_t>(base + i);
            if (xf_[addr] != values[i]) {
                xf_[addr] = values[i];
                changed = true;
            }
        }
        if (changed) {
            dirty_ |= kDirtyXfMatrices;
            // Marks the palette chunks the write touched (see xf_palette_dirty()).
            const std::uint32_t first = base / kMatrixPaletteChunkWords;
            const std::uint32_t last =
                static_cast<std::uint32_t>(base + count - 1u) /
                kMatrixPaletteChunkWords;
            const std::uint64_t bits =
                xf_palette_span_bits(first, last);
            if ((xf_palette_dirty_ & bits) != bits) {
                xf_palette_dirty_ |= bits;
            }
        }
        return;
    }
    for (std::uint16_t i = 0; i < count; ++i) {
        const std::uint16_t addr =
            static_cast<std::uint16_t>(base + i);
        if (xf_[addr] == values[i]) {
            continue;
        }
        xf_[addr] = values[i];

        // Channel count and ambient/material colors 0x1009-0x100D.
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
        // The renderer reads the viewport per draw, but width and height also size line and point
        // expansion in b0.
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

void GxState::load_xf_indexed(
    std::uint16_t xf_addr,
    const std::uint32_t* values,
    std::uint16_t count,
    std::uint32_t source) {
    // Errors report kLoadIndxA for all four LOAD_INDX opcodes; the A-D selector is not passed in.
    if (validate_xf_write(op::kLoadIndxA, xf_addr, values, count)) {
        if (track_matrix_loads_ && xf_addr < xf::kStatusRegsBase) [[unlikely]] {
            note_matrix_load(xf_addr, count, source);
        }
        apply_validated_xf(xf_addr, values, count);
    } else {
        apply_known_xf(xf_addr, values, count);
    }
}

void GxState::load_bp(std::uint32_t command) {
    const auto reg = static_cast<std::uint8_t>(command >> 24);
    const std::uint32_t val = command & 0x00FFFFFFu;

    // Rejected before the write mask is used or reset, so a skipped write changes nothing.
    if (!is_classified_bp_register(reg)) {
        reject_unknown_bp_write(reg, val, command);
        return;
    }

    // 0xFE is the one-shot write mask register. It arms the mask but does not write bp_[].
    if (reg == bp::kBpMask) {
        bp_mask_ = val;
        return;
    }

    // A TMEM load of nothing (GXTexModeSync) has no effect: textures are read from main memory.
    // A load of lines is not emulated; after reject_tmem_load it is stored like the empty one.
    if (reg == bp::kTexLoadStart && (val & bp_mask_) != 0u) {
        reject_tmem_load(val, command);
    }

    // TEV registers 0xE0-0xE7: bit 23 of the masked value sends the write to the konst bank
    // instead of the blending register, as in Dolphin's BPStructs. A konst write must not also
    // land in bp_[], where it would overwrite C0/C1/A0.
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
    // The mask applies to one write.
    bp_mask_ = 0x00FFFFFFu;
    if (previous == next) {
        return;
    }
    bp_[reg] = next;
    const auto metadata = kBpWriteMetadata[reg];
    if (((previous ^ next) & metadata.dependency_mask) != 0u) {
        invalidate_dependency_shape();
    }

    // Table bits per register; the four below also depend on which bits changed.
    dirty_ |= metadata.flags;
    switch (reg) {
    case bp::kBlendMode: {
        const auto reads_source_alpha = [](std::uint32_t word) {
            return ((word >> 5u) & 6u) == 4u || ((word >> 8u) & 6u) == 4u;
        };
        constexpr std::uint32_t kShaderScalarMask = 0x815u;
        if (((previous ^ next) & kShaderScalarMask) != 0u ||
            reads_source_alpha(previous) != reads_source_alpha(next)) {
            dirty_ |= kDirtyTev;
        }
        break;
    }
    case bp::kConstAlpha:
        if (((previous ^ next) & (1u << 8u)) != 0u) {
            dirty_ |= kDirtyRenderState | kDirtyTev;
        }
        break;
    case bp::kAlphaCompare:
        if (((previous ^ next) & 0x00ff0000u) != 0u) {
            dirty_ |= kDirtyTev;
        }
        break;
    case bp::kFogParam3:
        if (((previous ^ next) & 0x00e00000u) != 0u) {
            dirty_ |= kDirtyTev;
        }
        break;
    default:
        break;
    }
}

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

namespace {
// TEV color register pairs (BP 0xE0-0xE7): RA = red[10:0], alpha[22:12]; BG = blue[10:0],
// green[22:12]. Fields are signed 11-bit in the blending registers, 8-bit in the konst bank.
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

// Packs a pair as R8G8B8A8, each 11-bit field clamped to 255.
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
    // 0xE0/0xE1 = PREV RA/BG, 0xE2/0xE3 = REG0 RA/BG, and so on.
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
    // konst_ra_bg_ holds the eight RA/BG words written with bit 23 set. Pairs assemble the
    // same way as the blending registers.
    return {
        pack_tev_rgba(konst_ra_bg_[0], konst_ra_bg_[1]),
        pack_tev_rgba(konst_ra_bg_[2], konst_ra_bg_[3]),
        pack_tev_rgba(konst_ra_bg_[4], konst_ra_bg_[5]),
        pack_tev_rgba(konst_ra_bg_[6], konst_ra_bg_[7]),
    };
}

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

std::uint64_t GxState::dependency_shape_hash() const noexcept {
    if (dependency_shape_hash_valid_) {
        return dependency_shape_hash_;
    }
    const GxState& state = *this;
    std::size_t signature_index = 0;
    const auto fnv1a_mix_u32 = [this, &signature_index](
        std::uint64_t hash, std::uint32_t value) noexcept {
        dependency_shape_words_[signature_index++] = value;
        for (unsigned shift = 0; shift < 32u; shift += 8u) {
            hash ^= (value >> shift) & 0xFFu;
            hash *= 1099511628211ull;
        }
        return hash;
    };

    std::uint64_t hash = 1469598103934665603ull;

    // Dependency capture needs only the shape of guest-memory reads. Camera, lighting and
    // matrix values change often but do not alter which vertex arrays, display lists, TLUTs
    // or texture ranges are snapshotted.
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
    // A whole-FIFO hit also restores the final parser state, and two states with the same
    // sampled maps can differ after a later masked selector write. So every stage's selector
    // bits (inactive stages too) and both z-operation bits are hashed, each stage's color and
    // alpha selectors as one word. Arithmetic, swaps and the z format cannot change texture ranges.
    for (unsigned stage = 0; stage < kMaxTevStages; ++stage) {
        const auto color = state.bp(static_cast<std::uint8_t>(
            bp::kTevColorEnvBase + 2u * stage)) & 0xFFFFu;
        const auto alpha = state.bp(static_cast<std::uint8_t>(
            bp::kTevAlphaEnvBase + 2u * stage)) & 0xFFF0u;
        hash = fnv1a_mix_u32(hash, color | (alpha << 12u));
    }
    hash = fnv1a_mix_u32(hash, state.bp(bp::kTevZEnv1) & 0xCu);
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
