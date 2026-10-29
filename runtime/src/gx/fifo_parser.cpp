// GX FIFO command parser and its cache of parsed display lists.

#include "galaxy/gx/fifo_parser.h"
#include "galaxy/gx/snapshot_region_lookup.h"
#include "galaxy/motion_signals.h"

#include "galaxy/simd128.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <type_traits>
#include <utility>

namespace galaxy::gx {


std::uint64_t elapsed_fifo_us(
    std::chrono::steady_clock::time_point start,
    std::chrono::steady_clock::time_point end) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            end - start).count());
}


void swap32_block(
    const std::byte* src,
    std::uint32_t* dst,
    std::size_t count) {
    const std::byte* in  = src;
    std::uint8_t*    out = reinterpret_cast<std::uint8_t*>(dst);

    std::size_t i = 0;
    for (; i + 4 <= count; i += 4, in += 16, out += 16) {
        simd::store(out, simd::byteswap32(simd::load(in)));
    }
    for (; i < count; ++i) {
        const auto b0 = static_cast<std::uint32_t>(static_cast<std::uint8_t>(in[0]));
        const auto b1 = static_cast<std::uint32_t>(static_cast<std::uint8_t>(in[1]));
        const auto b2 = static_cast<std::uint32_t>(static_cast<std::uint8_t>(in[2]));
        const auto b3 = static_cast<std::uint32_t>(static_cast<std::uint8_t>(in[3]));
        dst[i] = (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;
        in += 4;
    }
}

void FifoParser::set_dump_dir(std::string dump_dir) {
    dump_dir_ = std::move(dump_dir);
}

// Best effort: I/O errors are ignored.
void FifoParser::write_dump(
    std::span<const std::byte> fifo,
    std::size_t failure_offset) const {
    // At most four dumps per process, so a failure that repeats every frame cannot fill the disk.
    static std::atomic_uint32_t s_dump_attempts{0};
    std::uint32_t observed = s_dump_attempts.load(std::memory_order_relaxed);
    for (;;) {
        if (observed >= 4u) {
            return;
        }
        if (s_dump_attempts.compare_exchange_strong(
                observed, observed + 1u,
                std::memory_order_relaxed, std::memory_order_relaxed)) {
            break;
        }
    }
    const std::string path =
        dump_dir_ + "/frame" + std::to_string(frame_index_) + ".gxdump";

#if defined(_WIN32)
    FILE* fp = nullptr;
    if (fopen_s(&fp, path.c_str(), "wb") != 0 || fp == nullptr) {
        return;
    }
#else
    FILE* fp = std::fopen(path.c_str(), "wb");
    if (fp == nullptr) {
        return;
    }
#endif

    // Header: magic (4), version (4), failure_offset (8), data_size (8); then the FIFO bytes.
    const std::uint32_t magic   = 0x47584446u; // "GXDF"
    const std::uint32_t version = 1u;
    const std::uint64_t off64   = static_cast<std::uint64_t>(failure_offset);
    const std::uint64_t size64  = static_cast<std::uint64_t>(fifo.size());

    std::fwrite(&magic,   sizeof(magic),   1, fp);
    std::fwrite(&version, sizeof(version), 1, fp);
    std::fwrite(&off64,   sizeof(off64),   1, fp);
    std::fwrite(&size64,  sizeof(size64),  1, fp);
    std::fwrite(fifo.data(), 1, fifo.size(), fp);
    std::fclose(fp);
}

// Selects the sorted region lookup for one run when the caller froze the regions and they are
// sorted and disjoint; restores the enclosing selection on exit.
struct FifoParser::FrozenRegionScope {
    FifoParser& parser;
    const GuestMemory* previous;

    FrozenRegionScope(FifoParser& owner, GuestMemory* memory, bool frozen)
        : parser(owner), previous(owner.frozen_sorted_memory_) {
        parser.frozen_sorted_memory_ =
            frozen && memory != nullptr && memory->regions != nullptr &&
                detail::snapshot_regions_are_disjoint_sorted(
                    {memory->regions, memory->region_count})
            ? memory : nullptr;
    }
    ~FrozenRegionScope() { parser.frozen_sorted_memory_ = previous; }
    FrozenRegionScope(const FrozenRegionScope&) = delete;
    FrozenRegionScope& operator=(const FrozenRegionScope&) = delete;
};

std::span<const std::byte> FifoParser::resolve_guest(
    GuestMemory* memory,
    std::uint32_t guest_addr,
    std::uint32_t size,
    const FifoCursor& at) const {
    if (memory != nullptr) {
        if (std::byte* fast = resolve_guest_fast(memory, guest_addr, size);
            fast != nullptr) {
            if (memory_read_recorder_ != nullptr) {
                memory_read_recorder_->record_guest_memory_read(guest_addr, size);
            }
            return std::span<const std::byte>(fast, size);
        }
    }
    if (memory != nullptr && memory->regions != nullptr) {
        const std::byte* host = nullptr;
        if (memory == frozen_sorted_memory_ && size != 0u) {
            host = detail::resolve_sorted_snapshot_region(
                {memory->regions, memory->region_count}, guest_addr, size);
        } else {
            const std::uint64_t req_end =
                static_cast<std::uint64_t>(guest_addr) + size;
            for (std::uint32_t ri = 0; ri < memory->region_count; ++ri) {
                const GuestMemoryRegion& r = memory->regions[ri];
                const std::uint64_t reg_end =
                    static_cast<std::uint64_t>(r.guest_base) + r.size;
                if (guest_addr >= r.guest_base && req_end <= reg_end &&
                    r.host_base != nullptr) {
                    host = r.host_base + (guest_addr - r.guest_base);
                    break;
                }
            }
        }
        if (host != nullptr) {
            if (memory_read_recorder_ != nullptr) {
                memory_read_recorder_->record_guest_memory_read(guest_addr, size);
            }
            return std::span<const std::byte>(host, size);
        }
    }
    char addr_buf[12];
    std::snprintf(
        addr_buf, sizeof(addr_buf), "%08X",
        static_cast<unsigned>(guest_addr));
    throw GxFatalError(
        std::string("GX FIFO: unresolvable guest pointer 0x") + addr_buf,
        at.base_offset + at.offset,
        0u);
}

void FifoParser::run(
    std::span<const std::byte> fifo,
    GuestMemory* memory,
    FifoSink& sink,
    GxState& state,
    bool frozen_regions) {
    const FrozenRegionScope region_scope(*this, memory, frozen_regions);
    ++frame_index_;

    FifoCursor cursor;
    cursor.data        = fifo;
    cursor.offset      = 0;
    cursor.base_offset = 0;
    cursor.command_offset = 0;
    cursor.recover_truncation = false;

    try {
        run_window(cursor, sink, state, 0);
    } catch (const GxFatalError& err) {
        write_dump(fifo, err.fifo_offset());
        throw;
    }
}
std::size_t FifoParser::run_available(
    std::span<const std::byte> fifo,
    GuestMemory* memory,
    FifoSink& sink,
    GxState& state,
    bool frozen_regions) {
    const FrozenRegionScope region_scope(*this, memory, frozen_regions);
    ++frame_index_;

    FifoCursor cursor;
    cursor.data = fifo;
    cursor.offset = 0;
    cursor.base_offset = 0;
    cursor.command_offset = 0;
    cursor.recover_truncation = true;

    try {
        run_window(cursor, sink, state, 0);
        return cursor.offset;
    } catch (const FifoNeedMoreData& pending) {
        return pending.command_offset();
    } catch (const GxFatalError& err) {
        write_dump(fifo, err.fifo_offset());
        throw;
    }
}

// Frees a vector's heap block. shrink_to_fit is only a request; the capacity-based budget
// needs the block gone.
template <typename T>
void release_capacity(std::vector<T>& values) {
    std::vector<T>{}.swap(values);
}

// Whether the bytes after a top-level opcode hold the whole command, so a command split by a
// capture boundary stops the window at its opcode without throwing FifoNeedMoreData. Opcodes
// not listed rely on the cursor's own checks.
static bool top_level_command_complete(
    const FifoCursor& cursor,
    std::uint8_t opcode,
    const FifoSink& sink) {
    const std::size_t available = cursor.data.size() - cursor.offset;
    const auto u16_at = [&](std::size_t at) {
        return static_cast<std::uint16_t>(
            (std::to_integer<std::uint32_t>(cursor.data[cursor.offset + at]) << 8) |
            std::to_integer<std::uint32_t>(cursor.data[cursor.offset + at + 1u]));
    };
    switch (opcode) {
    case op::kLoadCpReg:
        return available >= 5u;
    case op::kLoadXfReg:
        return available >= 4u &&
            available >= 4u + 4u * ((static_cast<std::size_t>(u16_at(0)) & 0xFu) + 1u);
    case op::kLoadBpReg:
        return available >= 4u;
    case op::kHostMarker:
        return available >= op::kHostMarkerBytes - 1u;
    default:
        if (opcode >= op::kDrawFirst && opcode <= op::kDrawLast) {
            if (available < 2u) {
                return false;
            }
            // A GX vertex is at most 129 bytes (direct position, NBT normal,
            // two colors, eight texture coordinates, nine matrix indices), so
            // most draws can be checked for completeness without the layout lookup.
            constexpr std::size_t kVertexBytesBound = 160u;
            const std::uint16_t vertex_count = u16_at(0);
            if (available - 2u >= static_cast<std::size_t>(vertex_count) * kVertexBytesBound) {
                return true;
            }
            return available - 2u >= sink.draw_payload_size(
                static_cast<std::uint8_t>(opcode & 0x07u), vertex_count);
        }
        return true;
    }
}

void FifoParser::run_window(
    FifoCursor& cursor,
    FifoSink& sink,
    GxState& state,
    int depth) {
    while (!cursor.empty()) {
        const std::size_t local_opcode_offset = cursor.offset;
        const std::size_t opcode_offset =
            cursor.base_offset + local_opcode_offset;
        cursor.command_offset = opcode_offset;
        const std::uint8_t opcode = cursor.read_u8();
        if (depth == 0 && cursor.recover_truncation &&
            !top_level_command_complete(cursor, opcode, sink)) {
            cursor.offset = local_opcode_offset;
            return;
        }
        FifoParserProfile* const profile = profile_;
        const auto command_start = profile != nullptr
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        // command_us accumulates exclusive time: a CALL_DL's nested commands add to it while the
        // CALL_DL runs, and finish_profile subtracts that growth from the CALL_DL's own time.
        const std::uint64_t command_us_before =
            profile != nullptr ? profile->command_us : 0u;
        if (profile != nullptr) {
            ++profile->command_count;
        }
        const auto finish_profile =
            [&](std::uint64_t FifoParserProfile::*count_field,
                std::uint64_t FifoParserProfile::*us_field) -> std::uint64_t {
                if (profile == nullptr) {
                    return 0;
                }
                ++(profile->*count_field);
                const std::uint64_t us = elapsed_fifo_us(
                    command_start,
                    std::chrono::steady_clock::now());
                (profile->*us_field) += us;
                const std::uint64_t exclusive_us = profile->command_us >= command_us_before
                    ? us - (profile->command_us - command_us_before)
                    : us;
                profile->command_us += exclusive_us;
                return us;
            };

        switch (opcode) {

        case op::kNop:
            // Commands with no effect are not recorded for replay.
            finish_profile(
                &FifoParserProfile::nop_count,
                &FifoParserProfile::nop_us);
            break;

        case op::kLoadCpReg: {
            const std::uint8_t  reg = cursor.read_u8();
            const std::uint32_t val = cursor.read_u32();
            state.load_cp(reg, val);
            finish_profile(
                &FifoParserProfile::cp_count,
                &FifoParserProfile::cp_us);
            break;
        }

        case op::kLoadXfReg: {
            // Header: u16 count-minus-1, u16 base address.
            const std::uint16_t count_m1 = cursor.read_u16();
            const std::uint16_t base     = cursor.read_u16();
            const std::uint16_t count    =
                static_cast<std::uint16_t>((count_m1 & 0x000Fu) + 1u);

            // count uses four bits of count_m1, so at most 16 words.
            std::array<std::uint32_t, 16> values{};
            const auto raw = cursor.take(static_cast<std::size_t>(count) * 4u);
            swap32_block(raw.data(), values.data(), count);
            state.load_xf(base, values.data(), count);
            finish_profile(
                &FifoParserProfile::xf_count,
                &FifoParserProfile::xf_us);
            break;
        }

        case op::kHostMarker: {
            if (depth != 0) {
                throw GxFatalError("GX FIFO: host marker inside a display list",
                                   opcode_offset, opcode);
            }
            const std::uint8_t kind = cursor.read_u8();
            const std::uint16_t aux = cursor.read_u16();
            const std::uint32_t a = cursor.read_u32();
            const std::uint32_t b = cursor.read_u32();
            sink.on_host_marker(kind, aux, a, b);
            finish_profile(
                &FifoParserProfile::nop_count,
                &FifoParserProfile::nop_us);
            break;
        }

        case op::kUnknownMetrics:
            // Metrics flush: no payload and no state effect, so not recorded for replay.
            finish_profile(
                &FifoParserProfile::metrics_count,
                &FifoParserProfile::metrics_us);
            break;

        case op::kInvalidateVertexCache:
            sink.on_invalidate_vertex_cache();
            finish_profile(
                &FifoParserProfile::inval_vtx_count,
                &FifoParserProfile::inval_vtx_us);
            break;

        case op::kLoadBpReg: {
            const std::uint32_t command = cursor.read_u32();
            state.load_bp(command);

            const std::uint8_t  bp_reg = static_cast<std::uint8_t>(command >> 24);
            const std::uint32_t bp_val = state.bp(bp_reg);

            switch (bp_reg) {
            case bp::kCopyExecute:      // 0x52: PE_COPY_EXECUTE
                sink.on_efb_copy(bp_val);
                break;
            case bp::kPeDone:           // 0x45: GXDrawDone
                if ((bp_val & 0xFFu) == 2u) {
                    sink.on_pe_finish();
                }
                break;
            case bp::kPeToken:          // 0x47: draw-sync token (no interrupt)
                sink.on_pe_token(
                    static_cast<std::uint16_t>(bp_val & 0xFFFFu),
                    false);
                break;
            case bp::kPeTokenInt:       // 0x48: draw-sync token (with interrupt)
                sink.on_pe_token(
                    static_cast<std::uint16_t>(bp_val & 0xFFFFu),
                    true);
                break;
            case 0x66u:                 // GXInvalidateTexAll signal
                if (bp_val == 0u) {
                    sink.on_invalidate_textures();
                }
                break;
            case bp::kTlutDest:         // 0x65: TLUT transfer trigger
                sink.on_tlut_load();
                break;
            default:
                break;
            }
            finish_profile(
                &FifoParserProfile::bp_count,
                &FifoParserProfile::bp_us);
            break;
        }

        default:
            if (opcode >= op::kDrawFirst && opcode <= op::kDrawLast) {
                // bits 7:3 = primitive class, bits 2:0 = vertex format index.
                // PrimitiveClass enumerators are the shifted value (Quads=0x10).
                const auto prim   = static_cast<PrimitiveClass>(opcode >> 3u);
                const auto vtxfmt = static_cast<std::uint8_t>(opcode & 0x07u);
                FifoCursor probe = cursor;
                const std::uint16_t vertex_count = probe.read_u16();
                const auto payload_probe_start = profile != nullptr
                    ? std::chrono::steady_clock::now()
                    : std::chrono::steady_clock::time_point{};
                const std::size_t payload_size =
                    sink.draw_payload_size(vtxfmt, vertex_count);
                if (profile != nullptr) {
                    profile->draw_payload_probe_us += elapsed_fifo_us(
                        payload_probe_start,
                        std::chrono::steady_clock::now());
                    profile->draw_vertices += vertex_count;
                    profile->draw_payload_bytes += payload_size;
                }
                probe.require(payload_size);
                sink.on_draw(prim, vtxfmt, cursor);
                finish_profile(
                    &FifoParserProfile::draw_count,
                    &FifoParserProfile::draw_us);
            } else if (opcode <= 0x07u || opcode == 0x3Fu) {
                static std::atomic_uint s_hw_noop_logs{0u};
                const unsigned log_index = s_hw_noop_logs.fetch_add(
                    1u, std::memory_order_relaxed);
                if (log_index < 8u) {
                    std::fprintf(
                        stderr,
                        "[gx-parse] hw-noop op=0x%02X off=0x%zx\n",
                        static_cast<unsigned>(opcode),
                        opcode_offset);
                }
                // Single-byte commands the hardware ignores; not recorded for replay.
                finish_profile(
                    &FifoParserProfile::hw_noop_count,
                    &FifoParserProfile::hw_noop_us);
            } else {
                std::fprintf(
                    stderr,
                    "[gx-parse] unknown depth=%d base=0x%zx local=0x%zx "
                    "frame=0x%zx op=0x%02X remaining=0x%zx\n",
                    depth,
                    cursor.base_offset,
                    cursor.offset - 1u,
                    opcode_offset,
                    static_cast<unsigned>(opcode),
                    cursor.remaining());
                std::fprintf(
                    stderr,
                    "[gx-parse] cp vcdlo=0x%08X vcdhi=0x%08X "
                    "vat0=(0x%08X,0x%08X,0x%08X) strides:",
                    state.cp(cp::kVcdLo),
                    state.cp(cp::kVcdHi),
                    state.cp(cp::kVatABase + 0u),
                    state.cp(cp::kVatBBase + 0u),
                    state.cp(cp::kVatCBase + 0u));
                for (std::uint8_t fmt = 0; fmt < 8; ++fmt) {
                    std::size_t payload_size = 0;
                    try {
                        payload_size = sink.draw_payload_size(fmt, 1);
                    } catch (...) {
                        std::fprintf(
                            stderr, " f%u=unavailable",
                            static_cast<unsigned>(fmt));
                        continue;
                    }
                    std::fprintf(
                        stderr,
                        " f%u=%zu",
                        static_cast<unsigned>(fmt),
                        payload_size);
                }
                std::fprintf(stderr, "\n[gx-parse] window:");
                const std::size_t local = cursor.offset - 1u;
                const std::size_t begin =
                    local > 48u ? local - 48u : 0u;
                const std::size_t end =
                    std::min(cursor.data.size(), local + 49u);
                for (std::size_t i = begin; i < end; ++i) {
                    if ((i - begin) % 16u == 0u) {
                        std::fprintf(stderr, "\n[gx-parse] %06zx:", i);
                    }
                    std::fprintf(
                        stderr,
                        " %02X",
                        static_cast<unsigned>(
                            static_cast<std::uint8_t>(cursor.data[i])));
                }
                std::fprintf(stderr, "\n");
                char op_buf[4];
                std::snprintf(
                    op_buf, sizeof(op_buf), "%02X",
                    static_cast<unsigned>(opcode));
                throw GxFatalError(
                    std::string("GX FIFO: unknown opcode 0x") + op_buf,
                    opcode_offset,
                    opcode);
            }
            break;
        }
    }
}

}  // namespace galaxy::gx
