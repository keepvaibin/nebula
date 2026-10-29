#pragma once

// GX FIFO command parser: dispatches the big-endian command stream to GxState and a FifoSink.
//
// An unknown opcode, a truncated payload or an unmapped guest pointer throws GxFatalError after
// the FIFO is written to a .gxdump file. Hardware no-op opcodes are accepted only where listed.

#include "galaxy/gx/gx_bitfields.h"
#include "galaxy/gx/gx_state.h"
#include "galaxy/guest_memory.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace galaxy::gx {

// FIFO opcodes (high byte of each command).
namespace op {
inline constexpr std::uint8_t kNop = 0x00;
inline constexpr std::uint8_t kLoadCpReg = 0x08;
inline constexpr std::uint8_t kLoadXfReg = 0x10;
inline constexpr std::uint8_t kLoadIndxA = 0x20;
inline constexpr std::uint8_t kLoadIndxB = 0x28;
inline constexpr std::uint8_t kLoadIndxC = 0x30;
inline constexpr std::uint8_t kLoadIndxD = 0x38;
inline constexpr std::uint8_t kUnknownMetrics = 0x44;
inline constexpr std::uint8_t kInvalidateVertexCache = 0x48;
inline constexpr std::uint8_t kLoadBpReg = 0x61;
inline constexpr std::uint8_t kDrawFirst = 0x80;
inline constexpr std::uint8_t kDrawLast = 0xBF;
// Host draw-identity marker, top level only (the runtime inserts it; the game
// never writes this opcode): u8 kind, u16 aux, u32 a, u32 b.
inline constexpr std::uint8_t kHostMarker = 0xF0;
inline constexpr std::size_t kHostMarkerBytes = 12u;
}  // namespace op

// Fatal parse error, with the FIFO offset and opcode of the failure.
class GxFatalError : public std::runtime_error {
public:
    GxFatalError(
        std::string message,
        std::size_t fifo_offset,
        std::uint8_t opcode)
        : std::runtime_error(std::move(message)),
          fifo_offset_(fifo_offset),
          opcode_(opcode) {}

    [[nodiscard]] std::size_t fifo_offset() const { return fifo_offset_; }
    [[nodiscard]] std::uint8_t opcode() const { return opcode_; }

private:
    std::size_t fifo_offset_;
    std::uint8_t opcode_;
};

// The top-level capture ended inside a command. VI retraces are not command boundaries, so
// the caller keeps the bytes from command_offset() and retries when more FIFO data arrives.
class FifoNeedMoreData final : public std::exception {
public:
    explicit FifoNeedMoreData(std::size_t command_offset)
        : command_offset_(command_offset) {}

    [[nodiscard]] std::size_t command_offset() const {
        return command_offset_;
    }

private:
    std::size_t command_offset_;
};

// Byte-swaps `count` big-endian 32-bit values from src into dst (no aliasing), 16 bytes per
// vector with a scalar tail.
void swap32_block(
    const std::byte* src,
    std::uint32_t* dst,
    std::size_t count);

// Checked big-endian reads over one command stream (the top-level FIFO or a CALL_DL window).
// An underrun throws GxFatalError, or FifoNeedMoreData when recover_truncation is set.
struct FifoCursor {
    std::span<const std::byte> data;
    std::size_t offset = 0;
    // Added to `offset` in error reports: 0 at top level, the CALL_DL opcode offset in a list.
    std::size_t base_offset = 0;
    std::size_t command_offset = 0;
    bool recover_truncation = false;

    [[nodiscard]] std::size_t remaining() const {
        return data.size() - offset;
    }
    [[nodiscard]] bool empty() const { return offset >= data.size(); }

    std::uint8_t read_u8() {
        require(1);
        return static_cast<std::uint8_t>(data[offset++]);
    }
    std::uint16_t read_u16() {
        require(2);
        const auto hi = static_cast<std::uint16_t>(data[offset]);
        const auto lo = static_cast<std::uint16_t>(data[offset + 1]);
        offset += 2;
        return static_cast<std::uint16_t>((hi << 8) | lo);
    }
    std::uint32_t read_u32() {
        require(4);
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v = (v << 8) | static_cast<std::uint32_t>(data[offset + i]);
        }
        offset += 4;
        return v;
    }

    // Returns the next `size` bytes unswapped, for bulk consumers, and advances past them.
    std::span<const std::byte> take(std::size_t size) {
        require(size);
        const auto window = data.subspan(offset, size);
        offset += size;
        return window;
    }

    void require(std::size_t size) const {
        if (data.size() - offset < size) {
            if (recover_truncation) {
                throw FifoNeedMoreData(command_offset);
            }
            throw GxFatalError(
                "GX FIFO truncated payload",
                base_offset + offset,
                offset < data.size()
                    ? static_cast<std::uint8_t>(data[offset])
                    : 0u);
        }
    }
};

// Parser callbacks for everything but register loads, which go straight to the GxState passed
// to run(). Implemented by GxBackend.
struct FifoSink {
    virtual ~FifoSink() = default;

    // Host marker (op::kHostMarker). Draw identity, the game object whose
    // commands follow: 1 J3D shape packet, 2 NameObj draw, 3 JPA emitter
    // pass, 4 JPA particle. Motion signals (galaxy/motion_signals.h): 5 draw
    // category or scene phase, 6 scene signals, 7 pass, 8 light load.
    virtual void on_host_marker(std::uint8_t kind, std::uint16_t aux, std::uint32_t a,
                                std::uint32_t b) {
        (void)kind;
        (void)aux;
        (void)a;
        (void)b;
    }

