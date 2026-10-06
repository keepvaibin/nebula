#pragma once

#include "galaxy/native_api.h"

namespace galaxy {

// Diagnostic-only checkpoint timing for the three closure-profile shards.
// An F-prefixed pseudo callee cannot name RMGE01 guest code, keeping these
// samples distinct from the real child edge at the same call PC. A resumed
// checkpoint is timed as a separate architectural invocation.
inline constexpr std::uint32_t call_return_checkpoint_profile_key(
    std::uint32_t return_pc) {
    return 0xF0000000u | (return_pc & 0x0FFFFFFFu);
}

// The shared profile report is capped to its highest-cycle records. Add a reversible rank-only bias to each pseudo-edge sample so a
// short checkpoint cannot disappear behind hundreds of real child edges.
// Consumers recover exact measured cycles as total - calls * this value.
inline constexpr std::uint64_t kCallReturnCheckpointProfileRankBias = 1ull << 48;

inline void crcp(
    const NativeServicesV1* services,
    std::uint32_t call_pc,
    std::uint32_t return_pc,
    PpcContext* context,
    GuestMemoryV1* memory) {
    if (!profile_generated_direct_edges_active(services)) {
        call_return_checkpoint(
            services, call_pc, return_pc, context, memory);
        return;
    }
    const std::uint64_t start_cycles = direct_edge_profile_ticks();
    try {
        call_return_checkpoint(
            services, call_pc, return_pc, context, memory);
    } catch (...) {
        report_direct_edge_profile(
            services,
            call_pc,
            call_return_checkpoint_profile_key(return_pc),
            kCallReturnCheckpointProfileRankBias +
                direct_edge_profile_ticks() - start_cycles);
        throw;
    }
    report_direct_edge_profile(
        services,
        call_pc,
        call_return_checkpoint_profile_key(return_pc),
        kCallReturnCheckpointProfileRankBias +
            direct_edge_profile_ticks() - start_cycles);
}

}  // namespace galaxy
