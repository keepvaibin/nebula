#include "galaxy/gx/gx_backend.h"
#include "galaxy/gx/fifo_cache_fingerprint.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kArrayBase = 0x00001000u;
constexpr std::uint32_t kArrayStride = 36u;
constexpr std::uint32_t kMaxIndex8 = 255u;
constexpr std::uint32_t kNbtElementSize = 36u;
constexpr std::uint32_t kLastElementOffset = kMaxIndex8 * kArrayStride;
constexpr std::uint32_t kRequiredArrayBytes =
    kLastElementOffset + kNbtElementSize;

void append_be_u16(std::vector<std::byte>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::byte>((value >> 8u) & 0xFFu));
    bytes.push_back(static_cast<std::byte>(value & 0xFFu));
}

void append_be_u32(std::vector<std::byte>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::byte>((value >> 24u) & 0xFFu));
    bytes.push_back(static_cast<std::byte>((value >> 16u) & 0xFFu));
    bytes.push_back(static_cast<std::byte>((value >> 8u) & 0xFFu));
    bytes.push_back(static_cast<std::byte>(value & 0xFFu));
}

void append_cp_write(
    std::vector<std::byte>& bytes,
    std::uint8_t reg,
    std::uint32_t value) {
    bytes.push_back(static_cast<std::byte>(galaxy::gx::op::kLoadCpReg));
    bytes.push_back(static_cast<std::byte>(reg));
    append_be_u32(bytes, value);
}

void append_call_dl(
    std::vector<std::byte>& bytes,
    std::uint32_t address,
    std::uint32_t size) {
    bytes.push_back(static_cast<std::byte>(galaxy::gx::op::kCallDisplayList));
    append_be_u32(bytes, address);
    append_be_u32(bytes, size);
}

void store_be_f32(std::span<std::byte> bytes, std::size_t offset, float value) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    bytes[offset + 0u] = static_cast<std::byte>((bits >> 24u) & 0xFFu);
    bytes[offset + 1u] = static_cast<std::byte>((bits >> 16u) & 0xFFu);
    bytes[offset + 2u] = static_cast<std::byte>((bits >> 8u) & 0xFFu);
    bytes[offset + 3u] = static_cast<std::byte>(bits & 0xFFu);
}

struct NbtFixture {
    std::vector<std::byte> fifo;
    std::size_t draw_opcode_offset = 0;
};

NbtFixture make_shared_index_nbt_fixture() {
    NbtFixture fixture;
    const std::uint32_t vcd_lo =
        static_cast<std::uint32_t>(galaxy::gx::VcdType::Index8) << 11u;
    const std::uint32_t vat_a =
        (1u << 9u) |
        (static_cast<std::uint32_t>(galaxy::gx::ComponentFormat::F32)
         << 10u);
    append_cp_write(fixture.fifo, galaxy::gx::cp::kVcdLo, vcd_lo);
    append_cp_write(fixture.fifo, galaxy::gx::cp::kVatABase, vat_a);
    append_cp_write(
        fixture.fifo,
        static_cast<std::uint8_t>(galaxy::gx::cp::kArrayBaseBase + 1u),
        kArrayBase);
    append_cp_write(
        fixture.fifo,
        static_cast<std::uint8_t>(galaxy::gx::cp::kArrayStrideBase + 1u),
        kArrayStride);

    fixture.draw_opcode_offset = fixture.fifo.size();
    const auto draw_opcode = static_cast<std::uint8_t>(
        static_cast<std::uint8_t>(galaxy::gx::PrimitiveClass::Points) << 3u);
    fixture.fifo.push_back(static_cast<std::byte>(draw_opcode));
    append_be_u16(fixture.fifo, 1u);
    fixture.fifo.push_back(static_cast<std::byte>(kMaxIndex8));
    return fixture;
}

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
    }
    return condition;
}

bool expect_vec3(
    const float (&actual)[3],
    const std::array<float, 3>& expected,
    const char* message) {
    return expect(
        actual[0] == expected[0] &&
            actual[1] == expected[1] &&
            actual[2] == expected[2],
        message);
}

bool reject_device_read(
    void*,
    std::uint32_t,
    std::uint32_t,
    std::byte*) {
    return false;
}

bool reject_device_write(
    void*,
    std::uint32_t,
    std::uint32_t,
    const std::byte*) {
    return false;
}

void observe_guest_write(void*, std::uint32_t, std::uint32_t) {}

void throw_guest_memory_fault(void*, std::uint32_t, const char* message) {
    throw std::runtime_error(message);
}

}  // namespace

namespace galaxy::gx {

struct MemorySnapshotNbtProbeResult {
    std::vector<GuestMemoryRange> ranges;
    DecodedPacketRunVertices decoded;
    std::uint64_t snapshot_bytes = 0;
    bool dependency_cache_hit = false;
    bool sealed = false;
    bool external_aliases_detached = false;
};

class GxBackendMemorySnapshotTestAccess {
public:
    static bool direct_pe_cached_draw_runs_preserve_events() {
        constexpr std::uint32_t address = 0x00002000u;
        for (const auto primitive : {PrimitiveClass::Points,
                 PrimitiveClass::Triangles, PrimitiveClass::Quads,
                 PrimitiveClass::TriangleStrip}) {
            std::vector<std::byte> list;
            const auto draw = [&](std::uint16_t count) {
                list.push_back(static_cast<std::byte>(
                    static_cast<std::uint8_t>(primitive) << 3u));
                append_be_u16(list, count);
                for (std::uint32_t i = 0; i < count * 2u; ++i) {
                    list.push_back(static_cast<std::byte>((i * 13u) & 0xFFu));
                }
            };
            const auto bp_write = [&](std::uint8_t reg, std::uint32_t value) {
                list.push_back(std::byte{op::kLoadBpReg});
                append_be_u32(list, (static_cast<std::uint32_t>(reg) << 24u) | value);
            };
            const std::uint16_t count = primitive == PrimitiveClass::Quads ? 4u : 3u;
            draw(count);
            draw(static_cast<std::uint16_t>(count * 2u));
            bp_write(bp::kPeToken, 0x1234u);
            draw(0u);
            draw(count);
            bp_write(bp::kPeDone, 2u);
            // Preserve BP's one-shot mask and both interrupt modes between runs.
            bp_write(0xFEu, 0x00FFu);
            bp_write(bp::kPeToken, 0xABCDu);  // existing 0x1234 -> 0x12CD
            bp_write(bp::kPeTokenInt, 0x5678u);

            GuestMemoryRegionV1 region{address,
                static_cast<std::uint32_t>(list.size()), list.data()};
            GuestMemoryV1 memory{};
            memory.region_count = 1u;
            memory.regions = &region;
            std::vector<std::uint32_t> events;
            NativeServicesV1 services{};
            services.user = &events;
            services.gx_pe_finish = [](void* user) {
                static_cast<std::vector<std::uint32_t>*>(user)->push_back(0x20000u);
            };
            services.gx_pe_token = [](void* user, std::uint16_t token, bool interrupt) {
                static_cast<std::vector<std::uint32_t>*>(user)->push_back(
                    token | (interrupt ? 0x10000u : 0u));
            };
            const std::vector<std::uint32_t> expected{
                0x1234u, 0x20000u, 0x12CDu, 0x15678u};
            FifoParserProfile profile{};
            auto backend = std::make_unique<GxBackend>();
            backend->event_parser_.set_profile(&profile);
            std::vector<std::byte> setup;
            append_cp_write(setup, cp::kVcdLo,
                static_cast<std::uint32_t>(VcdType::Direct) << 9u);
            // Direct U8 XY position: exactly two payload bytes per vertex.
            append_cp_write(setup, cp::kVatABase, 0u);
            backend->scan_pe_events_on_sim_thread(
                setup.data(), setup.size(), &memory, &services);
            std::vector<std::byte> call;
            append_call_dl(call, address, static_cast<std::uint32_t>(list.size()));
            for (unsigned round = 0; round < 3u; ++round) {
                events.clear();
                if (round == 2u) {
                    backend->scan_pe_events_on_sim_thread(
                        call.data(), 4u, &memory, &services);
                    if (!events.empty()) return false;
                    backend->scan_pe_events_on_sim_thread(
                        call.data() + 4u, call.size() - 4u, &memory, &services);
                } else {
                    backend->scan_pe_events_on_sim_thread(
                        call.data(), call.size(), &memory, &services);
                }
                if (events != expected || !backend->event_pending_fifo_.empty()) {
                    return false;
                }
            }
            const bool packet_only = primitive == PrimitiveClass::TriangleStrip;
            if (profile.call_dl_cache_hits != 2u ||
                profile.call_dl_replay_prepared_run_count != (packet_only ? 0u : 4u) ||
                profile.call_dl_replay_packet_run_count != (packet_only ? 4u : 0u)) {
                return false;
            }
            // Different VCD with the same two-byte stride still requires cold
            // validation and a separate cache entry, not trust in the old run.
            backend->event_state_.load_cp(cp::kVcdLo,
                static_cast<std::uint32_t>(VcdType::Index16) << 9u);
            events.clear();
            backend->scan_pe_events_on_sim_thread(
                call.data(), call.size(), &memory, &services);
            if (events != expected || profile.call_dl_cache_hits != 2u ||
                profile.call_dl_cache_stores != 2u) {
                return false;
            }
            // Changed bytes must invalidate the trusted run and revalidate the
            // first draw before emitting any later token/finish callbacks.
            events.clear();
            list[1] = std::byte{0xFF};
            list[2] = std::byte{0xFF};
            bool rejected = false;
            try {
                backend->scan_pe_events_on_sim_thread(
                    call.data(), call.size(), &memory, &services);
            } catch (const GxFatalError&) {
                rejected = true;
            }
            if (!rejected || !events.empty()) return false;
        }
        return true;
    }

