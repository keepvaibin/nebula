// GX FIFO command parser and its cache of parsed display lists.

#include "galaxy/gx/fifo_parser.h"
#include "galaxy/gx/snapshot_region_lookup.h"

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

static constexpr std::uint32_t kMaxCachedDisplayListBytes = 4u * 1024u * 1024u;

std::uint64_t elapsed_fifo_us(
    std::chrono::steady_clock::time_point start,
    std::chrono::steady_clock::time_point end) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            end - start).count());
}

void record_call_display_list_profile(
    FifoParserProfile& profile,
    std::uint32_t guest_addr,
    std::uint32_t byte_size,
    std::uint64_t elapsed_us) {
    FifoParserProfile::CallDisplayListEntry* empty = nullptr;
    auto* lowest = &profile.call_dl_entries[0];

    for (auto& entry : profile.call_dl_entries) {
        if (entry.count != 0 && entry.guest_addr == guest_addr &&
            entry.byte_size == byte_size) {
            ++entry.count;
            entry.us += elapsed_us;
            entry.bytes += byte_size;
            return;
        }
        if (entry.count == 0 && empty == nullptr) {
            empty = &entry;
        }
        if (entry.count != 0 && entry.us < lowest->us) {
            lowest = &entry;
        }
    }

    FifoParserProfile::CallDisplayListEntry* target = empty;
    if (target == nullptr) {
        ++profile.call_dl_profile_overflow;
        if (elapsed_us <= lowest->us) {
            return;
        }
        target = lowest;
    }

    target->guest_addr = guest_addr;
    target->byte_size = byte_size;
    target->count = 1;
    target->us = elapsed_us;
    target->bytes = byte_size;
}

[[nodiscard]] std::uint8_t canonical_cp_register(std::uint8_t reg) noexcept {
    switch (reg & 0xF0u) {
    case cp::kMatrixIndexA:
        return cp::kMatrixIndexA;
    case cp::kMatrixIndexB:
        return cp::kMatrixIndexB;
    case cp::kVcdLo:
        return cp::kVcdLo;
    case cp::kVcdHi:
        return cp::kVcdHi;
    case cp::kVatABase:
        return static_cast<std::uint8_t>(cp::kVatABase + (reg & 0x07u));
    case cp::kVatBBase:
        return static_cast<std::uint8_t>(cp::kVatBBase + (reg & 0x07u));
    case cp::kVatCBase:
        return static_cast<std::uint8_t>(cp::kVatCBase + (reg & 0x07u));
    case cp::kArrayBaseBase:
        return static_cast<std::uint8_t>(cp::kArrayBaseBase + (reg & 0x0Fu));
    case cp::kArrayStrideBase:
        return static_cast<std::uint8_t>(cp::kArrayStrideBase + (reg & 0x0Fu));
    default:
        return reg;
    }
}

void FifoParser::record_display_list_cp_dependency(
    DisplayListRecording* recording,
    const GxState& state,
    std::uint8_t reg) const {
    if (recording == nullptr || !recording->cacheable) {
        return;
    }

    const std::uint8_t canonical = canonical_cp_register(reg);
    if (recording->cp_written[canonical] ||
        recording->cp_dependency_recorded[canonical]) {
        return;
    }

    recording->cp_dependency_recorded[canonical] = true;
    recording->cp_dependencies.emplace_back(canonical, state.cp(canonical));
}

void FifoParser::record_display_list_draw_dependencies(
    DisplayListRecording* recording,
    const GxState& state,
    std::uint8_t vtxfmt) const {
    record_display_list_cp_dependency(recording, state, cp::kVcdLo);
    record_display_list_cp_dependency(recording, state, cp::kVcdHi);
    record_display_list_cp_dependency(
        recording,
        state,
        static_cast<std::uint8_t>(cp::kVatABase + vtxfmt));
    record_display_list_cp_dependency(
        recording,
        state,
        static_cast<std::uint8_t>(cp::kVatBBase + vtxfmt));
    record_display_list_cp_dependency(
        recording,
        state,
        static_cast<std::uint8_t>(cp::kVatCBase + vtxfmt));
}

bool FifoParser::display_list_dependencies_match(
    const CachedDisplayList& cached,
    const GxState& state) const {
    for (const auto& [reg, value] : cached.cp_dependencies) {
        if (state.cp(reg) != value) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::uint32_t normalize_display_list_guest_addr(
    std::uint32_t address) noexcept {
    if (address >= 0x80000000u && address < 0x81800000u) {
        return address - 0x80000000u;
    }
    if (address >= 0xC0000000u && address < 0xC1800000u) {
        return address - 0xC0000000u;
    }
    if (address >= 0x90000000u && address < 0x94000000u) {
        return 0x10000000u + (address - 0x90000000u);
    }
    if (address >= 0xD0000000u && address < 0xD4000000u) {
        return 0x10000000u + (address - 0xD0000000u);
    }
    return address;
}

enum class CachedReplayProfileKind : std::uint8_t {
    Misc,
    State,
    Indx,
    Draw,
};

// Categories include sink work; the caller records the replay total. The us fields are
// derived from nanosecond sums.
void record_cached_replay_profile(
    FifoParserProfile& profile, CachedReplayProfileKind kind,
    std::uint64_t command_count, std::uint64_t elapsed_ns) {
    profile.call_dl_replay_command_count += command_count;
    auto record = [&](std::uint64_t& count, std::uint64_t& ns, std::uint64_t& us) {
        count += command_count;
        ns += elapsed_ns;
        us = ns / 1'000u;
    };
    switch (kind) {
    case CachedReplayProfileKind::State:
        record(profile.call_dl_replay_state_count,
               profile.call_dl_replay_state_ns, profile.call_dl_replay_state_us);
        break;
    case CachedReplayProfileKind::Indx:
        record(profile.call_dl_replay_indx_count,
               profile.call_dl_replay_indx_ns, profile.call_dl_replay_indx_us);
        break;
    case CachedReplayProfileKind::Draw:
        record(profile.call_dl_replay_draw_count,
               profile.call_dl_replay_draw_ns, profile.call_dl_replay_draw_us);
        break;
    case CachedReplayProfileKind::Misc:
        record(profile.call_dl_replay_misc_count,
               profile.call_dl_replay_misc_ns, profile.call_dl_replay_misc_us);
        break;
    }
}

struct CachedReplayCommandScope {
    FifoParserProfile* profile;
    CachedReplayProfileKind kind;
    const std::size_t& command_index;
    std::size_t first_index;
    std::chrono::steady_clock::time_point start;

    ~CachedReplayCommandScope() noexcept {
        if (profile != nullptr) {
            const auto elapsed = std::chrono::steady_clock::now() - start;
            record_cached_replay_profile(*profile, kind,
                command_index - first_index,
                static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        elapsed).count()));
        }
    }
};

