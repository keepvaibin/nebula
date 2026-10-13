// SPDX-License-Identifier: GPL-3.0-only
// Module checkpoint gate (translator --checkpoint-globals): for every service
// table state, the snapshot helpers (*_due_g, transfer_pending_g) refreshed
// with refresh_module_checkpoint_gate decide exactly as the service-table
// helpers do: the same counted value, the same counter advance. An empty
// snapshot always reports kBranchCheckpointNotCounted, which sends a module
// on a runtime without snapshot support to the complete policy.
//
// Second part, the runtime's wiring: a GateController whose publish observer
// refreshes the snapshot. After every controller transition (bind, activate,
// full and inline scopes, exchange_chain, unbind) plus the adaptive-flag
// toggle the runtime refreshes by hand, the snapshot helpers decide exactly
// as the service-table helpers on an identical table driven by a second
// controller without an observer.

#define GALAXY_MODULE_CHECKPOINT_GLOBALS 1
#include "galaxy/checkpoint_gate.h"
#include "galaxy/native_api.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <optional>

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

using galaxy::checkpoint::GateController;
using galaxy::checkpoint::PointerView;
using galaxy::checkpoint::RequestedMode;

std::uint64_t g_observed_publications = 0u;
bool g_observed_half_published = false;

void refresh_from_world(void* user) noexcept {
    auto* world = static_cast<World*>(user);
    // Both slots hold their new values before the observer runs.
    g_observed_half_published |= (world->services.checkpoint_counter == nullptr) !=
        (world->services.checkpoint_next_slow == nullptr);
    galaxy::refresh_module_checkpoint_gate(&galaxy::g_module_checkpoint_gate, &world->services);
    ++g_observed_publications;
}

PointerView slots(World& world) {
    return PointerView{
        &world.services.checkpoint_counter, &world.services.checkpoint_next_slow};
}

// Every decision of both tables at one controller state. The service-table
// helpers run on `reference`, the snapshot helpers on `snapshot`; both
// counters advance in step.
void compare_decisions(World& reference, World& snapshot, unsigned step) {
    constexpr unsigned kScenario = 1000u;
    expect(galaxy::branch_checkpoint_due(&reference.services) ==
            galaxy::branch_checkpoint_due_g(&snapshot.services),
        kScenario, step, "controller: branch decision");
    expect(galaxy::branch_checkpoint_due_budgeted(&reference.services) ==
            galaxy::branch_checkpoint_due_budgeted_g(&snapshot.services),
        kScenario, step, "controller: budgeted branch decision");
    expect(galaxy::call_return_checkpoint_due(&reference.services) ==
            galaxy::call_return_checkpoint_due_g(&snapshot.services),
        kScenario, step, "controller: call-return decision");
    expect(galaxy::transfer_pending(&reference.services) ==
            galaxy::transfer_pending_g(&snapshot.services),
        kScenario, step, "controller: transfer test");
    expect(reference.counter == snapshot.counter, kScenario, step,
        "controller: same counter advance");
}