    static bool dependency_cache_rejects_fifo_fingerprint_collision() {
        auto backend = std::make_unique<GxBackend>();
        GuestMemoryV1 memory{};
        const GxState initial;
        std::vector<std::byte> first, second;
        append_cp_write(first, cp::kArrayBaseBase, 0x1000u);
        append_cp_write(second, cp::kArrayBaseBase, 0x2000u);
        std::vector<GuestMemoryRange> ranges;
        bool hit = false;
        backend->collect_memory_dependency_ranges(
            first.data(), first.size(), &memory, ranges, hit);
        for (auto& entry : backend->dependency_range_cache_) {
            if (entry.valid) {
                // Manufacture a lookup collision with equal initial state and
                // length, but different actual FIFO bytes and resulting state.
                entry.fifo_hash = dependency_fifo_fingerprint(second);
            }
        }
        backend->dependency_state_ = initial;
        backend->collect_memory_dependency_ranges(
            second.data(), second.size(), &memory, ranges, hit);
        if (hit || backend->dependency_state_.array_base(0u) != 0x2000u)
            return false;
        backend->dependency_state_ = initial;
        backend->collect_memory_dependency_ranges(
            second.data(), second.size(), &memory, ranges, hit);
        return hit && backend->dependency_state_.array_base(0u) == 0x2000u;
    }

    static bool dependency_cache_preserves_pending_bp_masks() {
        auto backend = std::make_unique<GxBackend>();
        GuestMemoryV1 memory{};
        GxState initial;
        initial.load_bp(0x64000080u);
        const std::array<std::byte, 1> fifo{std::byte{op::kNop}};
        std::vector<GuestMemoryRange> ranges;
        // Same visible register banks, different one-shot masks. A NOP must
        // preserve the mask for the next chunk, including on a cache hit.
        for (const std::uint32_t mask :
             {0x00FFFFFFu, 0u, 0xFFu, 0xFFFFu, 0x0F0F0Fu}) {
            GxState masked = initial;
            masked.load_bp(0xFE000000u | mask);
            for (unsigned replay = 0; replay < 2u; ++replay) {
                backend->dependency_state_ = masked;
                bool hit = false;
                backend->collect_memory_dependency_ranges(
                    fifo.data(), fifo.size(), &memory, ranges, hit);
                GxState continued = backend->dependency_state_;
                continued.load_bp(0x64000100u);
                const std::uint32_t expected = (0x80u & ~mask) | (0x100u & mask);
                if (!expect(continued.bp(bp::kTlutSrcAddr) == expected,
                            "cached NOP must preserve one-shot mask into next chunk")) return false;
                continued.load_bp(0x64000200u);
                if (!expect(continued.bp(bp::kTlutSrcAddr) == 0x200u,
                            "one-shot BP mask must reset after exactly one write")) return false;
                if (!expect(hit == (replay != 0u),
                            "whole-FIFO cache must distinguish pending BP masks and reuse identical masks")) return false;
            }
        }
        return true;
    }

    static bool dependency_cache_applies_bp_mask_to_tlut_ranges() {
        // Exercise actual register parsing and memory-range discovery, both
        // directly and through a guest display list. Independent literal
        // transfer sizes/addresses catch stale cached snapshot ownership.
        for (const bool nested : {false, true}) {
            auto backend = std::make_unique<GxBackend>();
            std::vector<std::byte> commands;
            commands.push_back(std::byte{op::kLoadBpReg});
            append_be_u32(commands, 0x64000100u);
            commands.push_back(std::byte{op::kLoadBpReg});
            append_be_u32(commands, 0x65000400u);
            commands.resize(32u, std::byte{op::kNop});
            std::vector<std::byte> fifo;
            if (nested) append_call_dl(fifo, 0x800u, static_cast<std::uint32_t>(commands.size()));
            else fifo = commands;
            GuestMemoryRegionV1 region{0x800u, static_cast<std::uint32_t>(commands.size()), commands.data()};
            GuestMemoryV1 memory{};
            memory.region_count = 1u;
            memory.regions = &region;
            GxState initial;
            initial.load_bp(0x64000080u);
            for (const std::uint32_t mask : {0x00FFFFFFu, 0u, 0xFFu, 0xFFFFu}) {
                GxState masked = initial;
                masked.load_bp(0xFE000000u | mask);
                for (unsigned replay = 0; replay < 2u; ++replay) {
                    backend->dependency_state_ = masked;
                    std::vector<GuestMemoryRange> ranges;
                    bool hit = false;
                    backend->collect_memory_dependency_ranges(
                        fifo.data(), fifo.size(), &memory, ranges, hit);
                    const std::uint32_t expected_source =
                        ((0x80u & ~mask) | (0x100u & mask)) << 5u;
                    const bool transfer = std::any_of(ranges.begin(), ranges.end(),
                        [&](const GuestMemoryRange& r) {
                            return r.guest_base == expected_source && r.size == 32u;
                        });
                    const bool owned_dl = !nested || std::any_of(ranges.begin(), ranges.end(),
                        [](const GuestMemoryRange& r) {
                            return r.guest_base == 0x800u && r.size == 32u;
                        });
                    if (hit != (replay != 0u) || !transfer || !owned_dl ||
                        ranges.size() != (nested ? 2u : 1u)) {
                        std::cerr << "TLUT mask probe nested=" << nested
                                  << " mask=" << mask << " replay=" << replay
                                  << " hit=" << hit << " expectedSource=" << expected_source
                                  << " finalSource=" << backend->dependency_state_.bp(bp::kTlutSrcAddr)
                                  << " ranges=" << ranges.size() << '\n';
                        for (const auto& range : ranges) {
                            std::cerr << "  base=" << range.guest_base << " size=" << range.size << '\n';
                        }
                    }
                    if (!expect(hit == (replay != 0u) && transfer && owned_dl &&
                                ranges.size() == (nested ? 2u : 1u),
                                "masked BP source write must select exact TLUT dependency and own nested DL bytes")) return false;
                    if (!expect(backend->dependency_state_.bp(bp::kTlutSrcAddr) == (expected_source >> 5u),
                                "cached masked transfer must preserve final BP state")) return false;
                }
            }
        }
        return true;
    }

    static bool dependency_scan_preserves_bp_masks_across_partial_commands() {
        std::vector<std::byte> fifo;
        fifo.push_back(std::byte{op::kLoadBpReg});
        append_be_u32(fifo, 0x64000100u);
        fifo.push_back(std::byte{op::kLoadBpReg});
        append_be_u32(fifo, 0x65000400u);
        GuestMemoryV1 memory{};
        for (std::size_t split = 1u; split < fifo.size(); ++split) {
            auto backend = std::make_unique<GxBackend>();
            for (const std::uint32_t mask : {0x00FFFFFFu, 0u, 0xFFu, 0xFFFFu}) {
                backend->dependency_state_ = GxState{};
                backend->dependency_state_.load_bp(0x64000080u);
                backend->dependency_state_.load_bp(0xFE000000u | mask);
                std::vector<GuestMemoryRange> ranges;
                bool hit = false;
                backend->collect_memory_dependency_ranges(
                    fifo.data(), split, &memory, ranges, hit);
                if (!expect(!hit && ranges.empty(),
                            "partial BP commands must not fabricate dependencies or a whole-FIFO cache hit")) return false;
                const bool pending_command = !backend->dependency_pending_fifo_.empty();
                backend->collect_memory_dependency_ranges(
                    fifo.data() + split, fifo.size() - split, &memory, ranges, hit);
                const std::uint32_t expected_source =
                    ((0x80u & ~mask) | (0x100u & mask)) << 5u;
                if (!expect((!pending_command || !hit) && backend->dependency_pending_fifo_.empty() &&
                            ranges.size() == 1u && ranges[0].guest_base == expected_source &&
                            ranges[0].size == 32u,
                            "partial FIFO continuation must consume the pending mask exactly once and own the correct TLUT")) return false;
            }
        }
        return true;
    }