bool FifoParser::cached_draw_run_concatenation_preserves_primitive(
    PrimitiveClass primitive,
    std::span<const FifoParser::CachedDisplayListCommand> commands) {
    switch (primitive) {
    case PrimitiveClass::Triangles:
        return std::all_of(
            commands.begin(), commands.end(),
            [](const FifoParser::CachedDisplayListCommand& command) {
                return (command.vertex_count % 3u) == 0u;
            });
    case PrimitiveClass::Points:
        return true;
    case PrimitiveClass::Quads:
    case PrimitiveClass::Quads2:
        return std::all_of(
            commands.begin(),
            commands.end(),
            [](const FifoParser::CachedDisplayListCommand& command) {
                return (command.vertex_count % 4u) == 0u;
            });
    case PrimitiveClass::Lines:
        return std::all_of(
            commands.begin(),
            commands.end(),
            [](const FifoParser::CachedDisplayListCommand& command) {
                return (command.vertex_count % 2u) == 0u;
            });
    case PrimitiveClass::TriangleStrip:
    case PrimitiveClass::TriangleFan:
    case PrimitiveClass::LineStrip:
        return false;
    }
    return false;
}

std::uint32_t cached_packet_run_index_count(
    PrimitiveClass primitive,
    std::uint32_t vertex_count) {
    switch (primitive) {
    case PrimitiveClass::Quads:
    case PrimitiveClass::Quads2:
        return (vertex_count / 4u) * 6u;
    case PrimitiveClass::Triangles:
        return (vertex_count / 3u) * 3u;
    case PrimitiveClass::TriangleStrip:
    case PrimitiveClass::TriangleFan:
        return vertex_count >= 3u ? (vertex_count - 2u) * 3u : 0u;
    case PrimitiveClass::Lines:
        return (vertex_count / 2u) * 2u;
    case PrimitiveClass::LineStrip:
        return vertex_count >= 2u ? (vertex_count - 1u) * 2u : 0u;
    case PrimitiveClass::Points:
        return vertex_count;
    }
    return 0u;
}

