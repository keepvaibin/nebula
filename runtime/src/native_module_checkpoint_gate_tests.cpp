// SPDX-License-Identifier: GPL-3.0-only
// Module checkpoint gate (translator --checkpoint-globals): for every service
// table state, the snapshot helpers (*_due_g, transfer_pending_g) refreshed
// with refresh_module_checkpoint_gate decide exactly as the service-table
// helpers do: the same counted value, the same counter advance. An empty
// snapshot always reports kBranchCheckpointNotCounted, which sends a module
// on a runtime without snapshot support to the complete policy.

#define GALAXY_MODULE_CHECKPOINT_GLOBALS 1
#include "galaxy/native_api.h"

#include <atomic>
#include <cstdint>
#include <cstdio>

galaxy::ModuleCheckpointGateV1 galaxy::g_module_checkpoint_gate{};

namespace {

void callback(void*, std::uint32_t, galaxy::PpcContext*, galaxy::GuestMemoryV1*) {}

struct World {
    std::atomic_uint32_t mask{};
    std::uint64_t counter{};
    std::uint64_t watermark{};
    galaxy::NativeServicesV1 services{};

    void configure(std::uint32_t flags, unsigned pointers, std::uint32_t events) {
        mask.store(events, std::memory_order_relaxed);
        counter = 0u;
        watermark = 3u;
        services = {};
        services.runtime_flags = flags;
        services.branch_checkpoint = (pointers & 1u) ? &callback : nullptr;
        services.pending_event_mask = (pointers & 2u) ? &mask : nullptr;
        services.checkpoint_counter = (pointers & 4u) ? &counter : nullptr;
        services.checkpoint_next_slow = (pointers & 8u) ? &watermark : nullptr;
    }
};

int failures = 0;

void expect(bool condition, unsigned scenario, unsigned step, const char* what) {
    if (!condition) {
        ++failures;
        if (failures <= 20) {
            std::fprintf(stderr, "FAIL scenario %u step %u: %s\n", scenario, step, what);
        }
    }
}

}  // namespace

int main() {
    // An empty snapshot is the conservative path.
    galaxy::refresh_module_checkpoint_gate(&galaxy::g_module_checkpoint_gate, nullptr);
    if (galaxy::branch_checkpoint_due_g(nullptr) != galaxy::kBranchCheckpointNotCounted ||
        galaxy::branch_checkpoint_due_budgeted_g(nullptr) != galaxy::kBranchCheckpointNotCounted ||
        galaxy::call_return_checkpoint_due_g(nullptr) != galaxy::kBranchCheckpointNotCounted ||
        galaxy::transfer_pending_g(nullptr)) {
        std::fprintf(stderr, "FAIL: empty snapshot is not conservative\n");
        return 1;
    }

    unsigned scenario = 0u;
    World reference;
    World snapshot;
    for (const std::uint32_t flags : {0u, galaxy::kNativeServiceFlagAdaptiveCallReturnCheckpoints,
             galaxy::kNativeServiceFlagTraceWpadReadCopies,
             galaxy::kNativeServiceFlagAdaptiveCallReturnCheckpoints |
                 galaxy::kNativeServiceFlagTraceWpadReadCopies}) {
        for (unsigned pointers = 0u; pointers != 16u; ++pointers) {
            for (const std::uint32_t events : {0u, 1u, galaxy::kPendingEventTransferBit}) {
                for (unsigned kind = 0u; kind != 3u; ++kind) {
                    reference.configure(flags, pointers, events);
                    snapshot.configure(flags, pointers, events);
                    galaxy::refresh_module_checkpoint_gate(
                        &galaxy::g_module_checkpoint_gate, &snapshot.services);
                    for (unsigned step = 0u; step != 8u; ++step) {
                        // The runtime refreshes after each change below.
                        if (step == 2u) {
                            reference.mask.store(events ^ 1u);
                            snapshot.mask.store(events ^ 1u);
                        }
                        if (step == 3u) {
                            reference.services.runtime_flags ^=
                                galaxy::kNativeServiceFlagAdaptiveCallReturnCheckpoints;
                            snapshot.services.runtime_flags ^=
                                galaxy::kNativeServiceFlagAdaptiveCallReturnCheckpoints;
                        }
                        if (step == 4u) {
                            reference.watermark = reference.counter + 1u;
                            snapshot.watermark = snapshot.counter + 1u;
                        }
                        if (step == 5u) {
                            // Full scope: the gate pointers are withdrawn.
                            reference.services.checkpoint_counter = nullptr;
                            reference.services.checkpoint_next_slow = nullptr;
                            snapshot.services.checkpoint_counter = nullptr;
                            snapshot.services.checkpoint_next_slow = nullptr;
                        }
                        if (step == 6u && (pointers & 12u) == 12u) {
                            // Scope exit: republished.
                            reference.services.checkpoint_counter = &reference.counter;
                            reference.services.checkpoint_next_slow = &reference.watermark;
                            snapshot.services.checkpoint_counter = &snapshot.counter;
                            snapshot.services.checkpoint_next_slow = &snapshot.watermark;
                        }
                        galaxy::refresh_module_checkpoint_gate(
                            &galaxy::g_module_checkpoint_gate, &snapshot.services);
                        std::uint64_t expected = 0u;
                        std::uint64_t actual = 0u;
                        switch (kind) {
                        case 0u:
                            expected = galaxy::branch_checkpoint_due(&reference.services);
                            actual = galaxy::branch_checkpoint_due_g(&snapshot.services);
                            break;
                        case 1u:
                            expected = galaxy::branch_checkpoint_due_budgeted(&reference.services);
                            actual = galaxy::branch_checkpoint_due_budgeted_g(&snapshot.services);
                            break;
                        default:
                            expected = galaxy::call_return_checkpoint_due(&reference.services);
                            actual = galaxy::call_return_checkpoint_due_g(&snapshot.services);
                            break;
                        }
                        expect(actual == expected, scenario, step, "same decision");
                        expect(snapshot.counter == reference.counter, scenario, step,
                            "same counter advance");
                        expect(galaxy::transfer_pending_g(&snapshot.services) ==
                                galaxy::transfer_pending(&reference.services),
                            scenario, step, "same transfer test");
                    }
                    ++scenario;
                }
            }
        }
    }
    galaxy::refresh_module_checkpoint_gate(&galaxy::g_module_checkpoint_gate, nullptr);
    if (failures != 0) {
        std::fprintf(stderr, "module checkpoint gate: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("module checkpoint gate: %u scenarios equivalent\n", scenario);
    return scenario == 576u ? 0 : 2;
}
