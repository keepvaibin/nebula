#include "galaxy/native_api.h"

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace {
struct Fault {};
struct Transfer {};
struct Probe {
    unsigned callbacks{}, faults{};
    std::uint32_t guest_pc{};
    bool transfer{};
};
void callback(void* user, std::uint32_t pc, galaxy::PpcContext*, galaxy::GuestMemoryV1*) {
    auto& probe = *static_cast<Probe*>(user);
    ++probe.callbacks;
    probe.guest_pc = pc;
    if (probe.transfer) throw Transfer{};
}
void fatal(void* user, std::uint32_t pc, const char*) {
    auto& probe = *static_cast<Probe*>(user);
    ++probe.faults;
    probe.guest_pc = pc;
    throw Fault{};
}
} // namespace

int main() {
    // Each record includes observable callbacks, faults, transfers, guest PC,
    // and counted boundaries. The same public fixture can be built against a
    // preserved header to compare the entire record stream, not just a checksum.
    unsigned case_id = 0u;
    for (const auto flags : {0u, galaxy::kNativeServiceFlagAdaptiveCallReturnCheckpoints,
            galaxy::kNativeServiceFlagTraceWpadReadCopies,
            galaxy::kNativeServiceFlagAdaptiveCallReturnCheckpoints |
                galaxy::kNativeServiceFlagTraceWpadReadCopies}) {
        for (const auto pc : {0x80001234u, 0x804D945Cu, 0x803AB2B0u}) {
            for (unsigned pointers = 0u; pointers != 16u; ++pointers) {
                for (unsigned events = 0u; events != 2u; ++events) {
                    for (unsigned transfers = 0u; transfers != 2u; ++transfers) {
                        Probe probe{};
                        probe.transfer = transfers != 0u;
                        std::atomic_uint32_t mask{events};
                        std::uint64_t counter = 0u;
                        std::uint64_t watermark = 3u;
                        galaxy::PpcContext context{};
                        context.pc = 0xBAD00000u;
                        galaxy::NativeServicesV1 services{};
                        services.user = &probe;
                        services.fatal = &fatal;
                        services.runtime_flags = flags;
                        services.branch_checkpoint = (pointers & 1u) ? &callback : nullptr;
                        services.pending_event_mask = (pointers & 2u) ? &mask : nullptr;
                        services.checkpoint_counter = (pointers & 4u) ? &counter : nullptr;
                        services.checkpoint_next_slow = (pointers & 8u) ? &watermark : nullptr;
                        unsigned faults = 0u, caught_transfers = 0u;
                        for (unsigned step = 0u; step != 5u; ++step) {
                            // Change publication and adaptive policy between calls.
                            // A callee opening a later event/window must not be cached.
                            if (step == 2u) mask.store(events ^ 1u, std::memory_order_release);
                            if (step == 3u) services.runtime_flags ^= galaxy::kNativeServiceFlagAdaptiveCallReturnCheckpoints;
                            if (step == 4u) watermark = counter + 1u;
                            try {
                                galaxy::call_return_checkpoint(&services, pc, pc + 4u, &context, nullptr);
                            } catch (const Fault&) {
                                ++faults;
                            } catch (const Transfer&) {
                                ++caught_transfers;
                            }
                            std::printf("%u %u %u %u %u %u %08X %08X %llu\n", case_id, step,
                                probe.callbacks, probe.faults, faults, caught_transfers,
                                probe.guest_pc, context.pc, static_cast<unsigned long long>(counter));
                        }
                        // No callback service or event-mask capability must always
                        // fail closed, including an otherwise empty ordinary return.
                        if ((pointers & 3u) != 3u && (faults != 5u || probe.callbacks != 0u)) return 1;
                        if (probe.faults != faults) return 2;
                        ++case_id;
                    }
                }
            }
        }
    }
    std::fprintf(stderr, "checkpoint policy: %u scenarios / %u records\n", case_id, case_id * 5u);
    return case_id == 768u ? 0 : 3;
}