bool append_cached_packet_indices(
    PrimitiveClass primitive,
    std::uint32_t base_vertex,
    std::uint32_t vertex_count,
    std::vector<std::uint16_t>& out) {
    const auto emit_index = [&](std::uint32_t value) -> bool {
        if (value > std::numeric_limits<std::uint16_t>::max()) {
            return false;
        }
        out.push_back(static_cast<std::uint16_t>(value));
        return true;
    };
    const auto emit = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
        return emit_index(base_vertex + a) &&
            emit_index(base_vertex + b) &&
            emit_index(base_vertex + c);
    };
    const auto emit2 = [&](std::uint32_t a, std::uint32_t b) {
        return emit_index(base_vertex + a) && emit_index(base_vertex + b);
    };

    switch (primitive) {
    case PrimitiveClass::Quads:
    case PrimitiveClass::Quads2: {
        const std::uint32_t n = (vertex_count / 4u) * 4u;
        for (std::uint32_t i = 0; i < n; i += 4u) {
            if (!emit(i, i + 1u, i + 2u) ||
                !emit(i, i + 2u, i + 3u)) {
                return false;
            }
        }
        return true;
    }
    case PrimitiveClass::Triangles:
        for (std::uint32_t i = 0; i < (vertex_count / 3u) * 3u; ++i) {
            if (!emit_index(base_vertex + i)) {
                return false;
            }
        }
        return true;
    case PrimitiveClass::TriangleStrip:
        for (std::uint32_t i = 0; i + 2u < vertex_count; ++i) {
            const bool ok = (i & 1u) == 0u
                ? emit(i, i + 1u, i + 2u)
                : emit(i + 1u, i, i + 2u);
            if (!ok) {
                return false;
            }
        }
        return true;
    case PrimitiveClass::TriangleFan:
        for (std::uint32_t i = 1; i + 1u < vertex_count; ++i) {
            if (!emit(0u, i, i + 1u)) {
                return false;
            }
        }
        return true;
    case PrimitiveClass::Lines:
        for (std::uint32_t i = 0; i + 1u < vertex_count; i += 2u) {
            if (!emit2(i, i + 1u)) {
                return false;
            }
        }
        return true;
    case PrimitiveClass::LineStrip:
        for (std::uint32_t i = 0; i + 1u < vertex_count; ++i) {
            if (!emit2(i, i + 1u)) {
                return false;
            }
        }
        return true;
    case PrimitiveClass::Points:
        for (std::uint32_t i = 0; i < vertex_count; ++i) {
            if (!emit_index(base_vertex + i)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

// The arrays LOAD_INDX reads (matrices, lights) are computed by the CPU: big-endian from the console's CPU, in host
// order from the native game (set_indexed_load_host_order), where data from files stays big-endian.
std::atomic<bool> g_indexed_load_host_order{false};

void read_indexed_values(const std::byte* src, std::uint32_t* dst, std::size_t count) {
    if (g_indexed_load_host_order.load(std::memory_order_relaxed)) {
        std::memcpy(dst, src, count * sizeof(std::uint32_t));
    } else {
        swap32_block(src, dst, count);
    }
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
        run_window(cursor, memory, sink, state, 0, nullptr);
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
        run_window(cursor, memory, sink, state, 0, nullptr);
        return cursor.offset;
    } catch (const FifoNeedMoreData& pending) {
        return pending.command_offset();
    } catch (const GxFatalError& err) {
        write_dump(fifo, err.fifo_offset());
        throw;
    }
}

FifoParser::CachedDisplayList* FifoParser::find_cached_display_list(
    std::uint32_t guest_addr,
    std::uint32_t byte_size,
    const GxState& state,
    std::span<const std::byte> bytes) {
    const std::uint32_t canonical_addr =
        normalize_display_list_guest_addr(guest_addr);
    const DisplayListCacheKey key{
        canonical_addr,
        byte_size,
    };
    const auto it = display_list_cache_map_.find(key);
    if (it == display_list_cache_map_.end()) {
        return nullptr;
    }

    auto& entries = it->second;
    entries.erase(
        std::remove_if(
            entries.begin(),
            entries.end(),
            [this, canonical_addr, byte_size](std::size_t index) {
                if (index >= display_list_cache_.size()) {
                    return true;
                }
                const CachedDisplayList& cached =
                    display_list_cache_[index];
                return !cached.valid ||
                    cached.guest_addr != canonical_addr ||
                    cached.byte_size != byte_size;
            }),
        entries.end());
    if (entries.empty()) {
        display_list_cache_map_.erase(it);
        return nullptr;
    }

    const auto inspect = [&](std::size_t index) -> CachedDisplayList* {
        if (index >= display_list_cache_.size()) {
            return nullptr;
        }
        CachedDisplayList& cached = display_list_cache_[index];
        if (!cached.valid ||
            cached.guest_addr != canonical_addr ||
            cached.byte_size != byte_size) {
            return nullptr;
        }
        if (cached.bytes.size() != bytes.size() ||
            !std::equal(bytes.begin(), bytes.end(), cached.bytes.begin())) {
            invalidate_display_list_cache_index(index);
            return nullptr;
        }
        if (!display_list_dependencies_match(cached, state)) {
            return nullptr;
        }
        cached.last_used = ++display_list_cache_tick_;
        return &cached;
    };
    // inspect() erases the map entry, and with it `entries`, when a list's bytes changed, so
    // several candidates are copied before iterating: up to eight on the stack, more on the heap.
    if (entries.size() == 1u) {
        return inspect(entries.front());
    }
    std::array<std::size_t, 8> fixed_candidates;
    const std::size_t candidate_count = entries.size();
    if (candidate_count <= fixed_candidates.size()) {
        std::copy(entries.begin(), entries.end(), fixed_candidates.begin());
        for (std::size_t i = 0; i < candidate_count; ++i) {
            if (auto* cached = inspect(fixed_candidates[i])) return cached;
        }
        return nullptr;
    }
    const std::vector<std::size_t> candidates = entries;
    for (const std::size_t index : candidates) {
        if (auto* cached = inspect(index)) {
            return cached;
        }
    }

    return nullptr;
}

void FifoParser::erase_display_list_cache_index_from_maps(
    std::size_t index) {
    if (index >= display_list_cache_.size()) {
        return;
    }

    const CachedDisplayList& cached = display_list_cache_[index];
    if (!cached.valid) {
        return;
    }

    const DisplayListCacheKey key{
        cached.guest_addr,
        cached.byte_size,
    };
    if (const auto it = display_list_cache_map_.find(key);
        it != display_list_cache_map_.end()) {
        auto& entries = it->second;
        entries.erase(
            std::remove(entries.begin(), entries.end(), index),
            entries.end());
        if (entries.empty()) {
            display_list_cache_map_.erase(it);
        }
    }
}

void FifoParser::invalidate_display_list_cache_index(std::size_t index) {
    if (index >= display_list_cache_.size() ||
        !display_list_cache_[index].valid) {
        return;
    }

    erase_display_list_cache_index_from_maps(index);
    display_list_cache_[index].valid = false;
    display_list_free_slots_.push_back(static_cast<std::uint32_t>(index));
    if (last_display_list_cache_index_ == index) {
        last_display_list_cache_index_ = static_cast<std::size_t>(-1);
    }
}

void FifoParser::index_display_list_cache_entry(std::size_t index) {
    if (index >= display_list_cache_.size()) {
        return;
    }
    CachedDisplayList& cached = display_list_cache_[index];
    if (!cached.valid) {
        return;
    }

    auto& key_entries = display_list_cache_map_[DisplayListCacheKey{
        cached.guest_addr,
        cached.byte_size,
    }];
    if (std::find(key_entries.begin(), key_entries.end(), index) ==
        key_entries.end()) {
        key_entries.push_back(index);
    }
}

std::uint64_t FifoParser::display_list_cache_entry_bytes(
    const CachedDisplayList& cached) noexcept {
    // capacity(), not size(): the budget bounds the retained heap blocks, which the cache keeps
    // for reuse instead of shrinking them.
    const auto vector_bytes = [](const auto& container) -> std::uint64_t {
        return static_cast<std::uint64_t>(container.capacity()) *
            sizeof(typename std::decay_t<decltype(container)>::value_type);
    };
    return vector_bytes(cached.bytes) +
        vector_bytes(cached.commands) +
        vector_bytes(cached.cp_dependencies) +
        vector_bytes(cached.xf_values) +
        vector_bytes(cached.prepared_draw_runs) +
        vector_bytes(cached.prepared_draw_payloads) +
        vector_bytes(cached.draw_packet_runs) +
        vector_bytes(cached.draw_run_packets) +
        vector_bytes(cached.draw_run_indices);
}

// Frees a vector's heap block. shrink_to_fit is only a request; the capacity-based budget
// needs the block gone.
template <typename T>
void release_capacity(std::vector<T>& values) {
    std::vector<T>{}.swap(values);
}

void FifoParser::evict_display_list_cache_to_budget(std::size_t skip_index) {
    if (cache_bytes_ <= kDisplayListCacheBudgetBytes) {
        return;
    }
    const auto release_entry = [this](std::size_t index) {
        CachedDisplayList& entry = display_list_cache_[index];
        const std::uint64_t released = display_list_cache_entry_bytes(entry);
        invalidate_display_list_cache_index(index);
        release_capacity(entry.bytes);
        release_capacity(entry.commands);
        release_capacity(entry.cp_dependencies);
        release_capacity(entry.xf_values);
        release_capacity(entry.prepared_draw_runs);
        release_capacity(entry.prepared_draw_payloads);
        release_capacity(entry.draw_packet_runs);
        release_capacity(entry.draw_run_packets);
        release_capacity(entry.draw_run_indices);
        cache_bytes_ = cache_bytes_ > released ? cache_bytes_ - released : 0u;
    };
    // Invalid slots still hold charged buffers; free those before evicting live entries.
    for (std::size_t index = 0;
         index < display_list_cache_.size() && cache_bytes_ > kDisplayListCacheBudgetBytes;
         ++index) {
        if (index != skip_index && !display_list_cache_[index].valid) {
            release_entry(index);
        }
    }
    // Evicts the least recently used entry per pass. An entry depends only on its guest bytes
    // and the CP registers recorded with it, so eviction costs a later re-parse, nothing more.
    while (cache_bytes_ > kDisplayListCacheBudgetBytes) {
        std::size_t victim = display_list_cache_.size();
        std::uint64_t victim_last_used = 0u;
        for (std::size_t index = 0; index < display_list_cache_.size();
             ++index) {
            if (index == skip_index) {
                continue;
            }
            const CachedDisplayList& cached = display_list_cache_[index];
            if (!cached.valid) {
                continue;
            }
            if (victim == display_list_cache_.size() ||
                cached.last_used < victim_last_used) {
                victim = index;
                victim_last_used = cached.last_used;
            }
        }
        if (victim == display_list_cache_.size()) {
            break;  // nothing evictable left; only the skipped entry remains
        }
        release_entry(victim);
    }
}

void FifoParser::store_cached_display_list(
    std::uint32_t guest_addr,
    std::uint32_t byte_size,
    DisplayListRecording&& recording) {
    if (!recording.cacheable || byte_size > kMaxCachedDisplayListBytes ||
        recording.bytes.size() != byte_size) {
        return;
    }

    // A scene's first frame stores hundreds of lists, so the slot is chosen without scanning
    // every slot (see display_list_free_slots_).
    CachedDisplayList* target = nullptr;
    while (target == nullptr && !display_list_free_slots_.empty()) {
        const std::size_t index = display_list_free_slots_.back();
        display_list_free_slots_.pop_back();
        if (index < display_list_cache_.size() && !display_list_cache_[index].valid) {
            target = &display_list_cache_[index];
        }
    }
    if (target == nullptr && display_list_next_unused_ < display_list_cache_.size()) {
        target = &display_list_cache_[display_list_next_unused_++];
    }
    if (target == nullptr && !display_list_cache_.empty()) {
        constexpr std::size_t kVictimSample = 32u;
        const std::size_t slots = display_list_cache_.size();
        for (std::size_t i = 0; i < kVictimSample && i < slots; ++i) {
            CachedDisplayList& cached =
                display_list_cache_[(display_list_victim_cursor_ + i) % slots];
            if (!cached.valid) {
                target = &cached;
                break;
            }
            if (target == nullptr || cached.last_used < target->last_used) {
                target = &cached;
            }
        }
        display_list_victim_cursor_ = (display_list_victim_cursor_ + kVictimSample) % slots;
    }
    if (target == nullptr) {
        return;
    }
    const std::uint32_t canonical_addr =
        normalize_display_list_guest_addr(guest_addr);

    if (target->valid) {
        erase_display_list_cache_index_from_maps(static_cast<std::size_t>(
            target - display_list_cache_.data()));
    }
    // An invalidated slot's vectors are still charged; uncharge them before charging the new data.
    const std::uint64_t released = display_list_cache_entry_bytes(*target);
    cache_bytes_ = cache_bytes_ > released ? cache_bytes_ - released : 0u;

    const std::size_t target_index = static_cast<std::size_t>(
        target - display_list_cache_.data());
    target->valid = true;
    target->cache_token = ++display_list_cache_token_;
    target->guest_addr = canonical_addr;
    target->byte_size = byte_size;
    target->last_used = ++display_list_cache_tick_;
    target->bytes = std::move(recording.bytes);
    target->commands = std::move(recording.commands);
    target->cp_dependencies = std::move(recording.cp_dependencies);
    target->xf_values = std::move(recording.xf_values);
    target->classify_summary = 0u;
    prepare_cached_display_list_draw_runs(*target);
    index_display_list_cache_entry(target_index);
    cache_bytes_ += display_list_cache_entry_bytes(*target);
    evict_display_list_cache_to_budget(target_index);
}

void FifoParser::prepare_cached_display_list_draw_runs(
    CachedDisplayList& cached) const {
    cached.prepared_draw_runs.clear();
    cached.prepared_draw_payloads.clear();
    cached.draw_packet_runs.clear();
    cached.draw_run_packets.clear();
    cached.draw_run_indices.clear();

    std::size_t command_index = 0;
    while (command_index < cached.commands.size()) {
        CachedDisplayListCommand& first = cached.commands[command_index];
        first.prepared_run_index = static_cast<std::size_t>(-1);
        first.prepared_run_command_count = 0;
        first.packet_run_index = static_cast<std::size_t>(-1);
        first.packet_run_command_count = 0;

        if (first.kind != CachedDisplayListCommand::Kind::Draw) {
            ++command_index;
            continue;
        }

        std::size_t run_end = command_index + 1u;
        while (run_end < cached.commands.size()) {
            const CachedDisplayListCommand& next = cached.commands[run_end];
            if (next.kind != CachedDisplayListCommand::Kind::Draw ||
                next.primitive != first.primitive ||
                next.vtxfmt != first.vtxfmt) {
                break;
            }
            ++run_end;
        }

        const std::span<const CachedDisplayListCommand> run_commands(
            cached.commands.data() + command_index,
            run_end - command_index);
        if (run_commands.size() <= 1u) {
            command_index = run_end;
            continue;
        }

        bool packet_run_valid = true;
        const std::size_t packet_offset = cached.draw_run_packets.size();
        std::uint32_t packet_run_total_vertices = 0;
        std::uint32_t packet_run_total_indices = 0;
        for (const CachedDisplayListCommand& command : run_commands) {
            if (command.draw_cursor_offset + 2u + command.draw_payload_size >
                cached.bytes.size()) {
                packet_run_valid = false;
                break;
            }
            if (packet_run_total_vertices >
                std::numeric_limits<std::uint32_t>::max() -
                    command.vertex_count) {
                packet_run_valid = false;
                break;
            }
            const std::uint32_t packet_index_count =
                cached_packet_run_index_count(
                    first.primitive,
                    command.vertex_count);
            if (packet_run_total_indices >
                std::numeric_limits<std::uint32_t>::max() -
                    packet_index_count) {
                packet_run_valid = false;
                break;
            }
            packet_run_total_vertices += command.vertex_count;
            packet_run_total_indices += packet_index_count;
            cached.draw_run_packets.push_back(CachedDrawPacket{
                command.opcode,
                command.vertex_count,
                command.local_opcode_offset,
                command.draw_cursor_offset,
                command.draw_payload_size,
            });
        }
        std::size_t packet_index_offset = 0;
        std::size_t packet_index_count = 0;
        if (packet_run_valid &&
            packet_run_total_indices != 0u &&
            (first.primitive != PrimitiveClass::Triangles ||
             !cached_draw_run_concatenation_preserves_primitive(
                 first.primitive, run_commands)) &&
            packet_run_total_vertices <=
                std::numeric_limits<std::uint16_t>::max()) {
            packet_index_offset = cached.draw_run_indices.size();
            cached.draw_run_indices.reserve(
                cached.draw_run_indices.size() +
                static_cast<std::size_t>(packet_run_total_indices));
            std::uint32_t packet_vertex_base = 0;
            for (const CachedDisplayListCommand& command : run_commands) {
                if (command.vertex_count != 0u &&
                    !append_cached_packet_indices(
                        first.primitive,
                        packet_vertex_base,
                        command.vertex_count,
                        cached.draw_run_indices)) {
                    packet_run_valid = false;
                    break;
                }
                packet_vertex_base += command.vertex_count;
            }
            if (packet_run_valid) {
                packet_index_count =
                    cached.draw_run_indices.size() - packet_index_offset;
                if (packet_index_count != packet_run_total_indices) {
                    packet_run_valid = false;
                }
            }
            if (!packet_run_valid) {
                cached.draw_run_indices.resize(packet_index_offset);
            }
        }

        if (packet_run_valid) {
            const std::size_t packet_run_index =
                cached.draw_packet_runs.size();
            cached.draw_packet_runs.push_back(CachedDrawPacketRun{
                first.primitive,
                first.vtxfmt,
                packet_offset,
                run_commands.size(),
                packet_run_total_vertices,
                packet_run_total_indices,
                packet_index_offset,
                packet_index_count,
            });
            first.packet_run_index = packet_run_index;
            first.packet_run_command_count = run_commands.size();
        } else {
            cached.draw_run_packets.resize(packet_offset);
            command_index = run_end;
            continue;
        }

        // Only the prepared run concatenates the packets into one draw, so only it needs the
        // primitive to survive concatenation (never true for strips, fans and line strips). The
        // packet run above keeps one draw per packet, with indices from
        // append_cached_packet_indices that match VertexLoader's emitters, odd strip triangles'
        // winding swap included.
        if (!cached_draw_run_concatenation_preserves_primitive(
                first.primitive,
                run_commands)) {
            command_index = run_end;
            continue;
        }

        std::uint32_t total_vertices = 0;
        std::size_t total_payload_bytes = 0;
        bool valid = true;
        for (const CachedDisplayListCommand& command : run_commands) {
            if (command.vertex_count == 0u) {
                continue;
            }
            if (total_vertices >
                static_cast<std::uint32_t>(
                    std::numeric_limits<std::uint16_t>::max()) -
                    command.vertex_count) {
                valid = false;
                break;
            }
            if (command.draw_cursor_offset + 2u + command.draw_payload_size >
                cached.bytes.size()) {
                valid = false;
                break;
            }
            total_vertices += command.vertex_count;
            total_payload_bytes += command.draw_payload_size;
        }
        if (!valid || total_vertices == 0u) {
            command_index = run_end;
            continue;
        }

        const std::size_t payload_offset =
            cached.prepared_draw_payloads.size();
        cached.prepared_draw_payloads.resize(
            payload_offset + 2u + total_payload_bytes);
        std::byte* const out =
            cached.prepared_draw_payloads.data() + payload_offset;
        out[0] = static_cast<std::byte>((total_vertices >> 8) & 0xFFu);
        out[1] = static_cast<std::byte>(total_vertices & 0xFFu);
        std::size_t dst = 2u;
        for (const CachedDisplayListCommand& command : run_commands) {
            if (command.vertex_count == 0u) {
                continue;
            }
            const std::size_t src = command.draw_cursor_offset + 2u;
            std::memcpy(
                out + dst,
                cached.bytes.data() + src,
                command.draw_payload_size);
            dst += command.draw_payload_size;
        }

        const std::size_t prepared_index = cached.prepared_draw_runs.size();
        cached.prepared_draw_runs.push_back(CachedPreparedDrawRun{
            first.primitive,
            first.vtxfmt,
            first.opcode,
            static_cast<std::uint16_t>(total_vertices),
            first.local_opcode_offset,
            run_commands.size(),
            payload_offset,
            2u + total_payload_bytes,
        });
        first.prepared_run_index = prepared_index;
        first.prepared_run_command_count = run_commands.size();
        command_index = run_end;
    }
}

// Classify-only replay of a cached list with no PE register write: its CP
// writes in order, then its last BP command, which leaves the one-shot mask as
// the full replay does (armed by a final mask write, otherwise spent). Lists
// that write PE_DONE/PE_TOKEN/PE_TOKEN_INT take the full replay.
bool FifoParser::replay_event_free_display_list(
    const CachedDisplayList& cached,
    GxState& state) const {
    if (cached.classify_summary == 0u) {
        cached.classify_cp_writes.clear();
        cached.classify_has_bp = false;
        cached.classify_summary = 1u;
        for (const CachedDisplayListCommand& command : cached.commands) {
            if (command.kind == CachedDisplayListCommand::Kind::LoadCp) {
                cached.classify_cp_writes.emplace_back(command.reg, command.value);
            } else if (command.kind == CachedDisplayListCommand::Kind::LoadBp) {
                const auto reg = static_cast<std::uint8_t>(command.command >> 24);
                if (reg == bp::kPeDone || reg == bp::kPeToken ||
                    reg == bp::kPeTokenInt) {
                    cached.classify_summary = 2u;
                    cached.classify_cp_writes.clear();
                    break;
                }
                cached.classify_has_bp = true;
                cached.classify_last_bp = command.command;
            }
        }
    }
    if (cached.classify_summary != 1u) {
        return false;
    }
    for (const auto& [reg, value] : cached.classify_cp_writes) {
        state.load_cp(reg, value);
    }
    if (cached.classify_has_bp) {
        state.load_bp(cached.classify_last_bp);
    }
    return true;
}

void FifoParser::replay_cached_display_list(
    const CachedDisplayList& cached,
    GuestMemory* memory,
    FifoSink& sink,
    GxState& state,
    std::size_t opcode_offset) const {
    if (event_classification_only_ &&
        replay_event_free_display_list(cached, state)) {
        return;
    }
    FifoCursor replay_context;
    replay_context.data = cached.bytes;
    replay_context.offset = 0;
    replay_context.base_offset = opcode_offset;
    replay_context.command_offset = opcode_offset;
    replay_context.recover_truncation = false;
    FifoParserProfile* const replay_profile = profile_;
    const auto replay_start = replay_profile != nullptr
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};

    struct CachedDrawRunScope {
        FifoSink* sink = nullptr;
        bool active = false;

        ~CachedDrawRunScope() {
            if (active && sink != nullptr) {
                sink->end_cached_draw_run();
            }
        }
    };

    // Replay runs thousands of times per frame; reusing this buffer keeps the loop free of
    // allocations. The sink copies the packets before on_cached_simple_draw_run returns, and
    // replay is not reentrant.
    static thread_local std::vector<CachedDrawPacket> simple_draw_packets;
    std::size_t command_index = 0;
    // End of the current run of draws sharing one primitive and vertex format, as
    // scan_draw_run_end computes it; kept across iterations so each run is scanned once.
    std::size_t run_end_cache = 0;
    while (command_index < cached.commands.size()) {
        const CachedDisplayListCommand& first = cached.commands[command_index];
        const auto profile_kind =
            first.kind == CachedDisplayListCommand::Kind::Draw
                ? CachedReplayProfileKind::Draw
            : first.kind == CachedDisplayListCommand::Kind::LoadIndx
                ? CachedReplayProfileKind::Indx
            : (first.kind == CachedDisplayListCommand::Kind::LoadCp ||
               first.kind == CachedDisplayListCommand::Kind::LoadXf ||
               first.kind == CachedDisplayListCommand::Kind::LoadBp)
                ? CachedReplayProfileKind::State : CachedReplayProfileKind::Misc;
        // Times everything this iteration consumes: a fast-path run, or the commands up to
        // run_end with the sink's run teardown.
        const CachedReplayCommandScope replay_command_scope{
            replay_profile, profile_kind, command_index, command_index,
            replay_profile != nullptr ? std::chrono::steady_clock::now()
                                      : std::chrono::steady_clock::time_point{}};
        std::size_t run_end = command_index + 1u;
        bool run_end_scanned = false;
        auto scan_draw_run_end = [&]() -> std::size_t {
            if (run_end_scanned ||
                first.kind != CachedDisplayListCommand::Kind::Draw) {
                return run_end;
            }
            if (run_end_cache > command_index) {
                run_end = run_end_cache;
                run_end_scanned = true;
                return run_end;
            }
            while (run_end < cached.commands.size()) {
                const CachedDisplayListCommand& next =
                    cached.commands[run_end];
                if (next.kind != CachedDisplayListCommand::Kind::Draw ||
                    next.primitive != first.primitive ||
                    next.vtxfmt != first.vtxfmt) {
                    break;
                }
                ++run_end;
            }
            run_end_cache = run_end;
            run_end_scanned = true;
            return run_end;
        };

        if (first.kind == CachedDisplayListCommand::Kind::Draw &&
            first.prepared_run_index != static_cast<std::size_t>(-1)) {
            if (first.prepared_run_index >= cached.prepared_draw_runs.size()) {
                throw GxFatalError(
                    "GX FIFO: cached prepared draw run index out of range",
                    opcode_offset + first.local_opcode_offset,
                    first.opcode);
            }
            const CachedPreparedDrawRun& prepared =
                cached.prepared_draw_runs[first.prepared_run_index];
            if (prepared.source_draw_count <= 1u ||
                prepared.source_draw_count !=
                    first.prepared_run_command_count ||
                command_index + prepared.source_draw_count >
                    cached.commands.size() ||
                prepared.payload_offset + prepared.payload_size >
                    cached.prepared_draw_payloads.size()) {
                throw GxFatalError(
                    "GX FIFO: cached prepared draw run payload out of range",
                    opcode_offset + first.local_opcode_offset,
                    first.opcode);
            }
            const std::span<const std::byte> prepared_payload(
                cached.prepared_draw_payloads.data() +
                    prepared.payload_offset,
                prepared.payload_size);
            if (sink.on_cached_prepared_draw_run(
                    prepared.primitive,
                    prepared.vtxfmt,
                    prepared_payload,
                    opcode_offset,
                    prepared.local_opcode_offset,
                    prepared.opcode,
                    prepared.source_draw_count)) {
                if (replay_profile != nullptr) {
                    ++replay_profile->call_dl_replay_prepared_run_count;
                    replay_profile
                        ->call_dl_replay_prepared_source_draw_count +=
                        prepared.source_draw_count;
                    replay_profile
                        ->call_dl_replay_prepared_payload_bytes +=
                        prepared.payload_size;
                }
                command_index += prepared.source_draw_count;
                continue;
            }
        }

        if (first.kind == CachedDisplayListCommand::Kind::Draw &&
            first.packet_run_index != static_cast<std::size_t>(-1)) {
            if (first.packet_run_index >= cached.draw_packet_runs.size()) {
                throw GxFatalError(
                    "GX FIFO: cached packet draw run index out of range",
                    opcode_offset + first.local_opcode_offset,
                    first.opcode);
            }
            const CachedDrawPacketRun& packet_run =
                cached.draw_packet_runs[first.packet_run_index];
            if (packet_run.packet_count <= 1u ||
                packet_run.packet_count != first.packet_run_command_count ||
                command_index + packet_run.packet_count >
                    cached.commands.size() ||
                packet_run.packet_offset + packet_run.packet_count >
                    cached.draw_run_packets.size()) {
                throw GxFatalError(
                    "GX FIFO: cached packet draw run payload out of range",
                    opcode_offset + first.local_opcode_offset,
                    first.opcode);
            }
            const std::span<const CachedDrawPacket> packets(
                cached.draw_run_packets.data() + packet_run.packet_offset,
                packet_run.packet_count);
            std::span<const std::uint16_t> precomputed_indices;
            if (packet_run.index_count != 0u) {
                if (packet_run.index_offset + packet_run.index_count >
                    cached.draw_run_indices.size()) {
                    throw GxFatalError(
                        "GX FIFO: cached packet draw run index payload out of range",
                        opcode_offset + first.local_opcode_offset,
                        first.opcode);
                }
                precomputed_indices = std::span<const std::uint16_t>(
                    cached.draw_run_indices.data() + packet_run.index_offset,
                    packet_run.index_count);
            }
            if (sink.on_cached_packet_draw_run(
                    cached.cache_token,
                    first.packet_run_index,
                    packet_run.primitive,
                    packet_run.vtxfmt,
                    cached.bytes,
                    opcode_offset,
                    packets,
                    packet_run.total_vertices,
                    packet_run.total_indices,
                    precomputed_indices)) {
                if (replay_profile != nullptr) {
                    ++replay_profile->call_dl_replay_packet_run_count;
                    replay_profile
                        ->call_dl_replay_packet_source_draw_count +=
                        packet_run.packet_count;
                }
                command_index += packet_run.packet_count;
                continue;
            }
        }

        if (first.kind == CachedDisplayListCommand::Kind::Draw &&
            scan_draw_run_end() - command_index > 1u &&
            cached_draw_run_concatenation_preserves_primitive(
                first.primitive,
                std::span<const CachedDisplayListCommand>(
                    cached.commands.data() + command_index,
                    run_end - command_index))) {
            const std::size_t run_count = run_end - command_index;
            simple_draw_packets.clear();
            if (simple_draw_packets.capacity() < run_count) {
                simple_draw_packets.reserve(run_count);
            }
            for (std::size_t i = command_index; i < run_end; ++i) {
                const CachedDisplayListCommand& command = cached.commands[i];
                simple_draw_packets.push_back(CachedDrawPacket{
                    command.opcode,
                    command.vertex_count,
                    command.local_opcode_offset,
                    command.draw_cursor_offset,
                    command.draw_payload_size,
                });
            }
            if (sink.on_cached_simple_draw_run(
                    first.primitive,
                    first.vtxfmt,
                    cached.bytes,
                    opcode_offset,
                    simple_draw_packets)) {
                command_index = run_end;
                continue;
            }
        }

        CachedDrawRunScope draw_run_scope;
        scan_draw_run_end();
        if (run_end - command_index > 1u) {
            draw_run_scope.sink = &sink;
            draw_run_scope.active = sink.begin_cached_draw_run(
                first.primitive,
                first.vtxfmt,
                run_end - command_index);
        }

        while (command_index < run_end) {
            const CachedDisplayListCommand& command =
                cached.commands[command_index];

            switch (command.kind) {
            case CachedDisplayListCommand::Kind::Nop:
            case CachedDisplayListCommand::Kind::Metrics:
            case CachedDisplayListCommand::Kind::HardwareNoop:
                break;

            case CachedDisplayListCommand::Kind::LoadCp:
                state.load_cp(command.reg, command.value);
                break;

            case CachedDisplayListCommand::Kind::LoadXf:
                if (command.values_offset + command.count >
                    cached.xf_values.size()) {
                    throw GxFatalError(
                        "GX FIFO: cached display-list XF payload out of range",
                        opcode_offset + command.local_opcode_offset,
                        command.opcode);
                }
                state.load_xf(
                    command.base,
                    cached.xf_values.data() + command.values_offset,
                    command.count);
                break;

            case CachedDisplayListCommand::Kind::LoadIndx: {
                const std::uint32_t array_base =
                    state.cp(static_cast<std::uint8_t>(
                        0xACu + command.array_slot));
                const std::uint32_t array_stride =
                    state.cp(static_cast<std::uint8_t>(
                        0xBCu + command.array_slot));
                const std::uint32_t guest_addr =
                    array_base +
                    static_cast<std::uint32_t>(command.idx) * array_stride;
                const std::uint32_t byte_size =
                    static_cast<std::uint32_t>(command.length) * 4u;
                replay_context.offset = command.local_opcode_offset;
                const auto guest_window = resolve_guest(
                    memory, guest_addr, byte_size, replay_context);

                std::array<std::uint32_t, 16> values{};
                read_indexed_values(
                    guest_window.data(), values.data(), command.length);
                state.load_xf_indexed(
                    command.xf_addr, values.data(), command.length,
                    (static_cast<std::uint32_t>(command.array_slot) << 16u) |
                        static_cast<std::uint32_t>(command.idx));
                break;
            }

            case CachedDisplayListCommand::Kind::InvalidateVertexCache:
                sink.on_invalidate_vertex_cache();
                break;

            case CachedDisplayListCommand::Kind::LoadBp: {
                state.load_bp(command.command);
                const auto bp_reg =
                    static_cast<std::uint8_t>(command.command >> 24);
                const std::uint32_t bp_val = state.bp(bp_reg);
                switch (bp_reg) {
                case bp::kCopyExecute:
                    sink.on_efb_copy(bp_val);
                    break;
                case bp::kPeDone:
                    if ((bp_val & 0xFFu) == 2u) {
                        sink.on_pe_finish();
                    }
                    break;
                case bp::kPeToken:
                    sink.on_pe_token(
                        static_cast<std::uint16_t>(bp_val & 0xFFFFu),
                        false);
                    break;
                case bp::kPeTokenInt:
                    sink.on_pe_token(
                        static_cast<std::uint16_t>(bp_val & 0xFFFFu),
                        true);
                    break;
                case 0x66u:
                    if (bp_val == 0u) {
                        sink.on_invalidate_textures();
                    }
                    break;
                case bp::kTlutDest:
                    sink.on_tlut_load();
                    break;
                default:
                    break;
                }
                break;
            }

            case CachedDisplayListCommand::Kind::Draw: {
                FifoCursor draw_cursor;
                draw_cursor.data = cached.bytes;
                draw_cursor.offset = command.draw_cursor_offset;
                draw_cursor.base_offset = opcode_offset;
                draw_cursor.command_offset =
                    opcode_offset + command.local_opcode_offset;
                draw_cursor.recover_truncation = false;
                sink.on_draw(command.primitive, command.vtxfmt, draw_cursor);
                const std::size_t expected_offset =
                    command.draw_cursor_offset + 2u +
                    command.draw_payload_size;
                if (draw_cursor.offset != expected_offset) {
                    throw GxFatalError(
                        "GX FIFO: cached display-list draw consumed wrong size",
                        opcode_offset + command.local_opcode_offset,
                        command.opcode);
                }
                break;
            }
            }

            ++command_index;
        }
    }
    if (replay_profile != nullptr) {
        replay_profile->call_dl_replay_command_us += elapsed_fifo_us(
            replay_start,
            std::chrono::steady_clock::now());
    }
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
    case op::kLoadIndxA:
    case op::kLoadIndxB:
    case op::kLoadIndxC:
    case op::kLoadIndxD:
    case op::kLoadBpReg:
        return available >= 4u;
    case op::kCallDisplayList:
        return available >= 8u;
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
    GuestMemory* memory,
    FifoSink& sink,
    GxState& state,
    int depth,
    DisplayListRecording* recording) {
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
            if (recording != nullptr && recording->cacheable) {
                recording->cp_written[canonical_cp_register(reg)] = true;
                CachedDisplayListCommand command;
                command.kind = CachedDisplayListCommand::Kind::LoadCp;
                command.opcode = opcode;
                command.reg = reg;
                command.value = val;
                command.local_opcode_offset = local_opcode_offset;
                recording->commands.push_back(command);
            }
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
            if (!event_classification_only_) {
                state.load_xf(base, values.data(), count);
            }
            if (recording != nullptr && recording->cacheable) {
                CachedDisplayListCommand command;
                command.kind = CachedDisplayListCommand::Kind::LoadXf;
                command.opcode = opcode;
                command.count = count;
                command.base = base;
                command.values_offset = recording->xf_values.size();
                recording->xf_values.insert(
                    recording->xf_values.end(),
                    values.begin(),
                    values.begin() + count);
                command.local_opcode_offset = local_opcode_offset;
                recording->commands.push_back(command);
            }
            finish_profile(
                &FifoParserProfile::xf_count,
                &FifoParserProfile::xf_us);
            break;
        }

        case op::kLoadIndxA:
        case op::kLoadIndxB:
        case op::kLoadIndxC:
        case op::kLoadIndxD: {
            // A-D read the GXSetArray position-matrix, normal-matrix, texture-matrix and light
            // arrays: bases in CP 0xAC-0xAF, strides in bytes per element (not the transfer
            // size) in CP 0xBC-0xBF.
            const std::uint8_t array_slot =
                static_cast<std::uint8_t>((opcode - op::kLoadIndxA) / 8u);

            const std::uint16_t idx      = cursor.read_u16();
            const std::uint16_t len_addr = cursor.read_u16();
            const std::uint16_t length   =
                static_cast<std::uint16_t>((len_addr >> 12) + 1u);
            const std::uint16_t xf_addr  =
                static_cast<std::uint16_t>(len_addr & 0x0FFFu);

            // The stride register, not the length, spaces the elements: a normal-matrix load
            // reads 9 words from an array of 12-word matrices.
            const std::uint32_t array_base =
                state.cp(static_cast<std::uint8_t>(0xACu + array_slot));
            const std::uint32_t array_stride =
                state.cp(static_cast<std::uint8_t>(0xBCu + array_slot));
            const std::uint32_t guest_addr =
                array_base + static_cast<std::uint32_t>(idx) * array_stride;

            const std::uint32_t byte_size =
                static_cast<std::uint32_t>(length) * 4u;
            if (!event_classification_only_) {
                const auto guest_window =
                    resolve_guest(memory, guest_addr, byte_size, cursor);

                std::array<std::uint32_t, 16> values{};
                read_indexed_values(guest_window.data(), values.data(), length);
                state.load_xf_indexed(xf_addr, values.data(), length,
                                      (static_cast<std::uint32_t>(array_slot) << 16u) |
                                          static_cast<std::uint32_t>(idx));
            }
            if (recording != nullptr && recording->cacheable) {
                CachedDisplayListCommand command;
                command.kind = CachedDisplayListCommand::Kind::LoadIndx;
                command.opcode = opcode;
                command.array_slot = array_slot;
                command.idx = idx;
                command.length = length;
                command.xf_addr = xf_addr;
                command.local_opcode_offset = local_opcode_offset;
                recording->commands.push_back(command);
            }
            finish_profile(
                &FifoParserProfile::indx_count,
                &FifoParserProfile::indx_us);
            break;
        }

        case op::kCallDisplayList: {
            if (depth >= kMaxDisplayListDepth) {
                throw GxFatalError(
                    "GX FIFO: display list recursion limit exceeded",
                    opcode_offset,
                    opcode);
            }
            const std::uint32_t guest_addr =
                cursor.read_u32() & ~std::uint32_t{31u};
            const std::uint32_t byte_size =
                cursor.read_u32() & ~std::uint32_t{31u};
            if (byte_size == 0u) {
                finish_profile(
                    &FifoParserProfile::call_dl_count,
                    &FifoParserProfile::call_dl_us);
                break;
            }

            const std::uint32_t canonical_guest_addr =
                normalize_display_list_guest_addr(guest_addr);
            const auto dl_window =
                resolve_guest(memory, guest_addr, byte_size, cursor);
            if (memory_read_recorder_ != nullptr) {
                memory_read_recorder_->record_guest_dependency_shape_read(
                    guest_addr,
                    byte_size);
            }

            if (depth == 0) {
                if (last_display_list_cache_index_ <
                    display_list_cache_.size()) {
                    CachedDisplayList& cached =
                        display_list_cache_[last_display_list_cache_index_];
                    if (cached.valid &&
                        cached.guest_addr == canonical_guest_addr &&
                        cached.byte_size == byte_size) {
                        const bool bytes_match =
                            cached.bytes.size() == dl_window.size() &&
                            std::equal(
                                dl_window.begin(),
                                dl_window.end(),
                                cached.bytes.begin());
                        if (bytes_match && display_list_dependencies_match(cached, state)) {
                            if (profile != nullptr) {
                                ++profile->call_dl_cache_hits;
                            }
                            cached.last_used = ++display_list_cache_tick_;
                            replay_cached_display_list(
                                cached, memory, sink, state, opcode_offset);
                            if (profile != nullptr) {
                                profile->call_dl_bytes += byte_size;
                            }
                            const std::uint64_t call_dl_us = finish_profile(
                                &FifoParserProfile::call_dl_count,
                                &FifoParserProfile::call_dl_us);
                            if (profile != nullptr) {
                                record_call_display_list_profile(
                                    *profile,
                                    guest_addr,
                                    byte_size,
                                    call_dl_us);
                            }
                            break;
                        }
                        if (!bytes_match) {
                            invalidate_display_list_cache_index(
                                last_display_list_cache_index_);
                        }
                    }
                }
                if (auto* cached = find_cached_display_list(
                        guest_addr, byte_size, state, dl_window);
                    cached != nullptr) {
                    if (profile != nullptr) {
                        ++profile->call_dl_cache_hits;
                    }
                    last_display_list_cache_index_ = static_cast<std::size_t>(
                        cached - display_list_cache_.data());
                    replay_cached_display_list(
                        *cached, memory, sink, state, opcode_offset);
                    if (profile != nullptr) {
                        profile->call_dl_bytes += byte_size;
                    }
                    const std::uint64_t call_dl_us = finish_profile(
                        &FifoParserProfile::call_dl_count,
                        &FifoParserProfile::call_dl_us);
                    if (profile != nullptr) {
                        record_call_display_list_profile(
                            *profile,
                            guest_addr,
                            byte_size,
                            call_dl_us);
                    }
                    break;
                }
                if (profile != nullptr) {
                    ++profile->call_dl_cache_misses;
                }
            } else if (recording != nullptr) {
                recording->cacheable = false;
            }

            FifoCursor dl_cursor;
            dl_cursor.data        = dl_window;
            dl_cursor.offset      = 0;
            dl_cursor.base_offset = opcode_offset;  // parent context for errors
            dl_cursor.command_offset = opcode_offset;
            dl_cursor.recover_truncation = false;

            DisplayListRecording dl_recording;
            if (depth == 0) {
                // Oversized roots still execute fully, but cannot enter the cache.
                dl_recording.cacheable = byte_size <= kMaxCachedDisplayListBytes;
                if (dl_recording.cacheable) {
                    dl_recording.bytes.assign(
                        dl_window.begin(), dl_window.end());
                }
            }
            DisplayListRecording* const child_recording = depth == 0
                ? (dl_recording.cacheable ? &dl_recording : nullptr)
                : recording;
            const auto miss_parse_start = profile != nullptr && depth == 0
                ? std::chrono::steady_clock::now()
                : std::chrono::steady_clock::time_point{};
            run_window(
                dl_cursor, memory, sink, state, depth + 1, child_recording);
            if (profile != nullptr && depth == 0) {
                profile->call_dl_miss_parse_us += static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::steady_clock::now() - miss_parse_start).count());
            }
            if (depth == 0) {
                if (dl_recording.cacheable) {
                    if (profile != nullptr) {
                        ++profile->call_dl_cache_stores;
                    }
                    const auto store_start = profile != nullptr
                        ? std::chrono::steady_clock::now()
                        : std::chrono::steady_clock::time_point{};
                    store_cached_display_list(
                        guest_addr,
                        byte_size,
                        std::move(dl_recording));
                    if (profile != nullptr) {
                        profile->call_dl_store_us += static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - store_start).count());
                    }
                } else if (profile != nullptr) {
                    ++profile->call_dl_cache_uncacheable;
                }
            }
            if (profile != nullptr) {
                profile->call_dl_bytes += byte_size;
            }
            const std::uint64_t call_dl_us = finish_profile(
                &FifoParserProfile::call_dl_count,
                &FifoParserProfile::call_dl_us);
            if (profile != nullptr) {
                record_call_display_list_profile(
                    *profile,
                    guest_addr,
                    byte_size,
                    call_dl_us);
            }
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
            if (recording != nullptr && recording->cacheable) {
                CachedDisplayListCommand command;
                command.kind =
                    CachedDisplayListCommand::Kind::InvalidateVertexCache;
                command.opcode = opcode;
                command.local_opcode_offset = local_opcode_offset;
                recording->commands.push_back(command);
            }
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
            if (recording != nullptr && recording->cacheable) {
                CachedDisplayListCommand cached_command;
                cached_command.kind = CachedDisplayListCommand::Kind::LoadBp;
                cached_command.opcode = opcode;
                cached_command.command = command;
                cached_command.local_opcode_offset = local_opcode_offset;
                recording->commands.push_back(cached_command);
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
                record_display_list_draw_dependencies(
                    recording, state, vtxfmt);
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
                const std::size_t draw_cursor_offset = cursor.offset;
                sink.on_draw(prim, vtxfmt, cursor);
                if (recording != nullptr && recording->cacheable) {
                    CachedDisplayListCommand command;
                    command.kind = CachedDisplayListCommand::Kind::Draw;
                    command.opcode = opcode;
                    command.primitive = prim;
                    command.vtxfmt = vtxfmt;
                    command.vertex_count = vertex_count;
                    command.draw_cursor_offset = draw_cursor_offset;
                    command.draw_payload_size = payload_size;
                    command.local_opcode_offset = local_opcode_offset;
                    recording->commands.push_back(command);
                }
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