    static bool dependency_invalidation_preserves_alias_and_edge_rules() {
        auto backend = std::make_unique<GxBackend>();
        backend->immutable_range_cache_.resize(2u);
        const auto arm_immutable = [&](std::uint32_t address, std::uint32_t size) {
            backend->immutable_range_cache_[0] = {};
            backend->immutable_range_cache_[0].guest_base = address;
            backend->immutable_range_cache_[0].size = size;
            backend->immutable_range_cache_[0].valid = true;
            backend->immutable_range_cache_[1] = {};
            backend->immutable_range_cache_[1].valid = true; // empty range
        };
        // Literal physical interval oracle, independent of the overlap helper.
        // Cache shapes use any of the six RAM mappings; dirty ranges always
        // come from the physical tracker and are sorted/coalesced by callers.
        const std::array<std::uint32_t, 6> aliases{
            0u, 0x80000000u, 0xC0000000u,
            0x10000000u, 0x90000000u, 0xD0000000u};
        for (const auto alias : aliases) {
            const std::uint32_t bank = alias & 0x10000000u;
            for (const std::uint32_t size : {1u, 32u, 96u}) {
                for (const std::uint32_t offset : {0x40u, 0x80u, 0x100u}) {
                    const std::uint64_t begin = bank + offset;
                    const std::uint64_t end = begin + size;
                    for (const std::uint32_t dirty_size : {1u, 32u, 64u}) {
                        for (const std::int32_t delta : {-96, -32, -1, 0, 1, 31, 32, 95, 96}) {
                            if (delta < 0 && offset < static_cast<std::uint32_t>(-delta)) continue;
                            const std::uint32_t dirty_begin =
                                bank + offset + static_cast<std::uint32_t>(delta);
                            std::vector<GxBackend::GuestWriteRange> writes{
                                {0x13FFF000u, 16u}, {dirty_begin, dirty_size},
                                {0x00002000u, 32u}, {0u, 0u}};
                            backend->coalesce_dirty_ranges(writes);
                            auto& entry = backend->dependency_range_cache_[0];
                            entry.valid = true;
                            entry.shape_ranges = {{alias + offset, size}, {alias + 0x8000u, 0u}};
                            arm_immutable(alias + offset, size);
                            backend->invalidate_dependency_range_cache(writes);
                            backend->invalidate_immutable_range_cache(writes);
                            const bool expected = static_cast<std::uint64_t>(dirty_begin) < end &&
                                static_cast<std::uint64_t>(dirty_begin) + dirty_size > begin;
                            if (entry.valid == expected ||
                                backend->immutable_range_cache_[0].valid == expected ||
                                !backend->immutable_range_cache_[1].valid) return false;
                        }
                    }
                }
            }
        }
        // Multiple shapes: an untouched first range cannot hide a later hit.
        auto& entry = backend->dependency_range_cache_[0];
        entry.valid = true;
        entry.shape_ranges = {{0x80000040u, 16u}, {0xD0000080u, 16u}};
        backend->invalidate_dependency_range_cache({{0x10000088u, 1u}});
        if (entry.valid) return false;
        // A range ending at 2^32 must not wrap and overlap low RAM.
        entry.valid = true;
        entry.shape_ranges = {{0xFFFFFFE0u, 32u}};
        arm_immutable(0xFFFFFFE0u, 32u);
        backend->invalidate_dependency_range_cache({{0u, 32u}, {0xFFFFFFF0u, 16u}});
        backend->invalidate_immutable_range_cache({{0u, 32u}, {0xFFFFFFF0u, 16u}});
        if (entry.valid || backend->immutable_range_cache_[0].valid) return false;
        entry.valid = true;
        arm_immutable(0xFFFFFFE0u, 32u);
        backend->invalidate_dependency_range_cache({{0u, 32u}});
        backend->invalidate_immutable_range_cache({{0u, 32u}});
        return entry.valid && backend->immutable_range_cache_[0].valid;
    }

    static bool snapshot_range_merge_preserves_exact_owned_bytes() {
        auto backend = std::make_unique<GxBackend>();
        constexpr std::uint32_t base = 0x1000u;
        std::array<std::byte, 256> bytes{};
        for (unsigned i = 0u; i < bytes.size(); ++i) bytes[i] = static_cast<std::byte>(i);
        GuestMemoryRegionV1 region{base, 256u, bytes.data()};
        GuestMemoryV1 memory{}; memory.regions = &region; memory.region_count = 1u;
        std::vector<GuestMemoryRange> ranges{{base + 64u, 32u}, {base + 16u, 16u},
            {base + 24u, 16u}, {base + 40u, 24u}, {base + 128u, 8u},
            {base + 136u, 8u}, {base + 16u, 8u}, {0xFFFFFFFFu, 0u}};
        for (const bool immutable : {false, true}) {
            for (unsigned permutation = 0u; permutation < ranges.size(); ++permutation) {
                GxBackend::MemorySnapshot snapshot;
                backend->capture_memory_snapshot(&memory, snapshot, ranges, true, immutable);
                if (!snapshot.sealed || snapshot.regions.size() != 2u) return false;
                // Independent union: [16,96) and [128,144), with no gaps copied.
                const auto& first = snapshot.regions[0]; const auto& second = snapshot.regions[1];
                if (first.guest_base != base + 16u || first.size != 80u ||
                    second.guest_base != base + 128u || second.size != 16u ||
                    first.host_base == bytes.data() + 16u || second.host_base == bytes.data() + 128u ||
                    std::memcmp(first.host_base, bytes.data() + 16u, 80u) != 0 ||
                    std::memcmp(second.host_base, bytes.data() + 128u, 16u) != 0) return false;
                std::rotate(ranges.begin(), ranges.begin() + 1u, ranges.end());
            }
        }
        // A union ending at 2^32 retains its last byte instead of wrapping.
        region.guest_base = 0xFFFFFFE0u; region.size = 32u;
        GxBackend::MemorySnapshot edge;
        backend->capture_memory_snapshot(&memory, edge,
            {{0xFFFFFFF0u, 16u}, {0xFFFFFFE0u, 20u}, {0u, 0u}}, true, false);
        return edge.sealed && edge.regions.size() == 1u &&
            edge.regions[0].guest_base == 0xFFFFFFE0u && edge.regions[0].size == 32u &&
            std::memcmp(edge.regions[0].host_base, bytes.data(), 32u) == 0;
    }

    static bool dirty_writes_cover_both_ram_boundaries() {
        auto backend = std::make_unique<GxBackend>();
        GuestMemoryV1 memory{};
        backend->install_guest_dirty_tracker(&memory);
        const std::array<GxBackend::GuestWriteRange, 4> expected{{
            {0x00000000u, 32u}, {0x017FFFE0u, 32u},
            {0x10000000u, 32u}, {0x13FFFFE0u, 32u}}};
        for (const std::uint32_t alias : {0u, 0x80000000u, 0xC0000000u}) {
            for (const bool fast : {false, true}) {
                for (const std::uint32_t address :
                     {0u, 0x017FFFFFu, 0x10000000u, 0x13FFFFFFu}) {
                    if (fast) guest_notify_write(&memory, address | alias, 1u);
                    else backend->notify_guest_memory_write(address | alias, 1u);
                }
                // Non-RAM writes must not create an alias of either bank.
                if (fast) guest_notify_write(&memory, 0x08000000u | alias, 1u);
                else backend->notify_guest_memory_write(0x08000000u | alias, 1u);
                std::vector<GxBackend::GuestWriteRange> ranges;
                backend->drain_guest_memory_writes(ranges);
                if (ranges.size() != expected.size()) return false;
                for (std::size_t i = 0; i < expected.size(); ++i) {
                    if (ranges[i].guest_addr != expected[i].guest_addr ||
                        ranges[i].size != expected[i].size) return false;
                }
                backend->drain_guest_memory_writes(ranges);
                if (!ranges.empty()) return false;
            }
            // The callback clamps a straddling write to real RAM. Test both
            // sides of each bank, including MEM1->hole and hole->MEM2.
            backend->notify_guest_memory_write(0x017FFFFFu | alias, 2u);
            backend->notify_guest_memory_write(0x0FFFFFFFu | alias, 2u);
            backend->notify_guest_memory_write(0x13FFFFFFu | alias, 2u);
            std::vector<GxBackend::GuestWriteRange> ranges;
            backend->drain_guest_memory_writes(ranges);
            if (ranges.size() != 3u) return false;
            for (std::size_t i = 0; i < ranges.size(); ++i) {
                if (ranges[i].guest_addr != expected[i + 1u].guest_addr ||
                    ranges[i].size != expected[i + 1u].size) return false;
            }
        }
        return true;
    }

    static bool dirty_writes_respect_efb_copy_boundaries() {
        auto backend = std::make_unique<GxBackend>();
        GuestMemoryV1 memory{};
        backend->install_guest_dirty_tracker(&memory);
        // GX copy destinations and tiled footprints use 32-byte units. Model
        // the observed copy whose end shares a 128-byte page with heap data.
        constexpr std::uint32_t copy_begin = 0x1326EBA0u;
        constexpr std::uint32_t copy_end = 0x13274FA0u;
        const auto check = [&](std::uint32_t address, std::uint32_t size,
                               bool expected_overlap, bool fast) {
            if (fast) guest_notify_write(&memory, address, size);
            else backend->notify_guest_memory_write(address, size);
            std::vector<GxBackend::GuestWriteRange> ranges;
            backend->drain_guest_memory_writes(ranges);
            const bool overlap = std::any_of(ranges.begin(), ranges.end(),
                [&](const auto& range) {
                    return range.guest_addr < copy_end &&
                        static_cast<std::uint64_t>(range.guest_addr) + range.size > copy_begin;
                });
            std::vector<GxBackend::GuestWriteRange> drained_again;
            backend->drain_guest_memory_writes(drained_again);
            return overlap == expected_overlap && !ranges.empty() && drained_again.empty();
        };
        for (const std::uint32_t alias : {0u, 0x80000000u, 0xC0000000u}) {
            for (const bool fast : {false, true}) {
                if (!check((copy_begin - 1u) | alias, 1u, false, fast) ||
                    !check(copy_end | alias, 4u, false, fast) ||
                    !check(copy_begin | alias, 1u, true, fast) ||
                    !check((copy_end - 1u) | alias, 1u, true, fast) ||
                    !check((copy_end - 1u) | alias, 2u, true, fast)) return false;
            }
        }
        return true;
    }

