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
inline constexpr std::uint8_t kCallDisplayList = 0x40;
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

inline constexpr int kMaxDisplayListDepth = 4;

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

    // Brackets a cached run of `draw_count` draws with no state command between them, so the
    // sink can resolve draw state once; the parser still replays and checks each packet.
    // end_cached_draw_run follows only when this returns true.
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

    // Fast path for a cached run of draws with one primitive and vertex format whose packets can
    // be concatenated without changing the primitives drawn. Returning false makes the parser
    // replay the packets one by one.
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

    // The same run with its payload built once when the list is cached: a u16 total vertex
    // count followed by the concatenated vertex data.
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

    // Fast path for a cached run kept as separate packets (strips, fans): the sink draws every
    // packet in order.
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
    // Cache misses: parsing the list (nested commands included) and storing it for replay.
    std::uint64_t call_dl_miss_parse_us = 0;
    std::uint64_t call_dl_store_us = 0;
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
    // Nanosecond sums behind the replay *_us fields above, so sub-microsecond commands count.
    std::uint64_t call_dl_replay_state_ns = 0;
    std::uint64_t call_dl_replay_indx_ns = 0;
    std::uint64_t call_dl_replay_draw_ns = 0;
    std::uint64_t call_dl_replay_misc_ns = 0;
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
    // draws, EFB copies, PE events and invalidations go to `sink`. CALL_DL recurses up to
    // kMaxDisplayListDepth. Before a GxFatalError propagates, the FIFO is written to
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

    // PE event classification only (FramePeEventClassifier): XF and indexed
    // loads leave the state untouched, and a cached display list with no PE
    // register write replays as its CP writes and its last BP command. The
    // classifier reads only CP registers (draw sizes), the BP mask and the PE
    // registers, so its events and command boundaries are unchanged.
    void set_event_classification_only(bool enabled) {
        event_classification_only_ = enabled;
    }

private:
    struct FrozenRegionScope;
    const GuestMemory* frozen_sorted_memory_ = nullptr;
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
        // Event-classification summary, built on the first classify-only
        // replay: 0 unbuilt, 1 no PE register write, 2 full replay.
        mutable std::uint8_t classify_summary = 0;
        mutable bool classify_has_bp = false;
        mutable std::uint32_t classify_last_bp = 0;
        mutable std::vector<std::pair<std::uint8_t, std::uint32_t>> classify_cp_writes;
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
            // CALL_DL addresses and sizes are 32-byte aligned, so their low five bits are zero;
            // the splitmix64 finalizer spreads the key over power-of-two bucket tables.
            std::uint64_t hash = (std::uint64_t{key.guest_addr} << 32u) |
                key.byte_size;
            hash ^= hash >> 30u;
            hash *= 0xBF58476D1CE4E5B9ull;
            hash ^= hash >> 27u;
            hash *= 0x94D049BB133111EBull;
            hash ^= hash >> 31u;
            return static_cast<std::size_t>(hash);
        }
    };

    void run_window(
        FifoCursor& cursor,
        GuestMemory* memory,
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

    bool replay_event_free_display_list(
        const CachedDisplayList& cached,
        GxState& state) const;
    void replay_cached_display_list(
        const CachedDisplayList& cached,
        GuestMemory* memory,
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
    bool event_classification_only_ = false;
    std::uint64_t display_list_cache_tick_ = 0;
    std::uint64_t display_list_cache_token_ = 0;
    std::size_t last_display_list_cache_index_ = static_cast<std::size_t>(-1);
    // Gameplay calls hundreds of display lists per frame, all of which should stay cached. The
    // slots live on the heap because tests construct parsers as local variables.
    static constexpr std::size_t kDisplayListCacheCapacity = 2048;
    // One entry can hold a 4 MiB list plus a copy of its draw payloads, so the cache is bounded
    // in bytes as well as slots. 256 MiB holds the display lists of a loaded scene within a
    // low-end machine's memory.
    static constexpr std::uint64_t kDisplayListCacheBudgetBytes = 256ull << 20;
    // Retained vector capacity of every slot, invalid ones included.
    std::uint64_t cache_bytes_ = 0;
    std::vector<CachedDisplayList> display_list_cache_ =
        std::vector<CachedDisplayList>(kDisplayListCacheCapacity);
    // Slot choice for a new entry without scanning the cache: invalidated
    // slots, then never-used ones (index >= next_unused), then the least
    // recently used of a rotating sample.
    std::vector<std::uint32_t> display_list_free_slots_{};
    std::size_t display_list_next_unused_ = 0;
    std::size_t display_list_victim_cursor_ = 0;
    std::unordered_map<
        DisplayListCacheKey,
        std::vector<std::size_t>,
        DisplayListCacheKeyHash> display_list_cache_map_{};

    // Retained capacity of one entry's vectors.
    [[nodiscard]] static std::uint64_t display_list_cache_entry_bytes(
        const CachedDisplayList& cached) noexcept;
    // Frees invalid slots' storage, then evicts least-recently-used entries until cache_bytes_
    // fits kDisplayListCacheBudgetBytes. `skip_index`, the slot just filled, is kept even if it
    // alone exceeds the budget.
    void evict_display_list_cache_to_budget(std::size_t skip_index);
};

}  // namespace galaxy::gx
