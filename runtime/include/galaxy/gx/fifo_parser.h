#pragma once

// FifoParser: opcode dispatch over the captured GX FIFO byte stream.
//
// Hard-fail contract (repo rule): an unknown opcode, a truncated payload, or
// an unresolvable guest pointer throws GxFatalError after dumping the
// surrounding FIFO window to a .gxdump file replayable in GxSandbox. Unknown
// CP/XF/BP state writes are fatal before state mutation. Hardware-observed
// no-op command bytes remain accepted only where explicitly classified.
//
// Endianness: all FIFO payloads are big-endian.  FifoCursor provides checked
// scalar reads for opcodes and register payloads (a few bytes each) and a
// raw take() window so bulk consumers (VertexLoader) can run the SIMD
// byte-swap kernels below over whole vertex arrays instead of per-byte reads.
//
// No Windows/D3D12 includes — unit-testable offline.

#include "galaxy/gx/gx_bitfields.h"
#include "galaxy/gx/gx_state.h"
#include "galaxy/native_api.h"

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
inline constexpr std::uint8_t kCallDisplayList = 0x40;
inline constexpr std::uint8_t kUnknownMetrics = 0x44;
inline constexpr std::uint8_t kInvalidateVertexCache = 0x48;
inline constexpr std::uint8_t kLoadBpReg = 0x61;
inline constexpr std::uint8_t kDrawFirst = 0x80;
inline constexpr std::uint8_t kDrawLast = 0xBF;
}  // namespace op

inline constexpr int kMaxDisplayListDepth = 4;

// Fatal parse error.  Carries the stream offset and offending opcode so the
// .gxdump writer and the log line can pinpoint the failure.
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

// Internal streaming signal: the top-level capture ended in the middle of a
// command. VI retraces are not GX command boundaries, so the caller retains
// bytes from command_offset() and retries when more FIFO data arrives.
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

// ---------------------------------------------------------------------------
// Bulk big-endian -> little-endian conversion kernels.  Implementations use
// SSSE3 pshufb over 16-byte blocks with a scalar tail; vertex arrays are the
// hot path and must never go through per-byte scalar reads.
// ---------------------------------------------------------------------------

// Swaps `count` 16-bit values from src into dst (may not alias).
void swap16_block(
    const std::byte* src,
    std::uint16_t* dst,
    std::size_t count);

// Swaps `count` 32-bit values from src into dst (may not alias).
void swap32_block(
    const std::byte* src,
    std::uint32_t* dst,
    std::size_t count);

// ---------------------------------------------------------------------------
// FifoCursor: checked reads over one command stream (top-level FIFO buffer or
// a CALL_DL guest window).  All reads throw GxFatalError on underrun.
// ---------------------------------------------------------------------------
struct FifoCursor {
    std::span<const std::byte> data;
    std::size_t offset = 0;
    // Offset of this window within the frame's top-level FIFO capture (zero
    // for the top level itself); used for error reporting and .gxdump.
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

    // Hands out a raw window of `size` bytes and advances past it.  Bulk
    // consumers pair this with swap16_block/swap32_block; no per-byte loop.
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

struct CachedDrawPacket {
    std::uint8_t opcode = 0;
    std::uint16_t vertex_count = 0;
    std::size_t local_opcode_offset = 0;
    std::size_t draw_cursor_offset = 0;
    std::size_t draw_payload_size = 0;
};

struct CachedPreparedDrawRun {
    PrimitiveClass primitive = PrimitiveClass::Points;
    std::uint8_t vtxfmt = 0;
    std::uint8_t opcode = 0;
    std::uint16_t total_vertices = 0;
    std::size_t local_opcode_offset = 0;
    std::size_t source_draw_count = 0;
    std::size_t payload_offset = 0;
    std::size_t payload_size = 0;
};

struct CachedDrawPacketRun {
    PrimitiveClass primitive = PrimitiveClass::Points;
    std::uint8_t vtxfmt = 0;
    std::size_t packet_offset = 0;
    std::size_t packet_count = 0;
    std::uint32_t total_vertices = 0;
    std::uint32_t total_indices = 0;
    std::size_t index_offset = 0;
    std::size_t index_count = 0;
};

// ---------------------------------------------------------------------------
// FifoSink: parser callbacks for everything that is not a plain register
// load.  Register loads are routed directly into the GxState passed to run().
// GxBackend implements this interface.
// ---------------------------------------------------------------------------
struct FifoSink {
    virtual ~FifoSink() = default;