    static bool dependency_discovery_is_independent_and_tracks_dl_versions() {
        auto backend = std::make_unique<GxBackend>();
        constexpr std::uint32_t outer_addr = 0x2000u;
        constexpr std::uint32_t first_addr = 0x2100u;
        constexpr std::uint32_t second_addr = 0x2200u;
        std::vector<std::byte> live(0x300u, std::byte{0});
        const auto write_outer = [&](std::uint32_t target) {
            std::vector<std::byte> dl;
            append_call_dl(dl, target, 32u);
            std::fill_n(live.begin(), 32u, std::byte{0});
            std::copy(dl.begin(), dl.end(), live.begin());
        };
        write_outer(first_addr);
        GuestMemoryRegionV1 region{outer_addr, static_cast<std::uint32_t>(live.size()), live.data()};
        GuestMemoryV1 memory{};
        memory.region_count = 1u;
        memory.regions = &region;
        std::vector<std::byte> fifo;
        append_call_dl(fifo, outer_addr, 32u);
        std::vector<GuestMemoryRange> ranges;
        GxBackend::MemorySnapshot first, second;
        const auto capture_without_render_lock = [&](GxBackend::MemorySnapshot& snapshot) {
            // Model a renderer parked in PSO creation while retaining its
            // parser mutex. Release even on failure, so a regression reports
            // a failure rather than deadlocking the complete test process.
            std::unique_lock renderer_lock(backend->parser_cache_mutex_);
            auto scan = std::async(std::launch::async, [&] {
                bool hit = false;
                backend->collect_memory_dependency_ranges(fifo.data(), fifo.size(),
                    &memory, ranges, hit);
                backend->capture_memory_snapshot(&memory, snapshot, ranges, true, false);
            });
            const bool ready = scan.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
            renderer_lock.unlock();
            scan.get();
            return ready && snapshot.sealed && snapshot.memory.flat_guest_read_base == nullptr;
        };
        if (!capture_without_render_lock(first)) return false;
        const auto has = [&](std::uint32_t address) {
            return std::any_of(ranges.begin(), ranges.end(), [&](const GuestMemoryRange& r) {
                return r.guest_base == address && r.size == 32u;
            });
        };
        if (!has(outer_addr) || !has(first_addr) || has(second_addr)) return false;
        write_outer(second_addr);
        backend->invalidate_dependency_range_cache({{outer_addr, 32u}});
        if (!capture_without_render_lock(second)) return false;
        // Independent literal BE address oracles verify the first sealed
        // version survives both live mutation and the dependency cache refill.
        return has(outer_addr) && has(second_addr) && !has(first_addr) &&
            guest_load_u32(&first.memory, outer_addr + 1u, nullptr, 0u) == first_addr &&
            guest_load_u32(&second.memory, outer_addr + 1u, nullptr, 0u) == second_addr;
    }

    static bool sampler_keys_ignore_only_unsampled_maps() {
        auto backend = std::make_unique<GxBackend>();
        const auto write = [&](unsigned map, unsigned base, std::uint32_t value) {
            const unsigned reg = base + (map >= 4u ? bp::kTexHighBankOffset : 0u) + (map & 3u);
            backend->state_.load_bp((reg << 24u) | value);
        };
        std::array<TextureHandle, kMaxTextureMaps> handles{};
        for (unsigned active = 0u; active < kMaxTextureMaps; ++active) {
            // Repeat S, clamp T, linear mag/min, +12/32 bias, host 6-level
            // mip chain. Literal packed result: min becomes 6, max LOD 80.
            write(active, bp::kTexMode0Base, 1u | (1u << 4u) | (4u << 5u) | (12u << 9u));
            write(active, bp::kTexMode1Base, 0u);
            handles[active].generated_mips = true;
            handles[active].mip_levels = 6u;
            const auto mask = static_cast<std::uint8_t>(1u << active);
            std::array<std::uint32_t, kMaxTextureMaps> expected{};
            expected[active] = 0x50000CD1u;
            RenderConfig config{};
            SamplerTableCache cache;
            for (unsigned variant = 0u; variant < 300u; ++variant) {
                for (unsigned map = 0u; map < kMaxTextureMaps; ++map) {
                    if (map == active) continue;
                    write(map, bp::kTexMode0Base, variant * 997u + map);
                    write(map, bp::kTexMode1Base, variant * 131u + map);
                }
                const auto keys = backend->capture_sampler_keys(mask, handles);
                if (keys != expected) return false;
                const auto allocation = cache.get_or_allocate(make_sampler_table_key(keys.data(), config));
                if (allocation.index != 8u || allocation.created != (variant == 0u)) return false;
            }
            // A changed sampled slot still gets a distinct sampler table.
            write(active, bp::kTexMode0Base, 2u | (1u << 4u) | (4u << 5u) | (12u << 9u));
            auto changed = backend->capture_sampler_keys(mask, handles);
            expected[active] = 0x50000CD2u;
            if (changed != expected || !cache.get_or_allocate(make_sampler_table_key(changed.data(), config)).created)
                return false;
            handles[active].generated_mips = false;
            changed = backend->capture_sampler_keys(mask, handles);
            expected[active] = 0x00000C92u;
            if (changed != expected) return false;
        }
        return true;
    }

    static bool texture_keys_preserve_actual_palette_dependencies() {
        auto backend = std::make_unique<GxBackend>();
        for (unsigned map = 0u; map < kMaxTextureMaps; ++map) {
            const unsigned bank = map >= 4u ? bp::kTexHighBankOffset : 0u;
            const unsigned slot = map & 3u;
            const auto write = [&](unsigned base, std::uint32_t value) {
                backend->state_.load_bp(((base + bank + slot) << 24u) | value);
            };
            const auto mask = static_cast<std::uint8_t>(1u << map);
            for (auto format : {TexFormat::I4, TexFormat::I8, TexFormat::IA4,
                    TexFormat::IA8, TexFormat::RGB565, TexFormat::RGB5A3,
                    TexFormat::RGBA8, TexFormat::CMPR, TexFormat::C4,
                    TexFormat::C8, TexFormat::C14X2}) {
                write(bp::kTexImage0Base, 7u | (7u << 10u) |
                    (static_cast<std::uint32_t>(format) << 20u));
                write(bp::kTexMode0Base, 0u); // nearest: no host mip settings
                write(bp::kTexTlutBase, 0u);
                const auto binding = backend->capture_texture_binding_key(mask);
                const auto image = backend->state_.tex_image(map);
                const auto mode = backend->state_.tex_mode(map);
                const auto handle = backend->texture_handle_key(image, mode, backend->state_.tex_tlut(map));
                const bool indexed = format == TexFormat::C4 ||
                    format == TexFormat::C8 || format == TexFormat::C14X2;
                // Literal TLUT fields: slot in bits 0..9, format in 10..11.
                for (std::uint32_t palette : {1u, 1u << 10u, 1023u | (2u << 10u)}) {
                    write(bp::kTexTlutBase, palette);
                    const bool same_binding = binding == backend->capture_texture_binding_key(mask);
                    const bool same_handle = handle == backend->texture_handle_key(
                        image, mode, backend->state_.tex_tlut(map));
                    if (same_binding == indexed || same_handle == indexed) return false;
                }
                write(bp::kTexTlutBase, 0u);
                write(bp::kTexMode0Base, 1u); // changed S wrap remains relevant
                if (binding == backend->capture_texture_binding_key(mask)) return false;
            }
        }
        return true;
    }

    static bool identical_tlut_reload_preserves_backend_bindings() {
        auto backend = std::make_unique<GxBackend>();
        constexpr std::uint32_t address = 0x00001000u;
        std::array<std::byte, 32> bytes{};
        GuestMemoryRegionV1 region{address, 32u, bytes.data()};
        GuestMemoryV1 memory{};
        memory.regions = &region; memory.region_count = 1u;
        backend->frame_memory_ = &memory;
        backend->state_.load_bp(0x64000000u | (address >> 5u));
        backend->state_.load_bp(0x65000400u); // 32 bytes at bank offset zero
        GxBackend::TextureHandleKey handle_key{};
        TextureHandle handle{};
        GxBackend::TextureBindingKey binding_key{};
        GxBackend::TextureBindingTables tables{};
        backend->texture_handle_cache_.emplace(handle_key, handle);
        backend->frame_texture_handle_cache_.emplace(handle_key, handle);
        backend->frame_texture_binding_tables_.emplace(binding_key, tables);
        backend->persistent_texture_binding_tables_.emplace(binding_key, tables);
        backend->frame_texture_tables_.emplace(GxBackend::TextureTableKey{}, D3D12_GPU_DESCRIPTOR_HANDLE{});
        backend->texture_bindings_dirty_ = false;
        backend->current_texture_binding_key_valid_ = true;
        backend->on_tlut_load();
        if (backend->texture_handle_cache_.size() != 1u ||
            backend->frame_texture_handle_cache_.size() != 1u ||
            backend->frame_texture_binding_tables_.size() != 1u ||
            backend->persistent_texture_binding_tables_.size() != 1u ||
            backend->frame_texture_tables_.size() != 1u ||
            backend->texture_bindings_dirty_ || !backend->current_texture_binding_key_valid_) return false;
        bytes[0] = std::byte{0xff};
        backend->on_tlut_load();
        return backend->texture_handle_cache_.empty() &&
            backend->frame_texture_handle_cache_.empty() &&
            backend->frame_texture_binding_tables_.empty() &&
            backend->persistent_texture_binding_tables_.empty() &&
            backend->frame_texture_tables_.empty() &&
            backend->texture_bindings_dirty_ && !backend->current_texture_binding_key_valid_;
    }