void gate_controller_observer_keeps_the_snapshot_current() {
    galaxy::refresh_module_checkpoint_gate(&galaxy::g_module_checkpoint_gate, nullptr);
    World reference;
    World snapshot;
    for (World* world : {&reference, &snapshot}) {
        // Complete table, no published gate yet: the controller owns the slots.
        world->configure(galaxy::kNativeServiceFlagAdaptiveCallReturnCheckpoints, 3u, 0u);
        world->watermark = 1'000'000u;
    }
    GateController reference_gate(RequestedMode::Inline);
    GateController snapshot_gate(RequestedMode::Inline);
    reference_gate.bind(slots(reference), &reference.counter, &reference.watermark);
    snapshot_gate.bind(slots(snapshot), &snapshot.counter, &snapshot.watermark);
    // The runtime installs the observer after module init and refreshes once.
    snapshot_gate.set_publish_observer(&refresh_from_world, &snapshot);
    refresh_from_world(&snapshot);
    std::uint64_t manual_refreshes = 1u;
    unsigned step = 0u;
    compare_decisions(reference, snapshot, step++);

    reference_gate.activate();
    snapshot_gate.activate();
    compare_decisions(reference, snapshot, step++);
    expect(galaxy::g_module_checkpoint_gate.branch_counter == &snapshot.counter, 1000u, step,
        "activation publishes the snapshot counter");
    {
        auto reference_full = reference_gate.full_scope();
        auto snapshot_full = snapshot_gate.full_scope();
        compare_decisions(reference, snapshot, step++);
        expect(galaxy::g_module_checkpoint_gate.branch_counter == nullptr, 1000u, step,
            "a full scope withdraws the snapshot counter");
        {
            auto reference_nested = reference_gate.full_scope();
            auto snapshot_nested = snapshot_gate.full_scope();
            compare_decisions(reference, snapshot, step++);
            {
                auto reference_inline = reference_gate.inline_scope();
                auto snapshot_inline = snapshot_gate.inline_scope();
                compare_decisions(reference, snapshot, step++);
                expect(galaxy::g_module_checkpoint_gate.branch_counter == &snapshot.counter,
                    1000u, step, "an inline scope republishes inside full scopes");
            }
            compare_decisions(reference, snapshot, step++);
        }
        compare_decisions(reference, snapshot, step++);
    }
    compare_decisions(reference, snapshot, step++);

    // Fiber switch to a chain parked inside a host callback, and back.
    GateController::ChainDepths reference_parked{};
    GateController::ChainDepths snapshot_parked{};
    reference_gate.exchange_chain(reference_parked, GateController::ChainDepths{1u, 0u});
    snapshot_gate.exchange_chain(snapshot_parked, GateController::ChainDepths{1u, 0u});
    compare_decisions(reference, snapshot, step++);
    GateController::ChainDepths reference_resumed{};
    GateController::ChainDepths snapshot_resumed{};
    reference_gate.exchange_chain(reference_resumed, reference_parked);
    snapshot_gate.exchange_chain(snapshot_resumed, snapshot_parked);
    compare_decisions(reference, snapshot, step++);

    // The checkpoint callback toggles the adaptive bit inside a full scope
    // and refreshes by hand; the scope exit publishes again.
    std::optional<GateController::FullScope> reference_callback(reference_gate.full_scope());
    std::optional<GateController::FullScope> snapshot_callback(snapshot_gate.full_scope());
    reference.services.runtime_flags &= ~galaxy::kNativeServiceFlagAdaptiveCallReturnCheckpoints;
    snapshot.services.runtime_flags &= ~galaxy::kNativeServiceFlagAdaptiveCallReturnCheckpoints;
    refresh_from_world(&snapshot);
    ++manual_refreshes;
    compare_decisions(reference, snapshot, step++);
    reference_callback.reset();
    snapshot_callback.reset();
    compare_decisions(reference, snapshot, step++);
    expect(galaxy::g_module_checkpoint_gate.call_return_counter == nullptr &&
            galaxy::g_module_checkpoint_gate.call_return_quiet == 1u,
        1000u, step, "non-adaptive call returns finish on an empty mask");

    // Events and the watermark are read through the snapshot's pointers.
    reference.mask.store(1u);
    snapshot.mask.store(1u);
    compare_decisions(reference, snapshot, step++);
    reference.mask.store(galaxy::kPendingEventTransferBit);
    snapshot.mask.store(galaxy::kPendingEventTransferBit);
    compare_decisions(reference, snapshot, step++);
    reference.mask.store(0u);
    snapshot.mask.store(0u);
    reference.watermark = reference.counter + 1u;
    snapshot.watermark = snapshot.counter + 1u;
    compare_decisions(reference, snapshot, step++);

    reference_gate.unbind();
    snapshot_gate.unbind();
    compare_decisions(reference, snapshot, step++);
    expect(galaxy::g_module_checkpoint_gate.branch_counter == nullptr, 1000u, step,
        "unbind withdraws the snapshot counter");

    // Every slot write reached the observer, and only those.
    const auto& telemetry = snapshot_gate.telemetry();
    expect(g_observed_publications ==
            manual_refreshes + telemetry.transitions_to_full + telemetry.transitions_to_inline,
        1000u, step, "the observer runs once per publication");
    expect(!g_observed_half_published, 1000u, step,
        "the observer never sees one slot without the other");

    // Teardown: observer removed, snapshot emptied before the table goes away.
    snapshot_gate.set_publish_observer(nullptr, nullptr);
    galaxy::refresh_module_checkpoint_gate(&galaxy::g_module_checkpoint_gate, nullptr);
    const std::uint64_t before = g_observed_publications;
    snapshot_gate.bind(slots(snapshot), &snapshot.counter, &snapshot.watermark);
    snapshot_gate.unbind();
    expect(g_observed_publications == before, 1000u, step, "a removed observer is not called");
    expect(galaxy::call_return_checkpoint_due_g(nullptr) == galaxy::kBranchCheckpointNotCounted,
        1000u, step, "the emptied snapshot is conservative");
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
    gate_controller_observer_keeps_the_snapshot_current();
    if (failures != 0) {
        std::fprintf(stderr, "module checkpoint gate: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("module checkpoint gate: %u scenarios equivalent, %llu observed publications\n",
        scenario, static_cast<unsigned long long>(g_observed_publications));
    return scenario == 576u ? 0 : 2;
}