    // Byte count of the in-band vertex payload for a draw. The parser uses
    // this to verify that the complete command is present before invoking
    // on_draw, keeping callbacks transactional across capture boundaries.
    [[nodiscard]] virtual std::size_t draw_payload_size(
        std::uint8_t vtxfmt,
        std::uint16_t vertex_count) const = 0;

    // Draw command (0x80-0xBF).  The cursor is positioned at the u16 vertex
    // count; the implementation must consume the entire vertex payload
    // (normally by handing the cursor to VertexLoader).
    virtual void on_draw(
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        FifoCursor& cursor) = 0;

    // Cached display-list replay may contain long runs of draw packets with no
    // intervening state command. A sink can opt into a prepared run so it can
    // resolve immutable draw state once while the parser still replays each
    // packet in order and verifies exact cursor consumption.
    [[nodiscard]] virtual bool begin_cached_draw_run(
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        std::size_t draw_count) {
        (void)primitive;
        (void)vtxfmt;
        (void)draw_count;
        return false;
    }
    virtual void end_cached_draw_run() {}

    // Optional fast path for cached display-list runs made entirely of
    // consecutive compatible draw packets with identical VCD/VAT state. A
    // sink may concatenate packets whose primitive restart semantics are
    // preserved by concatenation; the parser falls back to exact
    // packet-by-packet replay when this returns false.
    [[nodiscard]] virtual bool on_cached_simple_draw_run(
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        std::span<const std::byte> bytes,
        std::size_t base_offset,
        std::span<const CachedDrawPacket> packets) {
        (void)primitive;
        (void)vtxfmt;
        (void)bytes;
        (void)base_offset;
        (void)packets;
        return false;
    }

    // Prepared form of the same fast path. The parser builds the synthetic
    // u16-count + concatenated vertex payload once when a display list enters
    // the cache; replay can then hand that exact payload to the sink without
    // allocating or copying per frame.
    [[nodiscard]] virtual bool on_cached_prepared_draw_run(
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        std::span<const std::byte> prepared_payload,
        std::size_t base_offset,
        std::size_t local_opcode_offset,
        std::uint8_t opcode,
        std::size_t source_draw_count) {
        (void)primitive;
        (void)vtxfmt;
        (void)prepared_payload;
        (void)base_offset;
        (void)local_opcode_offset;
        (void)opcode;
        (void)source_draw_count;
        return false;
    }

    // Packet-preserving fast path for cached runs that cannot be raw
    // concatenated (for example strips and fans). The sink must replay every
    // packet in order; this only removes parser dispatch overhead.
    [[nodiscard]] virtual bool on_cached_packet_draw_run(
        std::uint64_t cache_token,
        std::size_t packet_run_index,
        PrimitiveClass primitive,
        std::uint8_t vtxfmt,
        std::span<const std::byte> bytes,
        std::size_t base_offset,
        std::span<const CachedDrawPacket> packets,
        std::uint32_t total_vertices,
        std::uint32_t total_indices,
        std::span<const std::uint16_t> precomputed_indices) {
        (void)cache_token;
        (void)packet_run_index;
        (void)primitive;
        (void)vtxfmt;
        (void)bytes;
        (void)base_offset;
        (void)packets;
        (void)total_vertices;
        (void)total_indices;
        (void)precomputed_indices;
        return false;
    }

    // BP 0x52 PE_COPY_EXECUTE write; `exec_command` is the 24-bit value.
    // Combine with GxState::efb_copy(exec_command) for the full snapshot.
    virtual void on_efb_copy(std::uint32_t exec_command) = 0;

    // BP 0x45 / 0x47 / 0x48 — GXDrawDone and draw-sync tokens.  These must be
    // routed back into the runtime's PE interrupt machinery.
    virtual void on_pe_finish() = 0;
    virtual void on_pe_token(std::uint16_t token, bool interrupt) = 0;

    // The BP write pattern emitted by GXInvalidateTexAll(); the only texture
    // invalidation signal (explicit invalidation, no content hashing).
    virtual void on_invalidate_textures() = 0;

    // BP 0x65 (TLUT dest/trigger) was written: a palette transfer from guest
    // memory (source latched in BP 0x64) into the TMEM TLUT bank must execute
    // now.  Default no-op so offline replay sinks need not care.
    virtual void on_tlut_load() {}