    static bool texture_binding_footprints_retire_all_dependent_caches() {
        auto backend = std::make_unique<GxBackend>();
        constexpr std::uint32_t address = 0x00100000u;
        GxBackend::TextureHandleKey key{};
        key.guest_addr = address;
        key.width = key.height = 8u;
        key.format = static_cast<std::uint8_t>(TexFormat::RGB565);
        key.levels = 3u;
        TextureHandle handle{};
        // Literal independently derived extent: 128 + 32 + 32 for 8/4/2.
        handle.guest_byte_size = 192u;
        GxBackend::TextureBindingTables tables{};
        tables.dependency_count = 1u;
        tables.dependencies[0] = {address, 8u, 8u,
            static_cast<std::uint8_t>(TexFormat::RGB565), 192u};
        const auto populate = [&] {
            backend->texture_handle_cache_.emplace(key, handle);
            backend->frame_texture_handle_cache_.emplace(key, handle);
            backend->frame_texture_binding_tables_.emplace(
                GxBackend::TextureBindingKey{}, tables);
        };
        populate();
        backend->invalidate_texture_binding_cache_range(address + 160u, 1u);
        if (!backend->texture_handle_cache_.empty() ||
            !backend->frame_texture_handle_cache_.empty() ||
            !backend->frame_texture_binding_tables_.empty()) return false;

        // A strided EFB write's second block row overlaps only the lower mip
        // of a packed texture starting later in the same memory range.
        key.guest_addr = address + 384u;
        tables.dependencies[0].guest_addr = key.guest_addr;
        populate();
        EfbCopyParams copy{};
        copy.src_width = copy.src_height = 8u;
        copy.target_format = 6u;
        copy.dest_addr = address;
        copy.dest_stride = 512u;
        backend->mark_texture_binding_alias_dirty(copy);
        backend->prune_dirty_texture_binding_tables();
        if (!backend->texture_handle_cache_.empty() ||
            !backend->frame_texture_handle_cache_.empty() ||
            !backend->frame_texture_binding_tables_.empty()) return false;

        // Bounding span 512+128 ends at 640: the following byte is unrelated.
        key.guest_addr = address + 640u;
        tables.dependencies[0].guest_addr = key.guest_addr;
        populate();
        backend->mark_texture_binding_alias_dirty(copy);
        backend->prune_dirty_texture_binding_tables();
        return backend->texture_handle_cache_.size() == 1u &&
            backend->frame_texture_handle_cache_.size() == 1u &&
            backend->frame_texture_binding_tables_.size() == 1u;
    }

    static bool worker_failure_releases_queued_efb_peek() {
        auto backend = std::make_unique<GxBackend>();
        GxBackend::EfbPeekRequest request;
        GxBackend::FrameChunk queued;
        queued.efb_peek_only = true;
        queued.efb_peek_request = &request;
        backend->frame_queue_.push_back(std::move(queued));
        // A previous frame's completed PE callback fails before dequeueing
        // the peek. No GPU is needed: the uninitialized fence reports zero.
        GxBackend::PendingFrameEffects prior;
        prior.fence_value = 0u;
        prior.pe_events.push_back({GxBackend::PeEvent::Kind::Finish});
        backend->pending_frame_effects_.push_back(std::move(prior));
        std::thread worker([&] { backend->render_thread_main(); });
        bool completed = false;
        {
            std::unique_lock<std::mutex> lock(request.mutex);
            completed = request.cv.wait_for(lock, std::chrono::seconds(1),
                [&] { return request.done; });
        }
        worker.join();
        bool error_matches = false;
        if (request.error != nullptr) {
            try { std::rethrow_exception(request.error); }
            catch (const std::runtime_error& error) {
                error_matches = std::string(error.what()).find(
                    "without native services") != std::string::npos;
            }
        }
        return completed && !request.success && error_matches &&
            backend->stop_render_thread_ && backend->render_thread_error_ != nullptr &&
            backend->frame_queue_.size() == 1u &&
            backend->frame_queue_.front().efb_peek_request == nullptr;
    }

    static bool immutable_nested_display_list_versions_and_reuse() {
        constexpr std::uint32_t kMemoryBase = 0x00002000u;
        constexpr std::uint32_t kOuterDl = kMemoryBase + 0x100u;
        constexpr std::uint32_t kInnerDl = kMemoryBase + 0x200u;
        constexpr std::uint32_t kDisplayListSize = 32u;
        std::vector<std::byte> live(0x300u, std::byte{0});
        std::vector<std::byte> outer;
        append_call_dl(outer, kInnerDl, kDisplayListSize);
        outer.resize(kDisplayListSize, std::byte{0});
        std::copy(
            outer.begin(),
            outer.end(),
            live.begin() + (kOuterDl - kMemoryBase));
        const std::array<std::byte, kDisplayListSize> inner{
            std::byte{op::kLoadBpReg},
            std::byte{bp::kBpMask},
            std::byte{0xFF},
            std::byte{0xFF},
            std::byte{0xFF},
            std::byte{op::kLoadBpReg},
            std::byte{0x48},
            std::byte{0x12},
            std::byte{0x34},
            std::byte{0x56},
        };
        std::copy(
            inner.begin(),
            inner.end(),
            live.begin() + (kInnerDl - kMemoryBase));

        GuestMemoryRegionV1 region{
            kMemoryBase,
            static_cast<std::uint32_t>(live.size()),
            live.data(),
        };
        GuestMemoryV1 memory{};
        memory.region_count = 1u;
        memory.regions = &region;
        std::vector<std::byte> fifo;
        append_call_dl(fifo, kOuterDl, static_cast<std::uint32_t>(outer.size()));

        auto backend = std::make_unique<GxBackend>();
        std::vector<GuestMemoryRange> ranges;
        bool cache_hit = false;
        backend->collect_memory_dependency_ranges(
            fifo.data(), fifo.size(), &memory, ranges, cache_hit);
        if (cache_hit || ranges.size() != 2u) {
            std::cerr << "immutable ownership fixture: initial cacheHit="
                      << cache_hit << " ranges=" << ranges.size() << '\n';
            return false;
        }

        GxBackend::MemorySnapshot first;
        backend->capture_memory_snapshot(
            &memory, first, ranges, true, true);
        if (!first.sealed || !first.immutable_range_ownership ||
            first.copied_bytes != outer.size() + inner.size() ||
            first.reused_bytes != 0u ||
            guest_load_u8(&first.memory, kInnerDl + 9u, nullptr, 0u) !=
                0x56u) {
            std::cerr << "immutable ownership fixture: first copied="
                      << first.copied_bytes << " reused="
                      << first.reused_bytes << " ranges="
                      << first.regions.size() << '\n';
            return false;
        }

        live[(kInnerDl - kMemoryBase) + 9u] = std::byte{0xA5};
        const std::vector<GxBackend::GuestWriteRange> dirty{
            {kInnerDl + 9u, 1u}};
        backend->invalidate_dependency_range_cache(dirty);
        backend->invalidate_immutable_range_cache(dirty);
        ranges.clear();
        backend->collect_memory_dependency_ranges(
            fifo.data(), fifo.size(), &memory, ranges, cache_hit);

        GxBackend::MemorySnapshot second;
        backend->capture_memory_snapshot(
            &memory, second, ranges, true, true);
        if (!second.sealed || second.copied_bytes != inner.size() ||
            second.reused_bytes != outer.size() ||
            guest_load_u8(&first.memory, kInnerDl + 9u, nullptr, 0u) !=
                0x56u ||
            guest_load_u8(&second.memory, kInnerDl + 9u, nullptr, 0u) !=
                0xA5u) {
            std::cerr << "immutable ownership fixture: second copied="
                      << second.copied_bytes << " reused="
                      << second.reused_bytes << " ranges="
                      << second.regions.size() << '\n';
            return false;
        }

        GxBackend::MemorySnapshot reused;
        backend->capture_memory_snapshot(
            &memory, reused, ranges, true, true);
        return reused.sealed && reused.copied_bytes == 0u &&
            reused.reused_bytes == outer.size() + inner.size() &&
            reused.regions.size() == second.regions.size() &&
            reused.regions[0].host_base == second.regions[0].host_base &&
            reused.regions[1].host_base == second.regions[1].host_base &&
            std::all_of(
                reused.regions.begin(),
                reused.regions.end(),
                [&](const GuestMemoryRegionV1& owned) {
                    return owned.host_base < live.data() ||
                        owned.host_base >= live.data() + live.size();
                });
    }

    static bool full_capture_reuse_detaches_live_memory(
        GuestMemoryV1& memory,
        std::span<std::byte> live_bytes) {
        if (live_bytes.empty()) {
            return false;
        }
        auto backend = std::make_unique<GxBackend>();
        GxBackend::MemorySnapshot snapshot;
        const std::vector<GuestMemoryRange> misleading_partial_range{
            {kArrayBase, 1u}};

        live_bytes[0] = std::byte{0x3Cu};
        backend->capture_memory_snapshot(
            &memory,
            snapshot,
            misleading_partial_range,
            false);
        if (!snapshot.sealed || snapshot.memory.region_count != 1u ||
            snapshot.memory.regions == nullptr ||
            snapshot.memory.regions[0].host_base == live_bytes.data() ||
            snapshot.memory.regions[0].size != live_bytes.size() ||
            snapshot.memory.regions[0].host_base[0] != std::byte{0x3Cu}) {
            return false;
        }

        // Reuse the same pooled storage for a later frame. The earlier source
        // pointer was cleared when sealing, yet the new capture must refresh
        // every byte and remain detached from subsequent live mutations.
        live_bytes[0] = std::byte{0xA7u};
        backend->capture_memory_snapshot(
            &memory,
            snapshot,
            misleading_partial_range,
            false);
        live_bytes[0] = std::byte{0x11u};
        GxBackend::MemorySnapshot moved = std::move(snapshot);
        return moved.sealed &&
            moved.memory.regions == moved.regions.data() &&
            moved.memory.regions[0].host_base != live_bytes.data() &&
            moved.memory.regions[0].host_base[0] == std::byte{0xA7u} &&
            moved.memory.user == nullptr &&
            moved.memory.read_device == nullptr &&
            moved.memory.write_device == nullptr &&
            moved.memory.notify_write == nullptr &&
            moved.memory.flat_guest_read_base == nullptr &&
            moved.memory.dirty_page_words == nullptr &&
            moved.memory.cpu_dirty_page_words == nullptr &&
            std::all_of(
                moved.storage.begin(),
                moved.storage.end(),
                [](const GxBackend::MemorySnapshotStorage& storage) {
                    return !storage.used || storage.source == nullptr;
                });
    }

