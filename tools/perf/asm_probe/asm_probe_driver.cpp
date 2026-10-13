// SPDX-License-Identifier: GPL-3.0-only
// Driver for dynamic instruction counts of the probe module (Linux only; see
// tools/perf/asm_probe.py --dynamic). Links the probe shards and module.cpp,
// prepares the guest state the corpus assumes (asm_probe.rs DRIVER_STATE) and
// calls the probes named on the command line N times each, so
// `valgrind --tool=callgrind` reports their self cost. The service table is
// the published-inline-gate, adaptive-call-return, no-pending-event state of
// gameplay; the direct dispatch table maps every probe address.
#include "functions.h"

#include <sys/mman.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

extern "C" galaxy::NativeGameFunction galaxy_lookup_function(std::uint32_t guest_address);
extern "C" std::size_t galaxy_module_function_records(const galaxy::FunctionRecordV1** records);
#if defined(GALAXY_MODULE_CHECKPOINT_GLOBALS) && GALAXY_MODULE_CHECKPOINT_GLOBALS
extern "C" galaxy::ModuleCheckpointGateV1* galaxy_module_checkpoint_gate();
#endif

namespace {

std::byte* g_base = nullptr;

void put32(std::uint32_t address, std::uint32_t value) {
    const std::uint32_t raw = galaxy::byte_swap_u32(value);
    std::memcpy(g_base + address, &raw, 4u);
}

void checkpoint(void*, std::uint32_t, galaxy::PpcContext*, galaxy::GuestMemoryV1*) {}

void call_guest_cached(void*, std::uint32_t address, std::uint32_t*, galaxy::NativeGameFunction*,
                       galaxy::PpcContext* context, galaxy::GuestMemoryV1* memory);
const galaxy::NativeServicesV1* g_services = nullptr;

void call_guest_cached(void*, std::uint32_t address, std::uint32_t*, galaxy::NativeGameFunction*,
                       galaxy::PpcContext* context, galaxy::GuestMemoryV1* memory) {
    if (const auto function = galaxy_lookup_function(address)) {
        context->pc = address;
        function(context, memory, g_services);
    }
}

void call_guest(void* user, std::uint32_t address, galaxy::PpcContext* context,
                galaxy::GuestMemoryV1* memory) {
    call_guest_cached(user, address, nullptr, nullptr, context, memory);
}

[[noreturn]] void fatal(void*, std::uint32_t pc, const char* message) {
    std::fprintf(stderr, "fatal at %08X: %s\n", pc, message);
    std::abort();
}

std::uint32_t g_first_probe = 0u;

// Guest data every call starts from (probes store into it): a vtable pointer
// at 0x80100000 whose +8 word is the first probe (callee_leaf), and binary32
// 1.5 values at 0x80100200.
void reset_data() {
    put32(0x80100000u, 0x80100100u);
    put32(0x80100108u, g_first_probe);
    for (std::uint32_t i = 1; i < 64; ++i) put32(0x80100000u + 0x200u + i * 4u, 0x3FC00000u);
    // Float operands of the float_loop and paired_mtx probes (r3 + 0x20..0xFC).
    for (std::uint32_t i = 8; i < 64; ++i) put32(0x80100000u + i * 4u, 0x3FC00000u);
}

void reset(galaxy::PpcContext& context, std::uint32_t r4) {
    reset_data();
    context.gpr[1] = 0x80400000u;
    context.gpr[3] = 0x80100000u;
    context.gpr[4] = r4;
    for (std::uint32_t reg = 5; reg <= 12; ++reg) context.gpr[reg] = reg - 4u;
    const double values[] = {0.0, 1.5, 2.5, 3.5, 3.5, 3.5, 3.5, 3.5, 3.5};
    for (std::uint32_t reg = 1; reg <= 8; ++reg) {
        context.fpr_bits[reg] = std::bit_cast<std::uint64_t>(values[reg]);
        context.ps1_bits[reg] = context.fpr_bits[reg];
    }
    context.cr = 0u;
    context.xer = 0u;
}

}  // namespace

int main(int argc, char** argv) {
    g_base = static_cast<std::byte*>(mmap(nullptr, 1ull << 32, PROT_READ | PROT_WRITE,
                                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0));
    galaxy::GuestMemoryV1 memory{};
    memory.flat_guest_base = g_base;
    memory.flat_guest_read_base = g_base;
    for (std::uint32_t alias : {0x0u, 0x8u, 0xCu}) memory.fast_regions[alias] = {0x01800000u, g_base + (std::uint64_t{alias} << 28)};
    for (std::uint32_t alias : {0x1u, 0x9u, 0xDu}) memory.fast_regions[alias] = {0x04000000u, g_base + (std::uint64_t{alias} << 28)};

    const galaxy::FunctionRecordV1* records = nullptr;
    const std::size_t count = galaxy_module_function_records(&records);
    std::uint32_t low = ~0u, high = 0u;
    std::map<std::string, std::uint32_t> by_name;
    for (std::size_t i = 0; i < count; ++i) {
        low = std::min(low, records[i].address);
        high = std::max(high, records[i].address);
    }
    std::vector<galaxy::NativeGameFunction> table((high - low) / 4u + 1u, nullptr);
    for (std::size_t i = 0; i < count; ++i) table[(records[i].address - low) / 4u] = records[i].function;

    g_first_probe = low;  // the corpus places callee_leaf first

    static std::atomic_uint32_t mask{0};
    static std::atomic_uint32_t suspended{0};
    static std::uint64_t counter = 0;
    static std::uint64_t next_slow = ~0ull;
    galaxy::NativeServicesV1 services{};
    services.fatal = &fatal;
    services.call_guest = &call_guest;
    services.call_guest_cached = &call_guest_cached;
    services.branch_checkpoint = &checkpoint;
    services.pending_event_mask = &mask;
    services.checkpoint_counter = &counter;
    services.checkpoint_next_slow = &next_slow;
    services.runtime_flags = galaxy::kNativeServiceFlagAdaptiveCallReturnCheckpoints;
    services.direct_dispatch = table.data();
    services.direct_dispatch_base = low;
    services.direct_dispatch_count = static_cast<std::uint32_t>(table.size() * 4u);
    services.direct_dispatch_suspended = &suspended;
    g_services = &services;
#if defined(GALAXY_MODULE_CHECKPOINT_GLOBALS) && GALAXY_MODULE_CHECKPOINT_GLOBALS
    galaxy::refresh_module_checkpoint_gate(galaxy_module_checkpoint_gate(), &services);
#endif

    galaxy::PpcContext context{};
    context.hid2 = 0xA0000000u;
    const int iterations = 1000;
    for (int arg = 1; arg < argc; ++arg) {
        const std::uint32_t address = static_cast<std::uint32_t>(std::strtoul(argv[arg], nullptr, 16));
        const auto function = galaxy_lookup_function(address);
        if (function == nullptr) {
            std::fprintf(stderr, "no probe at %s\n", argv[arg]);
            return 1;
        }
        for (int n = 0; n < iterations; ++n) {
            reset(context, 16u);
            context.pc = address;
            function(&context, &memory, &services);
        }
    }
    std::printf("ok counter=%llu\n", static_cast<unsigned long long>(counter));
    return 0;
}