    // Opcode 0x48 (vertex cache invalidate) — informational.
    virtual void on_invalidate_vertex_cache() = 0;
};

struct FifoParserProfile {
    struct CallDisplayListEntry {
        std::uint32_t guest_addr = 0;
        std::uint32_t byte_size = 0;
        std::uint64_t count = 0;
        std::uint64_t us = 0;
        std::uint64_t bytes = 0;
    };

    static constexpr std::size_t kCallDisplayListEntryCapacity = 32;

    std::uint64_t command_count = 0;
    std::uint64_t command_us = 0;

    std::uint64_t nop_count = 0;
    std::uint64_t nop_us = 0;
    std::uint64_t cp_count = 0;
    std::uint64_t cp_us = 0;
    std::uint64_t xf_count = 0;
    std::uint64_t xf_us = 0;
    std::uint64_t indx_count = 0;
    std::uint64_t indx_us = 0;
    std::uint64_t call_dl_count = 0;
    std::uint64_t call_dl_us = 0;
    std::uint64_t call_dl_bytes = 0;
    std::uint64_t call_dl_cache_hits = 0;
    std::uint64_t call_dl_cache_misses = 0;
    std::uint64_t call_dl_cache_stores = 0;
    std::uint64_t call_dl_cache_uncacheable = 0;
    std::uint64_t call_dl_replay_command_count = 0;
    std::uint64_t call_dl_replay_command_us = 0;
    std::uint64_t call_dl_replay_prepared_run_count = 0;
    std::uint64_t call_dl_replay_prepared_source_draw_count = 0;
    std::uint64_t call_dl_replay_prepared_payload_bytes = 0;
    std::uint64_t call_dl_replay_packet_run_count = 0;
    std::uint64_t call_dl_replay_packet_source_draw_count = 0;
    std::uint64_t call_dl_replay_state_count = 0;
    std::uint64_t call_dl_replay_state_us = 0;
    std::uint64_t call_dl_replay_indx_count = 0;
    std::uint64_t call_dl_replay_indx_us = 0;
    std::uint64_t call_dl_replay_draw_count = 0;
    std::uint64_t call_dl_replay_draw_us = 0;
    std::uint64_t call_dl_replay_misc_count = 0;
    std::uint64_t call_dl_replay_misc_us = 0;
    std::uint64_t call_dl_profile_overflow = 0;
    CallDisplayListEntry
        call_dl_entries[kCallDisplayListEntryCapacity]{};
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
    virtual void record_guest_memory_read(
        std::uint32_t guest_addr,
        std::uint32_t size) = 0;
    // The render-side ordered capture needs the exact bytes at the instant
    // CALL_DL or LOAD_INDX resolves them. Legacy range scans keep the cheaper
    // address/size callback through this default forwarding method.
    virtual void record_guest_memory_read_bytes(
        std::uint32_t guest_addr,
        std::span<const std::byte> bytes) {
        record_guest_memory_read(
            guest_addr, static_cast<std::uint32_t>(bytes.size()));
    }
    virtual void record_guest_dependency_shape_read(
        std::uint32_t guest_addr,
        std::uint32_t size) {
        (void)guest_addr;
        (void)size;
    }
};

// ---------------------------------------------------------------------------
// FifoParser.
// ---------------------------------------------------------------------------
class FifoParser {
public:
    // Parses one frame's accumulated FIFO bytes.  Register loads mutate
    // `state` (including LOAD_INDX resolution through `memory`); draws, EFB
    // copies, PE events, and invalidations are dispatched to `sink`.
    // CALL_DL recurses into guest memory windows up to kMaxDisplayListDepth.
    //
    // Throws GxFatalError per the hard-fail contract; before throwing, the
    // offending window is written to `<dump_dir>/frame<N>.gxdump`.
    void run(
        std::span<const std::byte> fifo,
        GuestMemoryV1* memory,
        FifoSink& sink,
        GxState& state);

    // Parses all complete top-level commands. If the capture ends in the
    // middle of a command, returns that command's start offset so the caller
    // can retain the tail. Malformed input and CALL_DL windows remain fatal.
    [[nodiscard]] std::size_t run_available(
        std::span<const std::byte> fifo,
        GuestMemoryV1* memory,
        FifoSink& sink,
        GxState& state);

    // Directory for .gxdump failure artifacts (default: current directory).
    void set_dump_dir(std::string dump_dir);