    static bool decoded_vertex_dependencies_are_byte_exact() {
        constexpr std::uint32_t kBase = 0x00004000u;
        std::vector<std::byte> live(32u);
        for (std::size_t i = 0; i < live.size(); ++i) {
            live[i] = static_cast<std::byte>(i);
        }
        GuestMemoryRegionV1 region{
            kBase,
            static_cast<std::uint32_t>(live.size()),
            live.data(),
        };
        GuestMemoryV1 memory{};
        memory.region_count = 1u;
        memory.regions = &region;
        const std::vector<VertexDecodeGuestRange> dependencies{
            {kBase + 4u, 4u},
            {kBase + 16u, 3u},
        };
        std::vector<
            GxBackend::DecodedPacketRunCacheEntry::GuestDependencySnapshot>
            snapshots;
        std::size_t captured_bytes = 0u;
        if (!GxBackend::capture_decoded_packet_run_dependencies(
                &memory, dependencies, 7u, snapshots, captured_bytes) ||
            captured_bytes != 7u || snapshots.size() != 2u ||
            !GxBackend::decoded_packet_run_dependencies_match(
                &memory, snapshots)) {
            return false;
        }

        // Writes outside recorded indexed-array reads cannot invalidate the
        // decoded result. A write inside either range must reject it even when
        // no dirty-page notification was delivered.
        live[0] = std::byte{0xFE};
        if (!GxBackend::decoded_packet_run_dependencies_match(
                &memory, snapshots)) {
            return false;
        }
        const std::byte original = live[17];
        live[17] = std::byte{0xA5};
        if (GxBackend::decoded_packet_run_dependencies_match(
                &memory, snapshots)) {
            return false;
        }
        live[17] = original;
        if (!GxBackend::decoded_packet_run_dependencies_match(
                &memory, snapshots)) {
            return false;
        }

        // A descriptor with no backing storage must not become a non-null
        // pointer through address-offset arithmetic. It also cannot shadow
        // a later actual owner of the same guest interval.
        std::array<GuestMemoryRegionV1, 2> nullable_regions{{
            {kBase, static_cast<std::uint32_t>(live.size()), nullptr}, region}};
        memory.regions = nullable_regions.data();
        memory.region_count = 2u;
        if (!GxBackend::decoded_packet_run_dependencies_match(&memory, snapshots) ||
            !GxBackend::capture_decoded_packet_run_dependencies(
                &memory, dependencies, 7u, snapshots, captured_bytes) ||
            captured_bytes != 7u || snapshots.size() != 2u ||
            snapshots[0].bytes != std::vector<std::byte>(live.begin() + 4u, live.begin() + 8u) ||
            snapshots[1].bytes != std::vector<std::byte>(live.begin() + 16u, live.begin() + 19u)) return false;
        memory.region_count = 1u; // only the unbacked descriptor remains
        if (GxBackend::decoded_packet_run_dependencies_match(&memory, snapshots)) return false;
        captured_bytes = 99u;
        if (GxBackend::capture_decoded_packet_run_dependencies(
                &memory, dependencies, 7u, snapshots, captured_bytes) ||
            !snapshots.empty() || captured_bytes != 0u) return false;
        memory.regions = &region; memory.region_count = 1u;

        // Reject the entire capture when its byte budget is insufficient.
        captured_bytes = 99u;
        return !GxBackend::capture_decoded_packet_run_dependencies(
                   &memory, dependencies, 6u, snapshots, captured_bytes) &&
            snapshots.empty() && captured_bytes == 0u;
    }

    static bool decoded_vertex_keys_track_only_consumed_state() {
        auto backend = std::make_unique<GxBackend>();
        auto& state = backend->state_;
        state.load_cp(cp::kVcdLo, 1u << 9u); // direct position, no matrix index
        state.load_cp(cp::kVatABase, 1u | (4u << 1u)); // F32 XYZ
        std::vector<std::byte> bytes;
        append_be_u16(bytes, 1u);
        for (const std::uint32_t bits : {0x3f800000u, 0x40000000u, 0x40400000u})
            append_be_u32(bytes, bits);
        CachedDrawPacket packet{};
        packet.vertex_count = 1u;
        packet.draw_payload_size = 12u;
        const auto key = [&] {
            return backend->decoded_packet_run_cache_key(
                1u, 0u, PrimitiveClass::Points, 0u, state.vertex_desc(0u));
        };
        VertexLoader loader;
        const auto decode = [&](GuestMemoryV1* memory = nullptr) {
            const auto desc = state.vertex_desc(0u);
            return loader.decode_cached_packet_run_vertices_with_layout(
                bytes, 0u, std::span<const CachedDrawPacket>(&packet, 1u),
                PrimitiveClass::Points, 0u, state, desc,
                VertexLoader::source_vertex_size(desc), memory);
        };
        const auto equal_vertices = [](const auto& a, const auto& b) {
            return a.vertices.size() == b.vertices.size() &&
                std::memcmp(a.vertices.data(), b.vertices.data(),
                    a.vertices.size() * sizeof(GxVertexOut)) == 0;
        };
        const auto initial_key = key();
        const auto initial = decode();
        state.load_cp(cp::kMatrixIndexA, 0x123400u); // low six bits unchanged
        state.load_cp(cp::kMatrixIndexB, 0x567890u);
        for (unsigned attr = 0u; attr < 12u; ++attr) {
            state.load_cp(static_cast<std::uint8_t>(cp::kArrayBaseBase + attr), 0x1000u + attr * 32u);
            state.load_cp(static_cast<std::uint8_t>(cp::kArrayStrideBase + attr), 16u + attr);
        }
        if (!(key() == initial_key) || !equal_vertices(decode(), initial)) return false;
        state.load_cp(cp::kMatrixIndexA, 3u);
        if (key() == initial_key || equal_vertices(decode(), initial)) return false;

        // A packet-owned PNMTXIDX supersedes the default CP position index.
        state.load_cp(cp::kVcdLo, (1u << 9u) | 1u);
        bytes.insert(bytes.begin() + 2u, std::byte{6u});
        packet.draw_payload_size = 13u;
        const auto packet_key = key();
        const auto packet_owned = decode();
        state.load_cp(cp::kMatrixIndexA, 12u);
        if (!(key() == packet_key) || !equal_vertices(decode(), packet_owned)) return false;

        // Indexed position base and stride really do change the decoded data.
        state.load_cp(cp::kVcdLo, 2u << 9u); // Index8 position
        state.load_cp(cp::kArrayBaseBase, 0x1000u);
        state.load_cp(cp::kArrayStrideBase, 12u);
        std::array<std::byte, 32> arrays{};
        const std::array<std::uint32_t, 8> values{
            0x3f800000u, 0x40000000u, 0x40400000u, 0u,
            0x40800000u, 0x40a00000u, 0x40c00000u, 0u};
        for (unsigned i = 0u; i < values.size(); ++i)
            for (unsigned byte = 0u; byte < 4u; ++byte)
                arrays[i * 4u + byte] = static_cast<std::byte>(values[i] >> (24u - byte * 8u));
        GuestMemoryRegionV1 region{0x1000u, static_cast<std::uint32_t>(arrays.size()), arrays.data()};
        GuestMemoryV1 memory{};
        memory.regions = &region;
        memory.region_count = 1u;
        bytes = {std::byte{0u}, std::byte{1u}, std::byte{0u}};
        packet.draw_payload_size = 1u;
        const auto indexed_key = key();
        const auto indexed = decode(&memory);
        state.load_cp(cp::kArrayBaseBase, 0x1010u);
        if (key() == indexed_key || equal_vertices(decode(&memory), indexed)) return false;
        state.load_cp(cp::kArrayBaseBase, 0x1000u);
        bytes[2] = std::byte{1u};
        const auto stride_key = key();
        const auto stride = decode(&memory);
        state.load_cp(cp::kArrayStrideBase, 16u);
        return !(key() == stride_key) && !equal_vertices(decode(&memory), stride);
    }

    static bool worker_stage_seals_before_releasing(
        const NbtFixture& fixture,
        GuestMemoryV1& memory,
        std::span<std::byte> live_bytes) {
        auto backend = std::make_unique<GxBackend>();
        GxBackend::FrameChunk queued;
        queued.fifo = fixture.fifo;
        queued.memory = &memory;
        queued.worker_memory_snapshot = true;
        backend->frame_pe_completions_.reset();
        queued.pe_completion_token = backend->frame_pe_completions_.issue();
        queued.pe_completion_requires_resolution = true;
        queued.fifo_effects_pending = true;
        backend->render_fifo_effects_in_flight_ = 1u;
        // Mirror queue handoff: ownership moves before the worker seals it.
        GxBackend::FrameChunk chunk = std::move(queued);
        chunk.cpu_reads_pending = true;
        backend->render_cpu_reads_in_flight_ = 1u;
        const FramePeCompletionToken token = chunk.pe_completion_token;
        const std::vector<std::byte> before(live_bytes.begin(), live_bytes.end());
        backend->prepare_worker_memory_snapshot(chunk);
        if (!chunk.memory_snapshot_active || !chunk.memory_snapshot_strict ||
            !chunk.memory_snapshot.sealed || chunk.cpu_reads_pending ||
            backend->render_cpu_reads_in_flight_ != 0u ||
            !chunk.fifo_effects_pending ||
            backend->render_fifo_effects_in_flight_ != 1u ||
            chunk.pe_completion_token != token ||
            chunk.memory != &chunk.memory_snapshot.memory ||
            chunk.memory->flat_guest_read_base != nullptr ||
            chunk.memory->read_device != nullptr ||
            chunk.memory->write_device != nullptr ||
            chunk.memory->notify_write != nullptr ||
            chunk.memory->cpu_dirty_page_words != nullptr ||
            chunk.memory->dirty_page_words != nullptr ||
            std::any_of(chunk.memory_snapshot.storage.begin(),
                        chunk.memory_snapshot.storage.end(),
                        [](const GxBackend::MemorySnapshotStorage& storage) {
                            return storage.used && storage.source != nullptr;
                        })) {
            return false;
        }
        std::fill(live_bytes.begin(), live_bytes.end(), std::byte{0xC5});
        for (std::size_t i = 0; i < before.size(); ++i) {
            if (guest_load_u8(chunk.memory, kArrayBase +
                    static_cast<std::uint32_t>(i), nullptr, 0u) !=
                std::to_integer<std::uint8_t>(before[i])) {
                return false;
            }
        }
        // A missed dependency is a strict missing read, never a live fallback.
        bool missing_read_failed = false;
        NativeServicesV1 fault_services{};
        fault_services.fatal = &throw_guest_memory_fault;
        try {
            (void)guest_load_u8(chunk.memory,
                kArrayBase + static_cast<std::uint32_t>(before.size()),
                &fault_services, 0u);
        } catch (const std::runtime_error& error) {
            missing_read_failed =
                std::string(error.what()).find("is not mapped") != std::string::npos;
        }
        std::copy(before.begin(), before.end(), live_bytes.begin());
        // Retire the synthetic token only after testing the unchanged boundary.
        backend->frame_pe_completions_.resolve_on_submission(token);
        backend->frame_pe_completions_.consume_wait(token);
        backend->frame_pe_completions_.audit_drained();
        return missing_read_failed;
    }