    // Byte count of a draw's inline vertex payload. The parser checks that the whole command
    // is present before calling on_draw, so no callback sees a command cut by a capture boundary.
    [[nodiscard]] virtual std::size_t draw_payload_size(
        std::uint8_t vtxfmt,
        std::uint16_t vertex_count) const = 0;

    // Draw command (0x80-0xBF). The cursor is at the u16 vertex count; the implementation must
    // consume the whole vertex payload (normally through VertexLoader).
    virtual void on_draw(
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        FifoCursor& cursor) = 0;

    // BP 0x52 PE_COPY_EXECUTE write; `exec_command` is the 24-bit value.
    // Combine with GxState::efb_copy(exec_command) for the full snapshot.
    virtual void on_efb_copy(std::uint32_t exec_command) = 0;

    // BP 0x45 / 0x47 / 0x48: GXDrawDone and draw-sync tokens, raised as PE interrupts.
    virtual void on_pe_finish() = 0;
    virtual void on_pe_token(std::uint16_t token, bool interrupt) = 0;

    // BP 0x66 written with 0, as GXInvalidateTexAll() does.
    virtual void on_invalidate_textures() = 0;

    // BP 0x65 written: the palette at the BP 0x64 address loads into TMEM now.
    virtual void on_tlut_load() {}

    // Opcode 0x48 (vertex cache invalidate): informational.
    virtual void on_invalidate_vertex_cache() = 0;
};

struct FifoParserProfile {
    std::uint64_t command_count = 0;
    std::uint64_t command_us = 0;

    std::uint64_t nop_count = 0;
    std::uint64_t nop_us = 0;
    std::uint64_t cp_count = 0;
    std::uint64_t cp_us = 0;
    std::uint64_t xf_count = 0;
    std::uint64_t xf_us = 0;
    std::uint64_t metrics_count = 0;
    std::uint64_t metrics_us = 0;
    std::uint64_t inval_vtx_count = 0;
    std::uint64_t inval_vtx_us = 0;
    std::uint64_t bp_count = 0;
    std::uint64_t bp_us = 0;
    std::uint64_t draw_count = 0;
    std::uint64_t draw_us = 0;
    std::uint64_t draw_payload_probe_us = 0;
    std::uint64_t draw_vertices = 0;
    std::uint64_t draw_payload_bytes = 0;
    std::uint64_t hw_noop_count = 0;
    std::uint64_t hw_noop_us = 0;

    void reset() { *this = {}; }
};

struct FifoGuestMemoryReadRecorder {
    virtual ~FifoGuestMemoryReadRecorder() = default;
    // A guest range resolved for CALL_DL or LOAD_INDX.
    virtual void record_guest_memory_read(
        std::uint32_t guest_addr,
        std::uint32_t size) = 0;
    virtual void record_guest_dependency_shape_read(
        std::uint32_t guest_addr,
        std::uint32_t size) {
        (void)guest_addr;
        (void)size;
    }
};

class FifoParser {
public:

    // Parses one frame's FIFO bytes. Register loads update `state` (LOAD_INDX reads `memory`);
    // draws, EFB copies, PE events and invalidations go to `sink`; a CALL_DL (nothing sends one) is an unknown
    // command. Before a GxFatalError propagates, the FIFO is written to
    // `<dump_dir>/frame<N>.gxdump`.
    //
    // `frozen_regions` promises that memory's region table does not change during the call,
    // sink and recorder callbacks included. The parser then checks once that the regions are
    // sorted and disjoint and resolves pointers by binary search; otherwise the first region
    // containing the range wins. Nested calls restore the enclosing setting.
    void run(
        std::span<const std::byte> fifo,
        GuestMemory* memory,
        FifoSink& sink,
        GxState& state,
        bool frozen_regions = false);
    // Parses every complete top-level command and returns the offset of the first incomplete
    // one (the end when there is none) for the caller to retain. A truncated CALL_DL window
    // is still fatal.
    [[nodiscard]] std::size_t run_available(
        std::span<const std::byte> fifo,
        GuestMemory* memory,
        FifoSink& sink,
        GxState& state,
        bool frozen_regions = false);

    // Directory for .gxdump files (default: current directory).
    void set_dump_dir(std::string dump_dir);

    // Optional profiler, not owned. Null (the default) skips all counting and per-command
    // clocks. Cached-replay categories include sink work.
    void set_profile(FifoParserProfile* profile) { profile_ = profile; }

    // Optional read recorder for dependency scanning, not owned; null in normal rendering.
    void set_memory_read_recorder(FifoGuestMemoryReadRecorder* recorder) {
        memory_read_recorder_ = recorder;
    }

private:
    struct FrozenRegionScope;
    const GuestMemory* frozen_sorted_memory_ = nullptr;
    void run_window(
        FifoCursor& cursor,
        FifoSink& sink,
        GxState& state,
        int depth);

    // Resolves a guest physical address range to host memory; throws
    // GxFatalError if the range is unmapped.
    [[nodiscard]] std::span<const std::byte> resolve_guest(
        GuestMemory* memory,
        std::uint32_t guest_addr,
        std::uint32_t size,
        const FifoCursor& at) const;

    void write_dump(
        std::span<const std::byte> fifo,
        std::size_t failure_offset) const;

    std::string dump_dir_ = ".";
    std::uint64_t frame_index_ = 0;
    FifoParserProfile* profile_ = nullptr;
    FifoGuestMemoryReadRecorder* memory_read_recorder_ = nullptr;
};

}  // namespace galaxy::gx