    // Optional per-run profiler. The parser never owns this pointer; callers
    // set it only while gathering diagnostics.
    //
    // Per-command elapsed time is the one part of this profile that is NOT
    // free: `run_window` brackets every FIFO command with two steady_clock
    // reads, and a busy frame issues 40k+ commands, so the clocks themselves
    // dominate the number they are meant to report. Published recordings show
    // `fifo-command-us` exceeding the frame's whole parse time, which is the
    // instrumentation, not the parser. The category/count fields stay
    // unconditional (plain increments, valid with routine monitoring); the
    // timestamps are taken only when GALAXY_TRACE_GX_MICROPROFILE asks for
    // them, so a routine-monitoring run measures the parser instead of itself.
    void set_profile(FifoParserProfile* profile) { profile_ = profile; }

    // Optional read recorder for dependency scanning. The parser never owns
    // this pointer; normal rendering leaves it null.
    void set_memory_read_recorder(FifoGuestMemoryReadRecorder* recorder) {
        memory_read_recorder_ = recorder;
    }

    // Invalidates cached CALL_DL replays whose guest byte window overlaps a
    // dirty guest-memory range. The address may be physical or a MEM1/MEM2
    // cached/uncached CPU alias.
    std::size_t invalidate_display_list_cache_range(
        std::uint32_t guest_addr,
        std::uint32_t size);

private:
    struct CachedDisplayListCommand {
        enum class Kind : std::uint8_t {
            Nop,
            LoadCp,
            LoadXf,
            LoadIndx,
            Metrics,
            InvalidateVertexCache,
            LoadBp,
            Draw,
            HardwareNoop,
        };

        Kind kind = Kind::Nop;
        std::uint8_t opcode = 0;
        std::uint8_t reg = 0;
        std::uint8_t array_slot = 0;
        std::uint8_t vtxfmt = 0;
        PrimitiveClass primitive = PrimitiveClass::Points;
        std::uint16_t count = 0;
        std::uint16_t base = 0;
        std::uint16_t idx = 0;
        std::uint16_t length = 0;
        std::uint16_t xf_addr = 0;
        std::uint16_t vertex_count = 0;
        std::uint32_t value = 0;
        std::uint32_t command = 0;
        std::size_t values_offset = 0;
        std::size_t draw_cursor_offset = 0;
        std::size_t draw_payload_size = 0;
        std::size_t local_opcode_offset = 0;
        std::size_t prepared_run_index =
            static_cast<std::size_t>(-1);
        std::size_t prepared_run_command_count = 0;
        std::size_t packet_run_index = static_cast<std::size_t>(-1);
        std::size_t packet_run_command_count = 0;
    };

    [[nodiscard]] static bool
    cached_draw_run_concatenation_preserves_primitive(
        PrimitiveClass primitive,
        std::span<const CachedDisplayListCommand> commands);

    struct DisplayListRecording {
        bool cacheable = true;
        std::array<bool, 0x100> cp_written{};
        std::array<bool, 0x100> cp_dependency_recorded{};
        std::vector<std::byte> bytes;
        std::vector<CachedDisplayListCommand> commands;
        std::vector<std::pair<std::uint8_t, std::uint32_t>> cp_dependencies;
        std::vector<std::uint32_t> xf_values;
    };

    struct CachedDisplayList {
        bool valid = false;
        std::uint64_t cache_token = 0;
        std::uint32_t guest_addr = 0;
        std::uint32_t byte_size = 0;
        std::uint64_t first_page = 0;
        std::uint64_t last_page = 0;
        std::uint64_t last_used = 0;
        std::vector<std::byte> bytes;
        std::vector<CachedDisplayListCommand> commands;
        std::vector<std::pair<std::uint8_t, std::uint32_t>> cp_dependencies;
        std::vector<std::uint32_t> xf_values;
        std::vector<CachedPreparedDrawRun> prepared_draw_runs;
        std::vector<std::byte> prepared_draw_payloads;
        std::vector<CachedDrawPacketRun> draw_packet_runs;
        std::vector<CachedDrawPacket> draw_run_packets;
        std::vector<std::uint16_t> draw_run_indices;
    };

    struct DisplayListCacheKey {
        std::uint32_t guest_addr = 0;
        std::uint32_t byte_size = 0;

        [[nodiscard]] bool operator==(
            const DisplayListCacheKey& other) const noexcept {
            return guest_addr == other.guest_addr &&
                byte_size == other.byte_size;
        }
    };