    static bool worker_pe_classification_uses_sealed_display_list(bool has_event) {
        constexpr std::uint32_t kDisplayList = 0x00002000u;
        std::array<std::byte, 32u> live{};
        live[0] = std::byte{op::kLoadBpReg};
        live[1] = std::byte{0x48};
        live[2] = std::byte{0x00};
        live[3] = std::byte{0x34};
        live[4] = std::byte{0x56};
        if (!has_event) live.fill(std::byte{0});
        GuestMemoryRegionV1 region{kDisplayList,
            static_cast<std::uint32_t>(live.size()), live.data()};
        GuestMemoryV1 memory{};
        memory.region_count = 1u;
        memory.regions = &region;
        auto backend = std::make_unique<GxBackend>();
        backend->frame_pe_completions_.reset();
        GxBackend::FrameChunk chunk;
        append_call_dl(chunk.fifo, kDisplayList,
            static_cast<std::uint32_t>(live.size()));
        chunk.memory = &memory;
        chunk.worker_memory_snapshot = true;
        chunk.cpu_reads_pending = true;
        backend->render_cpu_reads_in_flight_ = 1u;
        const auto submitted = std::make_shared<FramePeCompletionToken>();
        chunk.worker_submission_token = submitted;
        backend->prepare_worker_memory_snapshot(chunk);
        // A later writer may change the token or remove it. Classification
        // and subsequent rendering must continue to use the sealed bytes.
        live.fill(std::byte{0});
        FramePeEventClassifier verifier;
        std::vector<FramePeEventSignature> verified;
        verifier.classify(chunk.fifo.data(), chunk.fifo.size(),
            chunk.memory, verified);
        const bool valid = chunk.memory_snapshot.sealed &&
            !chunk.cpu_reads_pending && chunk.pe_events_preclassified &&
            chunk.pe_event_free_receipt == !has_event &&
            *submitted == chunk.pe_completion_token &&
            submitted->kind == (has_event
                ? cadence::FrameTokenKind::EventBearing
                : cadence::FrameTokenKind::EventFree) &&
            backend->frame_pe_completions_.probe_wait(*submitted) ==
                (has_event ? FramePeWaitState::Pending : FramePeWaitState::Complete) &&
            chunk.fifo_effects_pending == has_event &&
            verified == chunk.preclassified_pe_events &&
            (has_event ? (verified.size() == 1u &&
                verified[0].kind == FramePeEventSignature::Kind::Token &&
                verified[0].token == 0x3456u) : verified.empty());
        if (has_event) backend->frame_pe_completions_.resolve_on_submission(*submitted);
        backend->frame_pe_completions_.consume_wait(*submitted);
        backend->frame_pe_completions_.audit_drained();
        return valid;
    }

    static bool worker_dependency_scan_failure_does_not_release(
        const NbtFixture& fixture,
        GuestMemoryV1& memory) {
        auto backend = std::make_unique<GxBackend>();
        GxBackend::FrameChunk chunk;
        chunk.fifo = fixture.fifo;
        // Broad array capture clips to the mapped region, so shortening NBT
        // storage alone can seal successfully and fault only at decode. An
        // unmapped CALL_DL instead requires a live read during the prepass,
        // before capture/seal/release, and deterministically fails that scan.
        constexpr std::uint32_t kUnmappedDisplayList = 0xF0000000u;
        append_call_dl(chunk.fifo, kUnmappedDisplayList, 32u);
        chunk.memory = &memory;
        chunk.worker_memory_snapshot = true;
        chunk.cpu_reads_pending = true;
        backend->render_cpu_reads_in_flight_ = 1u;
        bool failed = false;
        try {
            backend->prepare_worker_memory_snapshot(chunk);
        } catch (const GxFatalError& error) {
            failed = std::string(error.what()).find(
                "unresolvable guest pointer 0xF0000000") != std::string::npos;
        }
        return failed && chunk.cpu_reads_pending &&
            backend->render_cpu_reads_in_flight_ == 1u &&
            chunk.memory == &memory && !chunk.memory_snapshot_active &&
            !chunk.memory_snapshot.sealed && !chunk.memory_snapshot_strict;
    }

    static MemorySnapshotNbtProbeResult collect_capture_and_decode(
        const NbtFixture& fixture,
        GuestMemoryV1& memory,
        std::span<std::byte> live_bytes) {
        auto backend = std::make_unique<GxBackend>();
        MemorySnapshotNbtProbeResult result;
        backend->collect_memory_dependency_ranges(
            fixture.fifo.data(),
            fixture.fifo.size(),
            &memory,
            result.ranges,
            result.dependency_cache_hit);

        GxBackend::MemorySnapshot snapshot;
        backend->capture_memory_snapshot(
            &memory,
            snapshot,
            result.ranges,
            true);
        result.snapshot_bytes = GxBackend::memory_snapshot_used_bytes(snapshot);
        result.sealed = snapshot.sealed;
        result.external_aliases_detached =
            snapshot.memory.user == nullptr &&
            snapshot.memory.read_device == nullptr &&
            snapshot.memory.write_device == nullptr &&
            snapshot.memory.notify_write == nullptr &&
            snapshot.memory.flat_guest_read_base == nullptr &&
            snapshot.memory.dirty_page_words == nullptr &&
            snapshot.memory.cpu_dirty_page_words == nullptr &&
            std::all_of(
                snapshot.storage.begin(),
                snapshot.storage.end(),
                [](const GxBackend::MemorySnapshotStorage& storage) {
                    return !storage.used || storage.source == nullptr;
                });

        // Prove delayed parsing owns immutable bytes. If any vertex component
        // still aliases live guest memory, the decoded NBT values become zero.
        std::fill(live_bytes.begin(), live_bytes.end(), std::byte{0});

        CachedDrawPacket packet{};
        packet.opcode = std::to_integer<std::uint8_t>(
            fixture.fifo[fixture.draw_opcode_offset]);
        packet.vertex_count = 1u;
        packet.local_opcode_offset = fixture.draw_opcode_offset;
        packet.draw_cursor_offset = fixture.draw_opcode_offset + 1u;
        packet.draw_payload_size = 1u;

        const VertexDescriptor desc = backend->dependency_state_.vertex_desc(0u);
        VertexLoader loader;
        result.decoded = loader.decode_cached_packet_run_vertices_with_layout(
            fixture.fifo,
            0u,
            std::span<const CachedDrawPacket>(&packet, 1u),
            PrimitiveClass::Points,
            0u,
            backend->dependency_state_,
            desc,
            VertexLoader::source_vertex_size(desc),
            &snapshot.memory);
        return result;
    }
};

}  // namespace galaxy::gx

