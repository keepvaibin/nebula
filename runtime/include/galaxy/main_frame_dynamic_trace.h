#pragma once

#include "galaxy/frame_cadence_diagnostics.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace galaxy::diagnostics {

// Runtime-owned diagnostic data only; not part of the native module ABI.
inline constexpr std::array<std::size_t, 10u> kMemberEntryRegisters{
    0u, 1u, 3u, 4u, 5u, 11u, 12u, 29u, 30u, 31u};

struct MainFrameDynamicCallRecord {
    std::uint64_t vi{};
    std::uint64_t host_time_ns{};
    std::uint64_t elapsed_ns{};
    std::uint64_t thread_cpu_elapsed_100ns{};
    cadence::DspPollTimingSnapshot dsp_poll_timing{};
    std::uint32_t call_pc{};
    std::uint32_t target{};
    std::uint64_t serial{};
    std::uint64_t entry_serial{};
    std::array<std::uint32_t, kMemberEntryRegisters.size()> entry_gpr{};
    std::uint32_t entry_ctr{};
    bool register_snapshot{};
    bool host_boundary{};
    bool cached{};
    bool completed{};
};

// The context is read-only. This helper has no guest-memory or service
// interface, so it cannot add architectural loads or scheduling work.
template <typename Context>
void capture_member_entry_registers(
    MainFrameDynamicCallRecord& record, const Context& context) noexcept {
    for (std::size_t index = 0u; index < kMemberEntryRegisters.size(); ++index) {
        record.entry_gpr[index] = context.gpr[kMemberEntryRegisters[index]];
    }
    record.entry_ctr = context.ctr;
    record.register_snapshot = true;
}

// A return keeps its caller-supplied entry serial even if that entry has been
// overwritten. A transfer produces no return; no continuation is fabricated.
template <std::size_t Capacity>
std::uint64_t append_main_frame_dynamic_record(
    std::array<MainFrameDynamicCallRecord, Capacity>& records,
    std::size_t& count,
    MainFrameDynamicCallRecord record) noexcept {
    static_assert(Capacity > 0u);
    record.serial = count + 1u;
    if (!record.completed) {
        record.entry_serial = record.serial;
    }
    records[count % Capacity] = record;
    ++count;
    return record.serial;
}

}  // namespace galaxy::diagnostics