    struct DisplayListCacheKeyHash {
        [[nodiscard]] std::size_t operator()(
            const DisplayListCacheKey& key) const noexcept {
            std::uint64_t hash = 1469598103934665603ull;
            auto mix = [&hash](std::uint64_t value) {
                hash ^= value;
                hash *= 1099511628211ull;
            };
            mix(key.guest_addr);
            mix(key.byte_size);
            return static_cast<std::size_t>(hash);
        }
    };

    void run_window(
        FifoCursor& cursor,
        GuestMemoryV1* memory,
        FifoSink& sink,
        GxState& state,
        int depth,
        DisplayListRecording* recording);

    [[nodiscard]] CachedDisplayList* find_cached_display_list(
        std::uint32_t guest_addr,
        std::uint32_t byte_size,
        const GxState& state,
        std::span<const std::byte> bytes);

    void store_cached_display_list(
        std::uint32_t guest_addr,
        std::uint32_t byte_size,
        DisplayListRecording&& recording);
    void prepare_cached_display_list_draw_runs(CachedDisplayList& cached) const;

    void erase_display_list_cache_index_from_maps(std::size_t index);
    void invalidate_display_list_cache_index(std::size_t index);
    void index_display_list_cache_entry(std::size_t index);

    void replay_cached_display_list(
        const CachedDisplayList& cached,
        GuestMemoryV1* memory,
        FifoSink& sink,
        GxState& state,
        std::size_t opcode_offset) const;

    void record_display_list_cp_dependency(
        DisplayListRecording* recording,
        const GxState& state,
        std::uint8_t reg) const;
    void record_display_list_draw_dependencies(
        DisplayListRecording* recording,
        const GxState& state,
        std::uint8_t vtxfmt) const;
    [[nodiscard]] bool display_list_dependencies_match(
        const CachedDisplayList& cached,
        const GxState& state) const;

    // Resolves a guest physical address range to host memory; throws
    // GxFatalError if the range is unmapped.
    [[nodiscard]] std::span<const std::byte> resolve_guest(
        GuestMemoryV1* memory,
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
    std::uint64_t display_list_cache_tick_ = 0;
    std::uint64_t display_list_cache_token_ = 0;
    std::size_t last_display_list_cache_index_ = static_cast<std::size_t>(-1);
    // SMG gameplay commonly touches hundreds of small display lists per frame.
    // A 64-entry cache thrashed in the first playable scene, forcing the CPU to
    // re-parse roughly half of the nested list stream every frame.
    // Keep the fixed-capacity cache off the parser object's stack; each entry
    // owns several replay vectors, and diagnostics/tests construct parsers as
    // local variables.
    static constexpr std::size_t kDisplayListCacheCapacity = 2048;
    // The entry count alone is not a memory bound: one entry may hold a 4 MiB
    // list plus a duplicate of every draw payload, so a full cache can retain
    // gigabytes. `cache_bytes_` sums the retained vector CAPACITY of every valid
    // entry, and a store that would exceed the budget evicts least-recently-used
    // entries. Eviction only costs a later re-parse of that list.
    std::uint64_t cache_bytes_ = 0;
    std::vector<CachedDisplayList> display_list_cache_ =
        std::vector<CachedDisplayList>(kDisplayListCacheCapacity);
    std::unordered_map<
        DisplayListCacheKey,
        std::vector<std::size_t>,
        DisplayListCacheKeyHash> display_list_cache_map_{};
    std::unordered_map<std::uint64_t, std::vector<std::size_t>>
        display_list_cache_page_map_{};

    [[nodiscard]] std::uint64_t display_list_cache_budget_bytes() const;
    // Sums the retained capacity of one cached entry. Pure observation: it must
    // be callable for a live entry without changing any cache state.
    [[nodiscard]] static std::uint64_t display_list_cache_entry_bytes(
        const CachedDisplayList& cached) noexcept;
    // Drops least-recently-used valid entries until cache_bytes_ fits the
    // budget. `skip_index` is never evicted, so a caller that just filled that
    // slot keeps its entry even when the entry alone exceeds the budget.
    void evict_display_list_cache_to_budget(std::size_t skip_index);
    // Releases every cached entry and its retained capacity. Owning a parser
    // for a new renderer session must not inherit the previous session's lists.
    void clear_display_list_cache();
};

}  // namespace galaxy::gx