int main() {
    if (!expect(galaxy::gx::GxBackendMemorySnapshotTestAccess::
        dependency_cache_rejects_fifo_fingerprint_collision(),
        "FIFO fingerprint collision must compare bytes and preserve actual resulting state")) return 1;
    const bool mask_preserved = galaxy::gx::GxBackendMemorySnapshotTestAccess::
        dependency_cache_preserves_pending_bp_masks();
    const bool masked_ranges = galaxy::gx::GxBackendMemorySnapshotTestAccess::
        dependency_cache_applies_bp_mask_to_tlut_ranges();
    const bool partial_masks = galaxy::gx::GxBackendMemorySnapshotTestAccess::
        dependency_scan_preserves_bp_masks_across_partial_commands();
    if (!mask_preserved || !masked_ranges || !partial_masks) return 1;
    if (!expect(galaxy::gx::GxBackendMemorySnapshotTestAccess::
        dependency_invalidation_preserves_alias_and_edge_rules(),
        "dependency invalidation preserves all RAM aliases, half-open edges, multiple shapes and u32 end boundaries")) return 1;
    if (!expect(galaxy::gx::GxBackendMemorySnapshotTestAccess::
        dirty_writes_cover_both_ram_boundaries(),
        "dirty writes cover MEM1/MEM2 boundaries, aliases and clamped spans without bridging the physical hole")) return 1;
    if (!expect(galaxy::gx::GxBackendMemorySnapshotTestAccess::
        dirty_writes_respect_efb_copy_boundaries(),
        "writes adjacent to a GX copy must not invalidate its GPU contents; overlapping writes must")) return 1;
    if (!expect(galaxy::gx::GxBackendMemorySnapshotTestAccess::
        dependency_discovery_is_independent_and_tracks_dl_versions(),
        "dependency discovery must not wait for rendering and must track dirty nested DL versions")) return 1;
    if (!expect(galaxy::gx::GxBackendMemorySnapshotTestAccess::
        sampler_keys_ignore_only_unsampled_maps(),
        "300 unused-slot changes reuse one sampler table while sampled filters and generated mips remain effective")) return 1;
    if (!expect(galaxy::gx::GxBackendMemorySnapshotTestAccess::
        texture_keys_preserve_actual_palette_dependencies(),
        "backend texture keys ignore palettes only for direct formats in both register banks")) return 1;
    if (!expect(galaxy::gx::GxBackendMemorySnapshotTestAccess::
        identical_tlut_reload_preserves_backend_bindings(),
        "identical TLUT reload preserves backend caches; changed bytes invalidate all bindings")) return 1;
    if (!expect(galaxy::gx::GxBackendMemorySnapshotTestAccess::
        texture_binding_footprints_retire_all_dependent_caches(),
        "lower-mip and strided EFB writes retire backend handles and binding tables")) return 1;
    if (!expect(galaxy::gx::GxBackendMemorySnapshotTestAccess::
            worker_failure_releases_queued_efb_peek(),
            "terminal worker failure wakes queued EFB peek and detaches stack storage")) {
        return 1;
    }
    if (_putenv_s("GALAXY_DEPENDENCY_SNAPSHOT_BROAD_INDEX_RANGES", "1") != 0 ||
        _putenv_s("GALAXY_RENDER_MEMORY_SNAPSHOT_RANGES", "") != 0 ||
        _putenv_s("GALAXY_PE_EVENTS_GPU_FENCE", "1") != 0) {
        std::cerr << "FAIL: could not establish deterministic snapshot settings\n";
        return 1;
    }

    if (!expect(galaxy::gx::GxBackendMemorySnapshotTestAccess::
        snapshot_range_merge_preserves_exact_owned_bytes(),
        "range merge preserves sorted disjoint snapshot ownership across overlaps, zeros and u32 ends")) return 1;

    const NbtFixture fixture = make_shared_index_nbt_fixture();
    std::vector<std::byte> live_bytes(kRequiredArrayBytes);
    constexpr std::array<float, 9> kNbtValues{
        1.0f, 2.0f, 3.0f,
        4.0f, 5.0f, 6.0f,
        7.0f, 8.0f, 9.0f};
    for (std::size_t i = 0; i < kNbtValues.size(); ++i) {
        store_be_f32(
            live_bytes,
            static_cast<std::size_t>(kLastElementOffset) + i * sizeof(float),
            kNbtValues[i]);
    }

    galaxy::GuestMemoryRegionV1 region{
        kArrayBase,
        static_cast<std::uint32_t>(live_bytes.size()),
        live_bytes.data()};
    galaxy::GuestMemoryV1 memory{};
    memory.region_count = 1u;
    memory.regions = &region;
    memory.user = &memory;
    memory.read_device = &reject_device_read;
    memory.write_device = &reject_device_write;
    memory.notify_write = &observe_guest_write;
    // Snapshot capture must remove even a capability that bypasses regions.
    memory.flat_guest_read_base = live_bytes.data();
    std::array<std::atomic_uint64_t, 1> dirty_pages{};
    std::array<std::uint64_t, 1> cpu_dirty_pages{};
    memory.dirty_page_words = dirty_pages.data();
    memory.dirty_tracked_base = kArrayBase;
    memory.dirty_tracked_size = static_cast<std::uint32_t>(live_bytes.size());
    memory.dirty_page_shift = 12u;
    memory.dirty_page_word_count = 1u;
    memory.cpu_dirty_page_words = cpu_dirty_pages.data();
    memory.cpu_dirty_tracked_base = kArrayBase;
    memory.cpu_dirty_tracked_size =
        static_cast<std::uint32_t>(live_bytes.size());
    memory.cpu_dirty_page_shift = 12u;
    memory.cpu_dirty_page_word_count = 1u;

    bool ok = true;
    ok = expect(
             galaxy::gx::GxBackendMemorySnapshotTestAccess::
                 direct_pe_cached_draw_runs_preserve_events(),
             "direct PE cached runs changed event order, masks or truncation checks") && ok;
    ok = expect(
             galaxy::gx::GxBackendMemorySnapshotTestAccess::
                 decoded_vertex_keys_track_only_consumed_state(),
             "decoded vertex key lost a consumed input or retained unrelated CPU state") && ok;
    ok = expect(
             galaxy::gx::GxBackendMemorySnapshotTestAccess::
                 immutable_nested_display_list_versions_and_reuse(),
             "immutable nested display-list ownership did not version and reuse exact ranges") &&
        ok;
    ok = expect(
             galaxy::gx::GxBackendMemorySnapshotTestAccess::
                 full_capture_reuse_detaches_live_memory(memory, live_bytes),
             "full pooled snapshot retained or reused a live-memory alias") &&
        ok;
    ok = expect(
             galaxy::gx::GxBackendMemorySnapshotTestAccess::
                 decoded_vertex_dependencies_are_byte_exact(),
             "decoded vertex cache accepted stale or partial guest dependencies") &&
        ok;

    ok = expect(
             galaxy::gx::GxBackendMemorySnapshotTestAccess::
                 worker_stage_seals_before_releasing(fixture, memory, live_bytes),
             "worker snapshot release retained live bytes or resolved PE early") && ok;

    ok = expect(
             galaxy::gx::GxBackendMemorySnapshotTestAccess::
                 worker_pe_classification_uses_sealed_display_list(true),
             "worker PE classification did not retain the sealed display-list token") && ok;
    ok = expect(
             galaxy::gx::GxBackendMemorySnapshotTestAccess::
                 worker_pe_classification_uses_sealed_display_list(false),
             "worker event-free sealed display list did not publish a ready receipt") && ok;
    galaxy::gx::MemorySnapshotNbtProbeResult result;
    try {
        result = galaxy::gx::GxBackendMemorySnapshotTestAccess::
            collect_capture_and_decode(fixture, memory, live_bytes);
    } catch (const std::exception& error) {
        std::cerr << "FAIL: valid compact NBT snapshot threw: "
                  << error.what() << '\n';
        return 1;
    }

    ok = expect(!result.dependency_cache_hit,
                "first dependency collection unexpectedly hit cache") && ok;
    ok = expect(result.ranges.size() == 1u,
                "shared-index NBT collection did not produce one range") && ok;
    if (result.ranges.size() == 1u) {
        ok = expect(result.ranges[0].guest_base == kArrayBase,
                    "shared-index NBT range has the wrong base") && ok;
        ok = expect(result.ranges[0].size == kRequiredArrayBytes,
                    "shared-index NBT range omitted tangent/binormal bytes") && ok;
    }
    ok = expect(result.snapshot_bytes == kRequiredArrayBytes,
                "strict compact snapshot copied the wrong byte count") && ok;
    ok = expect(result.sealed,
                "strict compact snapshot was not sealed") && ok;
    ok = expect(result.external_aliases_detached,
                "strict compact snapshot retained a live-memory alias") && ok;
    ok = expect(result.decoded.vertices.size() == 1u,
                "snapshot decode did not produce one vertex") && ok;
    if (result.decoded.vertices.size() == 1u) {
        const galaxy::gx::GxVertexOut& vertex = result.decoded.vertices[0];
        ok = expect_vec3(vertex.normal, {1.0f, 2.0f, 3.0f},
                         "snapshot normal did not survive live-memory mutation") && ok;
        ok = expect_vec3(vertex.tangent, {4.0f, 5.0f, 6.0f},
                         "snapshot tangent did not survive live-memory mutation") && ok;
        ok = expect_vec3(vertex.binormal, {7.0f, 8.0f, 9.0f},
                         "snapshot binormal did not survive live-memory mutation") && ok;
    }
    ok = expect(result.decoded.guest_array_reads.size() == 1u,
                "vertex decoder did not report one NBT array read") && ok;
    if (result.decoded.guest_array_reads.size() == 1u) {
        const galaxy::gx::VertexDecodeGuestRange& read =
            result.decoded.guest_array_reads[0];
        ok = expect(read.guest_base == kArrayBase + kLastElementOffset,
                    "vertex decoder reported the wrong NBT read base") && ok;
        ok = expect(read.size == kNbtElementSize,
                    "vertex decoder did not request the complete N/T/B element") && ok;
    }

    // A snapshot that is genuinely too short must not alias the original
    // source as a fallback. The native vertex loader must hard-fail its exact
    // 36-byte read instead of decoding partial NBT data.
    std::vector<std::byte> short_live_bytes(
        kRequiredArrayBytes - 24u,
        std::byte{0x5A});
    region.size = static_cast<std::uint32_t>(short_live_bytes.size());
    region.host_base = short_live_bytes.data();
    memory.flat_guest_read_base = short_live_bytes.data();
    ok = expect(
             galaxy::gx::GxBackendMemorySnapshotTestAccess::
                 worker_dependency_scan_failure_does_not_release(fixture, memory),
             "failed worker dependency scan released the live-memory obligation") && ok;
    bool short_range_hard_failed = false;
    try {
        (void)galaxy::gx::GxBackendMemorySnapshotTestAccess::
            collect_capture_and_decode(fixture, memory, short_live_bytes);
    } catch (const galaxy::gx::GxFatalError& error) {
        short_range_hard_failed =
            std::string(error.what()).find("is not mapped") != std::string::npos;
    }
    ok = expect(short_range_hard_failed,
                "short strict snapshot did not hard-fail the complete NBT read") && ok;

    if (!ok) {
        return 1;
    }
    std::cout << "compact memory snapshot tests passed\n";
    return 0;
}
