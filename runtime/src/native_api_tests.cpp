#include "galaxy/native_api.h"
#include "galaxy/crcp.h"
#include "galaxy/interrupt_entry.h"

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string_view>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

std::uint64_t fixed_time_base(void*) {
    return 0x00000001'23456789ull;
}

struct DeviceProbe {
    std::uint32_t reads{};
    std::uint32_t writes{};
};

struct SystemCallProbe {
    std::uint32_t calls{};
    std::uint32_t guest_pc{};
    std::uint32_t instruction{};
};

struct FifoWriteProbe {
    std::uint32_t writes{};
    std::uint32_t address{};
    std::uint32_t size{};
    std::array<std::byte, 128> bytes{};
};

struct NotifyProbe {
    std::uint32_t writes{};
    std::uint32_t address{};
    std::uint32_t size{};
};

struct LogProbe {
    std::uint32_t calls{};
    std::array<char, 64> message{};
};

struct DirectEdgeProfileProbe {
    std::uint32_t calls{};
    std::uint32_t caller_pc{};
    std::uint32_t callee_pc{};
    std::uint64_t cycles{};
};

struct CachedCallProbe {
    std::uint32_t calls{};
    std::uint32_t guest_address{};
    std::array<std::uint32_t, 8> guest_addresses{};
    std::array<std::uintptr_t, 8> cache_address_slots{};
    std::array<std::uintptr_t, 8> cache_function_slots{};
    std::uint32_t gpr3{};
    std::uint32_t gpr4{};
    std::uint32_t gpr5{};
    std::uint32_t gpr6{};
    std::uint32_t gpr11{};
    std::uint32_t gpr12{};
    std::uint32_t ctr{};
    std::uint32_t lr{};
    std::uint32_t branch_checkpoints{};
    std::uint32_t branch_pc{};
    std::uint32_t branch_resume_pc{};
    std::uint32_t branch_gpr30{};
    std::uint32_t branch_gpr31{};
    std::uint32_t return_gpr3{0xC0DEC0DEu};
    bool cache_slots_present{};
    bool direct_cached_function_invoked{};
};

struct CachedEdgeProfileProbe {
    CachedCallProbe cached{};
    DirectEdgeProfileProbe edge{};
};

struct DirectResolvedCallProbe {
    const galaxy::NativeServicesV1* services{};
    std::uint32_t service_calls{};
    std::uint32_t native_calls{};
    std::uint32_t guest_address{};
    std::uint32_t caller_visible_pc{};
};

struct SimulatedGuestTransfer final {};
struct SimulatedExecutionFault final {};

struct FpuUnavailableProbe {
    std::uint32_t calls{};
    std::uint32_t guest_pc{};
    galaxy::PpcContext* context{};
    galaxy::GuestMemoryV1* memory{};
    bool enable_fp{true};
    std::uint32_t resume_pc_delta{};
};

struct RestartabilityProbe {
    std::uint32_t guest_calls{};
    std::uint32_t checkpoints{};
    std::uint32_t guest_address{};
    std::uint32_t checkpoint_pc{};
};

struct DspRunningProbe {
    std::uint32_t calls{};
    std::uint32_t guest_pc{};
    std::uint32_t flag_address{};
};

struct DecrementerWriteProbe {
    std::uint64_t ticks{};
    std::uint32_t calls{};
    std::uint32_t guest_pc{};
    std::uint32_t resume_pc{};
    std::uint64_t write_ticks{};
    std::uint32_t previous_value{};
    std::uint32_t observed_context_pc{};
    galaxy::PpcContext* context{};
    galaxy::GuestMemoryV1* memory{};
};

std::uint64_t mutable_time_base(void* user) {
    return static_cast<DecrementerWriteProbe*>(user)->ticks;
}

void capture_decrementer_write(
    void* user,
    std::uint32_t guest_pc,
    std::uint32_t resume_pc,
    std::uint64_t write_ticks,
    std::uint32_t previous_value,
    galaxy::PpcContext* context,
    galaxy::GuestMemoryV1* memory) {
    auto& probe = *static_cast<DecrementerWriteProbe*>(user);
    ++probe.calls;
    probe.guest_pc = guest_pc;
    probe.resume_pc = resume_pc;
    probe.write_ticks = write_ticks;
    probe.previous_value = previous_value;
    probe.observed_context_pc = context->pc;
    probe.context = context;
    probe.memory = memory;
}

void simulate_fpu_unavailable(
    void* user,
    std::uint32_t guest_pc,
    galaxy::PpcContext* context,
    galaxy::GuestMemoryV1* memory) {
    auto& probe = *static_cast<FpuUnavailableProbe*>(user);
    ++probe.calls;
    probe.guest_pc = guest_pc;
    probe.context = context;
    probe.memory = memory;
    if (probe.enable_fp) {
        context->msr |= galaxy::kMsrFloatingPointAvailable;
    }
    context->pc = guest_pc + probe.resume_pc_delta;
}

[[noreturn]] void throw_execution_fault(
    void*,
    std::uint32_t,
    const char*) {
    throw SimulatedExecutionFault{};
}

bool probe_read(
    void* user,
    std::uint32_t,
    std::uint32_t,
    std::byte*) {
    ++static_cast<DeviceProbe*>(user)->reads;
    return false;
}

bool probe_write(
    void* user,
    std::uint32_t,
    std::uint32_t,
    const std::byte*) {
    ++static_cast<DeviceProbe*>(user)->writes;
    return false;
}

void probe_system_call(
    void* user,
    std::uint32_t guest_pc,
    std::uint32_t instruction,
    galaxy::PpcContext*,
    galaxy::GuestMemoryV1*) {
    auto& probe = *static_cast<SystemCallProbe*>(user);
    ++probe.calls;
    probe.guest_pc = guest_pc;
    probe.instruction = instruction;
}

bool capture_fifo_write(
    void* user,
    std::uint32_t address,
    std::uint32_t size,
    const std::byte* input) {
    const std::uint32_t hi = address & 0xFF000000u;
    if (!((hi == 0x0C000000u || hi == 0xCC000000u) &&
          (address & 0x00FFF000u) == 0x00008000u)) {
        return false;
    }
    auto& probe = *static_cast<FifoWriteProbe*>(user);
    ++probe.writes;
    if (probe.address == 0u) {
        probe.address = address;
    }
    const bool fits = input != nullptr && probe.size + size <= probe.bytes.size();
    if (fits) {
        std::memcpy(probe.bytes.data() + probe.size, input, size);
        probe.size += size;
    }
    return fits;
}

void capture_notify_write(
    void* user,
    std::uint32_t address,
    std::uint32_t size) {
    auto& probe = *static_cast<NotifyProbe*>(user);
    ++probe.writes;
    probe.address = address;
    probe.size = size;
}

void capture_log_message(
    void* user,
    galaxy::LogLevelV1,
    const char* message) {
    auto& probe = *static_cast<LogProbe*>(user);
    ++probe.calls;
    std::snprintf(
        probe.message.data(), probe.message.size(), "%s",
        message != nullptr ? message : "");
}

void capture_direct_edge_profile(
    void* user,
    std::uint32_t caller_pc,
    std::uint32_t callee_pc,
    std::uint64_t cycles) {
    auto& probe = *static_cast<DirectEdgeProfileProbe*>(user);
    ++probe.calls;
    probe.caller_pc = caller_pc;
    probe.callee_pc = callee_pc;
    probe.cycles = cycles;
}

void capture_cached_call(
    void* user,
    std::uint32_t guest_address,
    std::uint32_t* cached_address,
    galaxy::NativeGameFunction* cached_function,
    galaxy::PpcContext* context,
    galaxy::GuestMemoryV1*) {
    auto& probe = *static_cast<CachedCallProbe*>(user);
    ++probe.calls;
    probe.guest_address = guest_address;
    if (probe.calls <= probe.guest_addresses.size()) {
        const std::size_t slot = probe.calls - 1u;
        probe.guest_addresses[slot] = guest_address;
        probe.cache_address_slots[slot] =
            reinterpret_cast<std::uintptr_t>(cached_address);
        probe.cache_function_slots[slot] =
            reinterpret_cast<std::uintptr_t>(cached_function);
    }
    probe.gpr3 = context->gpr[3];
    probe.gpr4 = context->gpr[4];
    probe.gpr5 = context->gpr[5];
    probe.gpr6 = context->gpr[6];
    probe.gpr11 = context->gpr[11];
    probe.gpr12 = context->gpr[12];
    probe.ctr = context->ctr;
    probe.lr = context->lr;
    probe.cache_slots_present = cached_address != nullptr && cached_function != nullptr;
    if (cached_address != nullptr) {
        *cached_address = guest_address;
    }
    context->pc = guest_address;
    context->gpr[3] = probe.return_gpr3;
}

void capture_direct_cached_function(
    galaxy::PpcContext*,
    galaxy::GuestMemoryV1*,
    const galaxy::NativeServicesV1* services) {
    auto& probe = *static_cast<CachedCallProbe*>(services->user);
    probe.direct_cached_function_invoked = true;
}

void capture_direct_resolved_function(
    galaxy::PpcContext* context,
    galaxy::GuestMemoryV1*,
    const galaxy::NativeServicesV1* services) {
    auto& probe = *static_cast<DirectResolvedCallProbe*>(services->user);
    ++probe.native_calls;
    probe.caller_visible_pc = context->pc;
}

void capture_direct_resolved_call(
    void* user,
    std::uint32_t guest_address,
    galaxy::NativeGameFunction function,
    galaxy::PpcContext* context,
    galaxy::GuestMemoryV1* memory) {
    auto& probe = *static_cast<DirectResolvedCallProbe*>(user);
    ++probe.service_calls;
    probe.guest_address = guest_address;
    context->pc = guest_address;
    function(context, memory, probe.services);
}

void capture_cached_edge_call(
    void* user,
    std::uint32_t guest_address,
    std::uint32_t* cached_address,
    galaxy::NativeGameFunction* cached_function,
    galaxy::PpcContext* context,
    galaxy::GuestMemoryV1* memory) {
    auto& probe = *static_cast<CachedEdgeProfileProbe*>(user);
    capture_cached_call(
        &probe.cached,
        guest_address,
        cached_address,
        cached_function,
        context,
        memory);
}

void capture_cached_edge_profile(
    void* user,
    std::uint32_t caller_pc,
    std::uint32_t callee_pc,
    std::uint64_t cycles) {
    auto& probe = *static_cast<CachedEdgeProfileProbe*>(user);
    capture_direct_edge_profile(
        &probe.edge, caller_pc, callee_pc, cycles);
}

void return_from_cached_call_for_restartability(
    void* user,
    std::uint32_t guest_address,
    std::uint32_t* cached_address,
    galaxy::NativeGameFunction*,
    galaxy::PpcContext*,
    galaxy::GuestMemoryV1*) {
    auto& probe = *static_cast<RestartabilityProbe*>(user);
    ++probe.guest_calls;
    probe.guest_address = guest_address;
    if (cached_address != nullptr) {
        *cached_address = guest_address;
    }
}

void throw_guest_transfer_on_checkpoint(
    void* user,
    std::uint32_t guest_pc,
    galaxy::PpcContext*,
    galaxy::GuestMemoryV1*) {
    auto& probe = *static_cast<RestartabilityProbe*>(user);
    ++probe.checkpoints;
    probe.checkpoint_pc = guest_pc;
    throw SimulatedGuestTransfer{};
}

void capture_branch_checkpoint(
    void* user,
    std::uint32_t guest_pc,
    galaxy::PpcContext* context,
    galaxy::GuestMemoryV1*) {
    auto& probe = *static_cast<CachedCallProbe*>(user);
    ++probe.branch_checkpoints;
    probe.branch_pc = guest_pc;
    probe.branch_resume_pc = context->pc;
    probe.branch_gpr30 = context->gpr[30];
    probe.branch_gpr31 = context->gpr[31];
}

void set_dsp_running_on_checkpoint(
    void* user,
    std::uint32_t guest_pc,
    galaxy::PpcContext*,
    galaxy::GuestMemoryV1* memory) {
    auto& probe = *static_cast<DspRunningProbe*>(user);
    ++probe.calls;
    probe.guest_pc = guest_pc;
    galaxy::guest_store_u8(memory, probe.flag_address, 1u, nullptr, guest_pc);
}

void run_reference_psvec_normalize_804B6BCC(
    galaxy::PpcContext* context,
    galaxy::GuestMemoryV1* memory) {
    galaxy::psq_load(
        context,
        2u,
        memory,
        context->gpr[3],
        0u,
        false,
        true,
        nullptr,
        0x804B6BCCu);
    galaxy::psq_load(
        context,
        3u,
        memory,
        context->gpr[3] + 8u,
        0u,
        true,
        true,
        nullptr,
        0x804B6BD0u);

    const auto f32 = [](std::uint64_t bits, std::uint32_t pc) {
        return galaxy::require_single_precision_bits(bits, nullptr, pc);
    };
    const auto commit_paired_mul = [&](
        std::uint32_t target,
        std::uint32_t left,
        std::uint32_t right,
        std::uint32_t pc) {
        const galaxy::PpcFloatResult lane0 = galaxy::ppc_f32_binary(
            galaxy::PpcFloatBinaryOperation::Multiply,
            f32(context->fpr_bits[left], pc),
            f32(context->fpr_bits[right], pc),
            context->fpscr);
        const galaxy::PpcFloatResult lane1 = galaxy::ppc_f32_binary(
            galaxy::PpcFloatBinaryOperation::Multiply,
            f32(context->ps1_bits[left], pc),
            f32(context->ps1_bits[right], pc),
            context->fpscr);
        galaxy::ppc_commit_paired_result(
            context, target, lane0, lane1, false, false, nullptr, pc);
    };
    const auto commit_paired_muls0 = [&](
        std::uint32_t target,
        std::uint32_t left,
        std::uint32_t right,
        std::uint32_t pc) {
        const std::uint32_t scalar = f32(context->fpr_bits[right], pc);
        const galaxy::PpcFloatResult lane0 = galaxy::ppc_f32_binary(
            galaxy::PpcFloatBinaryOperation::Multiply,
            f32(context->fpr_bits[left], pc),
            scalar,
            context->fpscr);
        const galaxy::PpcFloatResult lane1 = galaxy::ppc_f32_binary(
            galaxy::PpcFloatBinaryOperation::Multiply,
            f32(context->ps1_bits[left], pc),
            scalar,
            context->fpscr);
        galaxy::ppc_commit_paired_result(
            context, target, lane0, lane1, false, false, nullptr, pc);
    };

    commit_paired_mul(5u, 2u, 2u, 0x804B6BD4u);
    galaxy::load_fpr_single(
        context, 0u, memory, context->gpr[2] + 0x24E8u, nullptr, 0x804B6BD8u);
    galaxy::load_fpr_single(
        context, 1u, memory, context->gpr[2] + 0x24ECu, nullptr, 0x804B6BDCu);

    {
        const galaxy::PpcFloatResult lane0 = galaxy::ppc_f32_ternary(
            galaxy::PpcFloatTernaryOperation::MultiplyAdd,
            f32(context->fpr_bits[3], 0x804B6BE0u),
            f32(context->fpr_bits[3], 0x804B6BE0u),
            f32(context->fpr_bits[5], 0x804B6BE0u),
            context->fpscr);
        const galaxy::PpcFloatResult lane1 = galaxy::ppc_f32_ternary(
            galaxy::PpcFloatTernaryOperation::MultiplyAdd,
            f32(context->ps1_bits[3], 0x804B6BE0u),
            f32(context->ps1_bits[3], 0x804B6BE0u),
            f32(context->ps1_bits[5], 0x804B6BE0u),
            context->fpscr);
        galaxy::ppc_commit_paired_result(
            context, 4u, lane0, lane1, false, false, nullptr, 0x804B6BE0u);
    }
    {
        const galaxy::PpcFloatResult lane0 = galaxy::ppc_f32_binary(
            galaxy::PpcFloatBinaryOperation::Add,
            f32(context->fpr_bits[4], 0x804B6BE4u),
            f32(context->ps1_bits[5], 0x804B6BE4u),
            context->fpscr);
        const galaxy::PpcFloatResult lane1 =
            galaxy::ppc_f32_passthrough(f32(context->ps1_bits[3], 0x804B6BE4u));
        galaxy::ppc_commit_paired_result(
            context, 4u, lane0, lane1, false, false, nullptr, 0x804B6BE4u);
    }
    galaxy::ppc_commit_scalar_result(
        context,
        5u,
        galaxy::ppc_f64_reciprocal_sqrt_estimate(
            context->fpr_bits[4], context->fpscr),
        false,
        false,
        nullptr,
        0x804B6BE8u);
    galaxy::ppc_commit_scalar_result(
        context,
        6u,
        galaxy::ppc_f64_binary_to_f32(
            galaxy::PpcFloatBinaryOperation::Multiply,
            context->fpr_bits[5],
            context->fpr_bits[5],
            context->fpscr),
        true,
        false,
        nullptr,
        0x804B6BECu);
    galaxy::ppc_commit_scalar_result(
        context,
        0u,
        galaxy::ppc_f64_binary_to_f32(
            galaxy::PpcFloatBinaryOperation::Multiply,
            context->fpr_bits[5],
            context->fpr_bits[0],
            context->fpscr),
        true,
        false,
        nullptr,
        0x804B6BF0u);
    galaxy::ppc_commit_scalar_result(
        context,
        6u,
        galaxy::ppc_f32_ternary(
            galaxy::PpcFloatTernaryOperation::NegativeMultiplySubtract,
            f32(context->fpr_bits[6], 0x804B6BF4u),
            f32(context->fpr_bits[4], 0x804B6BF4u),
            f32(context->fpr_bits[1], 0x804B6BF4u),
            context->fpscr),
        true,
        false,
        nullptr,
        0x804B6BF4u);
    galaxy::ppc_commit_scalar_result(
        context,
        5u,
        galaxy::ppc_f64_binary_to_f32(
            galaxy::PpcFloatBinaryOperation::Multiply,
            context->fpr_bits[6],
            context->fpr_bits[0],
            context->fpscr),
        true,
        false,
        nullptr,
        0x804B6BF8u);

    commit_paired_muls0(2u, 2u, 5u, 0x804B6BFCu);
    commit_paired_muls0(3u, 3u, 5u, 0x804B6C00u);
    galaxy::psq_store(
        context, 2u, memory, context->gpr[4], 0u, false, true, nullptr, 0x804B6C04u);
    galaxy::psq_store(
        context, 3u, memory, context->gpr[4] + 8u, 0u, true, true, nullptr, 0x804B6C08u);
}

struct GprSpanFault { std::uint32_t pc; };

void throw_gpr_span_fault(void*, std::uint32_t pc, const char*) {
    throw GprSpanFault{pc};
}

bool test_checked_span_resolution() {
    bool passed = true;
    std::array<std::byte, 32> ram{}, special{};
    constexpr std::array<std::uint32_t, 6> aliases{0u, 0x80000000u, 0xc0000000u,
        0x10000000u, 0x90000000u, 0xd0000000u};
    std::array<galaxy::GuestMemoryRegionV1, 7> regions{};
    galaxy::GuestMemoryV1 memory{};
    for (unsigned i = 0u; i < aliases.size(); ++i) regions[i] = {aliases[i], 32u, ram.data()};
    regions.back() = {0xcc000080u, 32u, special.data()};
    memory.regions = regions.data(); memory.region_count = static_cast<std::uint32_t>(regions.size());
    unsigned device_reads = 0u;
    memory.user = &device_reads;
    memory.read_device = [](void* user, std::uint32_t, std::uint32_t, std::byte*) {
        ++*static_cast<unsigned*>(user); return false;
    };
    galaxy::NativeServicesV1 services{}; services.fatal = throw_gpr_span_fault;
    for (unsigned mode = 0u; mode < 3u; ++mode) {
        for (auto alias : aliases) memory.fast_regions[alias >> 28u] =
            mode == 0u ? galaxy::GuestMemoryFastRegionV1{} :
                galaxy::GuestMemoryFastRegionV1{mode == 1u ? 32u : 16u, ram.data()};
        for (const auto& region : regions) for (unsigned offset : {0u, 1u, 8u, 31u, 32u, 33u})
            for (unsigned size : {0u, 1u, 2u, 12u, 32u, 33u, 0xffffffffu}) {
                const std::uint32_t address = region.guest_base + offset;
                // Literal complete-region oracle, independent of the fast table.
                std::byte* expected = nullptr;
                for (const auto& candidate : regions) {
                    if (address >= candidate.guest_base &&
                        static_cast<std::uint64_t>(address) + size <=
                            static_cast<std::uint64_t>(candidate.guest_base) + candidate.size) {
                        expected = candidate.host_base + (address - candidate.guest_base); break;
                    }
                }
                std::byte* actual = nullptr; bool faulted = false;
                try { actual = galaxy::resolve_guest(&memory, address, size, &services, 0x80001000u); }
                catch (GprSpanFault fault) { faulted = fault.pc == 0x80001000u; }
                passed &= expect(actual == expected && faulted == (expected == nullptr),
                    "checked native span resolution retains six RAM aliases, partial-table fallback, bounds and exact fault PC");
            }
    }
    passed &= expect(device_reads == 0u, "plain span resolution never performs a device read");
    // The scalar ABI already accepts a fast-only owner. Native span helpers
    // can use that same valid capability without requiring a second table.
    memory.regions = nullptr; memory.region_count = 0u;
    memory.fast_regions[8] = {32u, ram.data()};
    passed &= expect(galaxy::resolve_guest(&memory, 0x80000008u, 12u, &services, 0x80001000u) == ram.data() + 8u,
        "native span helper accepts the same complete fast-only RAM capability as scalar accesses");
    return passed;
}

bool test_audio_interleave_order_and_overlap() {
    bool passed = true;
    constexpr std::uint32_t base = 0x80000000u;
    struct StoreObservation {
        std::array<std::byte, 64> bytes{};
        std::array<std::uint32_t, 6> registers{};
        std::uint32_t address{};
    };
    struct Observer {
        const std::array<StoreObservation, 8>* expected{};
        const std::array<std::byte, 64>* ram{};
        const galaxy::PpcContext* context{};
        unsigned calls{};
        bool valid{true};
        static void notify(void* user, std::uint32_t address, std::uint32_t size) {
            auto& observer = *static_cast<Observer*>(user);
            if (observer.calls >= observer.expected->size()) { observer.valid = false; return; }
            const auto& expected = (*observer.expected)[observer.calls++];
            const auto& context = *observer.context;
            observer.valid &= address == expected.address && size == 2u &&
                *observer.ram == expected.bytes && expected.registers ==
                std::array<std::uint32_t, 6>{context.gpr[0], context.gpr[3], context.gpr[4],
                    context.gpr[5], context.gpr[6], context.ctr};
        }
    };
    for (unsigned mode = 0u; mode < 5u; ++mode) {
        for (unsigned count = 0u; count <= 4u; ++count) {
            for (unsigned left : {0u, 2u, 8u, 16u}) for (unsigned right : {0u, 2u, 8u, 16u})
                for (unsigned destination : {0u, 2u, 8u, 16u}) {
                std::array<std::byte, 64> ram{};
                for (unsigned byte = 0u; byte < ram.size(); ++byte) ram[byte] = static_cast<std::byte>(0x81u + byte * 11u);
                auto expected_bytes = ram;
                galaxy::PpcContext actual{}, expected{};
                actual.gpr[3] = base + left; actual.gpr[4] = base + right;
                actual.gpr[5] = base + destination; actual.gpr[6] = count;
                expected = actual; expected.ctr = count;
                expected.cr = count == 0u ? 0x20000000u : 0x40000000u;
                std::array<StoreObservation, 8> observations{};
                unsigned stores = 0u;
                const auto read = [&](unsigned offset) {
                    const auto value = (static_cast<std::uint32_t>(expected_bytes[offset]) << 8u) |
                        static_cast<std::uint32_t>(expected_bytes[offset + 1u]);
                    return value & 0x8000u ? value | 0xffff0000u : value;
                };
                for (unsigned sample = 0u; sample < count; ++sample) {
                    expected.gpr[6] = read(expected.gpr[3] - base); expected.gpr[3] += 2u;
                    expected.gpr[0] = read(expected.gpr[4] - base); expected.gpr[4] += 2u;
                    for (unsigned channel = 0u; channel < 2u; ++channel) {
                        const unsigned offset = expected.gpr[5] - base + channel * 2u;
                        const auto value = expected.gpr[channel == 0u ? 6u : 0u];
                        expected_bytes[offset] = static_cast<std::byte>(value >> 8u);
                        expected_bytes[offset + 1u] = static_cast<std::byte>(value);
                        observations[stores++] = {expected_bytes,
                            {expected.gpr[0], expected.gpr[3], expected.gpr[4], expected.gpr[5], expected.gpr[6], expected.ctr},
                            base + offset};
                    }
                    expected.gpr[5] += 4u; --expected.ctr;
                }
                Observer observer{&observations, &ram, &actual};
                galaxy::GuestMemoryRegionV1 region{base, static_cast<std::uint32_t>(ram.size()), ram.data()};
                galaxy::GuestMemoryV1 memory{};
                memory.regions = &region; memory.region_count = 1u;
                if (mode != 4u) memory.fast_regions[8] = {region.size, ram.data()};
                memory.user = &observer;
                if (mode != 0u) memory.notify_write = &Observer::notify;
                std::atomic_uint64_t renderer_dirty{0u}; std::uint64_t cpu_dirty = 0u;
                if (mode == 1u) {
                    memory.dirty_page_words = &renderer_dirty; memory.dirty_tracked_size = region.size;
                    memory.dirty_page_shift = 2u; memory.dirty_page_word_count = 1u;
                } else if (mode == 2u) {
                    memory.cpu_dirty_page_words = &cpu_dirty; memory.cpu_dirty_tracked_size = region.size;
                    memory.cpu_dirty_page_shift = 2u; memory.cpu_dirty_page_word_count = 1u;
                }
                galaxy::native_audio_interleave_i16_804878BC(&actual, &memory, nullptr, 0x804878BCu);
                std::uint64_t expected_dirty = 0u;
                for (unsigned byte = destination; byte < destination + count * 4u; ++byte)
                    expected_dirty |= UINT64_C(1) << (byte / 4u);
                passed &= expect(ram == expected_bytes && std::memcmp(actual.gpr, expected.gpr, sizeof(actual.gpr)) == 0 &&
                    actual.cr == expected.cr && actual.ctr == expected.ctr && observer.valid &&
                    renderer_dirty.load() == (mode == 1u ? expected_dirty : 0u) &&
                    cpu_dirty == (mode == 2u ? expected_dirty : 0u) &&
                    observer.calls == (mode >= 3u ? count * 2u : 0u),
                    "audio interleave retains load/store ordering, alias results, last loaded values and callback state");
            }
        }
    }
    std::array<std::byte, 8> ram{std::byte{0x81}, std::byte{0x23}, std::byte{0x45}, std::byte{0x67}};
    galaxy::GuestMemoryRegionV1 region{base, 6u, ram.data()};
    galaxy::GuestMemoryV1 memory{}; memory.regions = &region; memory.region_count = 1u;
    memory.fast_regions[8] = {region.size, ram.data()};
    galaxy::NativeServicesV1 services{}; services.fatal = throw_gpr_span_fault;
    galaxy::PpcContext context{};
    context.gpr[3] = base; context.gpr[4] = base + 2u; context.gpr[5] = base + 4u; context.gpr[6] = 1u;
    bool faulted = false;
    try { galaxy::native_audio_interleave_i16_804878BC(&context, &memory, &services, 0x804878BCu); }
    catch (GprSpanFault fault) { faulted = fault.pc == 0x804878DCu; }
    passed &= expect(faulted && ram[4] == std::byte{0x81} && ram[5] == std::byte{0x23} &&
        context.gpr[3] == base + 2u && context.gpr[4] == base + 4u && context.gpr[5] == base + 4u &&
        context.gpr[6] == 0xffff8123u && context.gpr[0] == 0x4567u && context.ctr == 1u,
        "audio interleave keeps its first store and exact registers when the second store faults");
    return passed;
}

bool test_vector_copy_instruction_effects() {
    bool passed = true;
    constexpr std::uint32_t base = 0x80000000u;
    constexpr std::array<std::byte, 12> source{
        std::byte{0x3f}, std::byte{0x80}, std::byte{0}, std::byte{0},
        std::byte{0xc0}, std::byte{0}, std::byte{0}, std::byte{0},
        std::byte{0x40}, std::byte{0x40}, std::byte{0}, std::byte{0}};
    constexpr std::array<std::uint64_t, 3> values{
        UINT64_C(0x4008000000000000), UINT64_C(0xc000000000000000), UINT64_C(0x3ff0000000000000)};
    constexpr std::uint64_t sentinel = UINT64_C(0x4014000000000000);
    for (bool paired : {false, true}) for (unsigned destination : {0u, 4u, 8u, 16u}) {
        std::array<std::byte, 32> ram{};
        std::copy(source.begin(), source.end(), ram.begin());
        auto expected_bytes = ram;
        std::copy(source.begin(), source.end(), expected_bytes.begin() + destination);
        galaxy::GuestMemoryRegionV1 region{base, static_cast<std::uint32_t>(ram.size()), ram.data()};
        galaxy::GuestMemoryV1 memory{}; memory.regions = &region; memory.region_count = 1u;
        memory.fast_regions[8] = {region.size, ram.data()};
        galaxy::PpcContext context{};
        context.msr = galaxy::kMsrFloatingPointAvailable;
        context.hid2 = paired ? 0x20000000u : 0u;
        context.gpr[3] = base + destination; context.gpr[4] = base;
        for (unsigned reg = 0u; reg < 3u; ++reg) context.ps1_bits[reg] = sentinel;
        context.fpscr = 0x80000000u;
        galaxy::native_vec_copy_12(&context, &memory, nullptr, 0x8001CF64u);
        for (unsigned reg = 0u; reg < 3u; ++reg) {
            passed &= expect(context.fpr_bits[reg] == values[reg] &&
                context.ps1_bits[reg] == (paired ? values[reg] : sentinel),
                "scalar vector copy retains its three loaded FPRs and HID2-dependent PS1 effects");
        }
        passed &= expect(ram == expected_bytes && context.fpscr == 0x80000000u &&
            context.gpr[3] == base + destination && context.gpr[4] == base,
            "scalar vector copy loads all three words before overlapping stores without changing FPSCR or pointers");
    }
    std::array<std::byte, 32> ram{};
    ram[0] = std::byte{0xfe}; ram[1] = std::byte{3};
    std::copy(source.begin() + 8u, source.end(), ram.begin() + 8u);
    auto expected_bytes = ram;
    expected_bytes[16] = std::byte{0xfe}; expected_bytes[17] = std::byte{3};
    std::copy(source.begin() + 8u, source.end(), expected_bytes.begin() + 24u);
    galaxy::GuestMemoryRegionV1 region{base, static_cast<std::uint32_t>(ram.size()), ram.data()};
    galaxy::GuestMemoryV1 memory{}; memory.regions = &region; memory.region_count = 1u;
    memory.fast_regions[8] = {region.size, ram.data()};
    galaxy::PpcContext context{};
    context.msr = galaxy::kMsrFloatingPointAvailable; context.hid2 = 0xa0000000u;
    context.gqr[0] = (6u << 16u) | 6u; // signed byte load/store, scale zero
    context.gpr[3] = base + 16u; context.gpr[4] = base;
    context.fpr_bits[2] = sentinel; context.ps1_bits[2] = sentinel;
    galaxy::native_vec_copy_12(&context, &memory, nullptr, 0x80018B8Cu);
    passed &= expect(ram == expected_bytes && context.fpr_bits[1] == values[1] &&
        context.ps1_bits[1] == values[0] && context.fpr_bits[0] == values[0] &&
        context.ps1_bits[0] == values[0] && context.fpr_bits[2] == sentinel && context.ps1_bits[2] == sentinel,
        "paired vector copy respects GQR0 quantization, the scalar third component and untouched register two");

    // An unavailable FPU retries the first real instruction. A fault at the
    // third load keeps the first two FPR effects and performs no output store.
    std::copy(source.begin(), source.end(), ram.begin());
    FpuUnavailableProbe probe{};
    galaxy::NativeServicesV1 services{}; services.user = &probe;
    services.fpu_unavailable = &simulate_fpu_unavailable; services.fatal = throw_gpr_span_fault;
    context = {}; context.gpr[3] = base + 16u; context.gpr[4] = base;
    context.fpr_bits[0] = sentinel;
    region.size = 8u; memory.fast_regions[8].size = 8u;
    const auto before_fault = ram;
    bool faulted = false;
    try { galaxy::native_vec_copy_12(&context, &memory, &services, 0x8001CF64u); }
    catch (GprSpanFault fault) { faulted = fault.pc == 0x8001CF6Cu; }
    passed &= expect(faulted && probe.calls == 1u && probe.guest_pc == 0x8001CF64u &&
        context.fpr_bits[2] == values[2] && context.fpr_bits[1] == values[1] &&
        context.fpr_bits[0] == sentinel && ram == before_fault,
        "vector copy restores FPU retry and exact partial-load fault effects before any output store");
    return passed;
}

bool test_matrix_scale_publishes_before_exception() {
    bool passed = true;
    constexpr std::uint32_t base = 0x80000000u;
    for (bool tracked : {false, true}) {
        std::array<std::byte, 48> ram{};
        for (unsigned word = 0u; word < 12u; ++word) {
            ram[word * 4u] = std::byte{0x3f}; ram[word * 4u + 1u] = std::byte{0x80};
        }
        // Only the final multiply overflows, after the first output store.
        ram[0x18] = std::byte{0x7f}; ram[0x19] = std::byte{0x7f};
        ram[0x1a] = std::byte{0xff}; ram[0x1b] = std::byte{0xff};
        auto expected_bytes = ram; expected_bytes[0] = std::byte{0x40}; expected_bytes[1] = std::byte{0};
        galaxy::GuestMemoryRegionV1 region{base, static_cast<std::uint32_t>(ram.size()), ram.data()};
        galaxy::GuestMemoryV1 memory{}; memory.regions = &region; memory.region_count = 1u;
        memory.fast_regions[8] = {region.size, ram.data()};
        NotifyProbe notify{}; memory.user = &notify; memory.notify_write = capture_notify_write;
        std::atomic_uint64_t renderer_dirty{0u}; std::uint64_t cpu_dirty = 0u;
        if (tracked) {
            memory.dirty_page_words = &renderer_dirty; memory.dirty_tracked_size = region.size;
            memory.dirty_page_shift = 2u; memory.dirty_page_word_count = 1u;
            memory.cpu_dirty_page_words = &cpu_dirty; memory.cpu_dirty_tracked_size = region.size;
            memory.cpu_dirty_page_shift = 2u; memory.cpu_dirty_page_word_count = 1u;
        }
        galaxy::PpcContext context{};
        context.msr = galaxy::kMsrFloatingPointAvailable;
        context.gpr[3] = base; context.fpr_bits[1] = UINT64_C(0x4000000000000000);
        context.fpr_bits[2] = UINT64_C(0x3ff0000000000000);
        context.fpscr = 0x40u; // overflow exception enabled
        galaxy::NativeServicesV1 services{}; services.fatal = throw_gpr_span_fault;
        bool faulted = false;
        try { galaxy::native_mtx_scale_803A387C(&context, &memory, &services, 0x803A387Cu); }
        catch (GprSpanFault fault) { faulted = fault.pc == 0x803A38C4u; }
        passed &= expect(faulted && ram == expected_bytes &&
            renderer_dirty.load() == (tracked ? 1u : 0u) && cpu_dirty == (tracked ? 1u : 0u) &&
            notify.writes == (tracked ? 0u : 1u) &&
            (tracked || (notify.address == base && notify.size == 4u)) &&
            context.fpr_bits[2] == UINT64_C(0x3ff0000000000000),
            "matrix scale publishes the completed first store before the final multiply faults without later stores or destination mutation");
    }
    return passed;
}

bool test_gpr_helper_ram_spans() {
    bool passed = true;
    // Literal byte oracle, independent of the scalar load/store helpers.
    for (const auto alias : {0u, 0x80000000u, 0xC0000000u,
                            0x10000000u, 0x90000000u, 0xD0000000u}) {
        for (std::uint32_t first = 14u; first <= 29u; ++first) {
            std::array<std::byte, 0x100> ram;
            ram.fill(std::byte{0xCC});
            galaxy::GuestMemoryV1 memory{};
            memory.fast_regions[alias >> 28u] = {0x100u, ram.data()};
            std::atomic_uint64_t shared_dirty{};
            std::uint64_t cpu_dirty{};
            memory.dirty_page_words = &shared_dirty;
            memory.dirty_tracked_base = alias & 0x1FFFFFFFu;
            memory.dirty_tracked_size = 0x100u;
            memory.dirty_page_shift = 6u;
            memory.dirty_page_word_count = 1u;
            memory.cpu_dirty_page_words = &cpu_dirty;
            memory.cpu_dirty_tracked_base = alias & 0x1FFFFFFFu;
            memory.cpu_dirty_tracked_size = 0x100u;
            memory.cpu_dirty_page_shift = 6u;
            memory.cpu_dirty_page_word_count = 1u;
            galaxy::PpcContext saved{};
            for (std::uint32_t reg = 0u; reg < 32u; ++reg)
                saved.gpr[reg] = 0xA5123400u + reg;
            saved.gpr[11] = alias + 0x94u;
            saved.cr = 0x12345678u;
            saved.lr = 0x80001000u;
            saved.fpscr = 0x45678901u;
            galaxy::PpcContext original{};
            std::memcpy(&original, &saved, sizeof(saved));
            const auto save_pc = 0x805174FCu + (first - 14u) * 4u;
            galaxy::native_savegpr_805174FC(&saved, &memory, nullptr, save_pc);
            galaxy::PpcContext expected_context{};
            std::memcpy(&expected_context, &original, sizeof(original));
            expected_context.pc = save_pc;
            passed &= expect(std::memcmp(&saved, &expected_context, sizeof(saved)) == 0,
                             "GPR span save preserves architectural state except entry PC");
            for (std::uint32_t offset = 0u; offset < ram.size(); ++offset) {
                std::uint8_t expected = 0xCCu;
                const auto begin = 0x14u + first * 4u;
                if (offset >= begin && offset < 0x94u) {
                    const auto reg = (offset - 0x14u) / 4u;
                    expected = static_cast<std::uint8_t>(
                        original.gpr[reg] >> ((3u - (offset & 3u)) * 8u));
                }
                passed &= expect(static_cast<std::uint8_t>(ram[offset]) == expected,
                                 "GPR span save has exact big-endian bytes and unchanged neighbors");
            }
            const auto first_page = (0x14u + first * 4u) >> 6u;
            const auto expected_dirty = first_page == 1u ? 6u : 4u;
            passed &= expect(shared_dirty.load() == expected_dirty && cpu_dirty == expected_dirty,
                             "GPR span save marks both trackers across page boundaries");
            galaxy::PpcContext restored{};
            std::memcpy(&restored, &original, sizeof(original));
            for (std::uint32_t reg = first; reg < 32u; ++reg) restored.gpr[reg] = 0u;
            const auto restore_pc = 0x80517548u + (first - 14u) * 4u;
            galaxy::native_restgpr_80517548(&restored, &memory, nullptr, restore_pc);
            std::memcpy(&expected_context, &original, sizeof(original));
            expected_context.pc = restore_pc;
            passed &= expect(std::memcmp(&restored, &expected_context, sizeof(restored)) == 0,
                             "GPR span restore preserves all state and exact requested registers");
        }
    }
    {
        std::array<std::byte, 0x100> partial_ram{};
        galaxy::GuestMemoryV1 partial{};
        partial.fast_regions[8] = {0x100u, partial_ram.data()};
        std::atomic_uint64_t shared{};
        std::uint64_t cpu{};
        partial.dirty_page_words = &shared;
        partial.dirty_tracked_base = 0x80u;
        partial.dirty_tracked_size = 0x40u;
        partial.dirty_page_shift = 2u;
        partial.dirty_page_word_count = 1u;
        partial.cpu_dirty_page_words = &cpu;
        partial.cpu_dirty_tracked_base = 0x88u;
        partial.cpu_dirty_tracked_size = 8u;
        partial.cpu_dirty_page_shift = 2u;
        partial.cpu_dirty_page_word_count = 1u;
        galaxy::PpcContext partial_context{};
        partial_context.gpr[11] = 0x80000094u;
        galaxy::native_savegpr_805174FC(&partial_context, &partial, nullptr, 0x805174FCu);
        passed &= expect(shared.load() == 0x1Fu && cpu == 3u,
                         "GPR span save preserves independent partially overlapping dirty tracker coverage");
    }
    // A range mapping only the first two words of the tail falls back before
    // touching anything, then preserve successful words and the precise fault.
    std::array<std::byte, 0x100> ram;
    ram.fill(std::byte{0xCC});
    galaxy::GuestMemoryRegionV1 region{0x80000000u, 0x90u, ram.data()};
    galaxy::GuestMemoryV1 memory{};
    memory.fast_regions[8] = {0x90u, ram.data()};
    memory.regions = &region;
    memory.region_count = 1u;
    NotifyProbe notify{};
    memory.user = &notify;
    memory.notify_write = capture_notify_write;
    galaxy::NativeServicesV1 services{};
    services.fatal = throw_gpr_span_fault;
    galaxy::PpcContext context{};
    context.gpr[11] = 0x80000094u;
    context.gpr[29] = 0x11223344u;
    context.gpr[30] = 0x55667788u;
    context.gpr[31] = 0x99AABBCCu;
    bool faulted = false;
    try { galaxy::native_savegpr_805174FC(&context, &memory, &services, 0x80517538u); }
    catch (GprSpanFault f) { faulted = f.pc == 0x80517540u; }
    passed &= expect(faulted && notify.writes == 2u && notify.address == 0x8000008Cu && notify.size == 4u,
                     "GPR save boundary fallback preserves two scalar stores before the exact fault");
    constexpr std::array<std::byte, 8> prefix{std::byte{0x11},std::byte{0x22},std::byte{0x33},std::byte{0x44},
                                            std::byte{0x55},std::byte{0x66},std::byte{0x77},std::byte{0x88}};
    passed &= expect(std::memcmp(ram.data() + 0x88u, prefix.data(), prefix.size()) == 0 && ram[0x90u] == std::byte{0xCC},
                     "GPR save fault leaves the failing slot untouched");
    context.gpr[29] = context.gpr[30] = context.gpr[31] = 0xDEADBEEFu;
    faulted = false;
    try { galaxy::native_restgpr_80517548(&context, &memory, &services, 0x80517584u); }
    catch (GprSpanFault f) { faulted = f.pc == 0x8051758Cu; }
    passed &= expect(faulted && context.gpr[29] == 0x11223344u && context.gpr[30] == 0x55667788u && context.gpr[31] == 0xDEADBEEFu,
                     "GPR restore boundary fallback preserves prior loads and exact fault PC");
    return passed;
}

struct GprTrackerOrderProbe {
    std::byte* ram{};
    std::uint32_t calls{};
    std::array<std::uint32_t, 18> addresses{}, sizes{};
    std::array<std::array<std::byte, 512>, 18> snapshots{};
};

void capture_gpr_tracker_order(void* user, std::uint32_t address, std::uint32_t size) {
    auto& probe = *static_cast<GprTrackerOrderProbe*>(user);
    if (probe.calls < probe.addresses.size()) {
        probe.addresses[probe.calls] = address;
        probe.sizes[probe.calls] = size;
        std::memcpy(probe.snapshots[probe.calls].data(), probe.ram, 512u);
    }
    ++probe.calls;
}

bool test_gpr_save_tracker_admission() {
    bool passed = true;
    // Independent literal byte/bit oracle. Include renderer-sized pages,
    // tiny pages, shifted tracker origins, absent/overlapping CPU trackers,
    // partial extents, insufficient tables, and unaligned guest words.
    for (const auto alias : {0u, 0x80000000u, 0xC0000000u,
                            0x10000000u, 0x90000000u, 0xD0000000u}) {
        const auto physical_base = alias & 0x1FFFFFFFu;
        for (const auto origin : {0u, 120u}) {
            for (std::uint32_t first = 14u; first <= 29u; ++first) {
                for (std::uint32_t alignment = 0u; alignment < 4u; ++alignment) {
                    for (const auto shift : {0u, 1u, 2u, 5u, 6u, 7u, 12u}) {
                        for (std::uint32_t scenario = 0u; scenario < 11u; ++scenario) {
                            std::array<std::byte, 512> ram, expected;
                            ram.fill(std::byte{0xCC}); expected = ram;
                            std::array<std::atomic_uint64_t, 4> shared{};
                            std::array<std::uint64_t, 4> cpu{}, expected_shared{}, expected_cpu{};
                            shared[3].store(UINT64_C(0x8000000000000000));
                            cpu[3] = expected_shared[3] = expected_cpu[3] =
                                UINT64_C(0x8000000000000000);
                            galaxy::GuestMemoryV1 memory{};
                            memory.fast_regions[alias >> 28u] = {512u, ram.data()};
                            memory.dirty_page_words = shared.data();
                            memory.dirty_tracked_base = physical_base;
                            memory.dirty_tracked_size = 512u;
                            memory.dirty_page_shift = shift;
                            memory.dirty_page_word_count = 4u;
                            memory.cpu_dirty_page_words = cpu.data();
                            memory.cpu_dirty_tracked_base = physical_base + 512u;
                            memory.cpu_dirty_tracked_size = 512u;
                            memory.cpu_dirty_page_shift = shift;
                            memory.cpu_dirty_page_word_count = 4u;
                            if (scenario == 0u) memory.cpu_dirty_page_words = nullptr;
                            if (scenario == 2u) memory.cpu_dirty_tracked_base = physical_base;
                            if (scenario == 3u || scenario == 5u) {
                                memory.cpu_dirty_tracked_base = physical_base + origin + 0x88u;
                                memory.cpu_dirty_tracked_size = 8u;
                            }
                            if (scenario == 4u) {
                                memory.dirty_tracked_base = physical_base + origin + 0x80u;
                                memory.dirty_tracked_size = 0x10u;
                            }
                            if (scenario == 5u || scenario == 10u)
                                memory.dirty_page_words = nullptr;
                            if (scenario == 6u) memory.dirty_page_word_count = 1u;
                            if (scenario == 7u) {
                                memory.dirty_tracked_base = physical_base + 1u;
                                memory.dirty_tracked_size = 511u;
                            }
                            if (scenario == 8u) memory.dirty_page_shift = 32u;
                            if (scenario == 9u || scenario == 10u) {
                                memory.cpu_dirty_tracked_base = physical_base + origin + 0x88u;
                                memory.cpu_dirty_tracked_size = 0u;
                            }
                            GprTrackerOrderProbe probe{};
                            probe.ram = ram.data(); memory.user = &probe;
                            memory.notify_write = capture_gpr_tracker_order;
                            galaxy::PpcContext context{};
                            for (std::uint32_t reg = 0u; reg < 32u; ++reg)
                                context.gpr[reg] = 0xA5123400u + reg;
                            context.gpr[11] = alias + origin + 0x94u + alignment;
                            context.cr = 0x12345678u; context.lr = 0x80001000u;
                            context.fpscr = 0x45678901u;
                            galaxy::PpcContext expected_context{};
                            std::memcpy(&expected_context, &context, sizeof(context));
                            const auto pc = 0x805174FCu + (first - 14u) * 4u;
                            expected_context.pc = pc;
                            galaxy::native_savegpr_805174FC(&context, &memory, nullptr, pc);
                            std::uint32_t expected_calls = 0u;
                            // The oracle uses byte extents and literal memory
                            // encoding, never the optimized tracker/load helpers.
                            for (std::uint32_t reg = first; reg < 32u; ++reg) {
                                const auto offset = origin + 0x14u + reg * 4u + alignment;
                                for (std::uint32_t byte = 0u; byte < 4u; ++byte)
                                    expected[offset + byte] = static_cast<std::byte>(
                                        expected_context.gpr[reg] >> ((3u - byte) * 8u));
                                const std::uint64_t begin = physical_base + offset;
                                const auto mark = [&](bool present, std::uint32_t base,
                                                      std::uint32_t size, std::uint32_t page_shift,
                                                      std::uint32_t count, auto& bits) {
                                    if (!present || page_shift >= 32u || count == 0u)
                                        return false;
                                    const auto overlap_begin = std::max<std::uint64_t>(begin, base);
                                    const auto overlap_end = std::min<std::uint64_t>(begin + 4u, std::uint64_t(base) + size);
                                    if (overlap_begin >= overlap_end ||
                                        ((overlap_end - 1u - base) >> page_shift) >= std::uint64_t(count) * 64u)
                                        return false;
                                    for (std::uint32_t byte = 0u; byte < 4u; ++byte) {
                                        if (begin + byte < base || begin + byte >= std::uint64_t(base) + size) continue;
                                        const auto page = (begin + byte - base) >> page_shift;
                                        bits[page / 64u] |= UINT64_C(1) << (page % 64u);
                                    }
                                    return begin >= base && begin + 4u <= std::uint64_t(base) + size;
                                };
                                const bool shared_marked = mark(memory.dirty_page_words != nullptr,
                                    memory.dirty_tracked_base, memory.dirty_tracked_size,
                                    memory.dirty_page_shift, memory.dirty_page_word_count, expected_shared);
                                const bool cpu_marked = mark(memory.cpu_dirty_page_words != nullptr,
                                    memory.cpu_dirty_tracked_base, memory.cpu_dirty_tracked_size,
                                    memory.cpu_dirty_page_shift, memory.cpu_dirty_page_word_count, expected_cpu);
                                if (!shared_marked && !cpu_marked) {
                                    passed &= expect(expected_calls < probe.addresses.size() &&
                                        probe.addresses[expected_calls] == alias + offset &&
                                        probe.sizes[expected_calls] == 4u &&
                                        probe.snapshots[expected_calls] == expected,
                                        "GPR tracker fallback callback sees each ordered word and untouched future words");
                                    ++expected_calls;
                                }
                            }
                            passed &= expect(probe.calls == expected_calls && ram == expected &&
                                std::memcmp(&context, &expected_context, sizeof(context)) == 0,
                                "GPR tracker admission preserves literal bytes, callback count, neighbors and architectural state");
                            for (std::uint32_t word = 0u; word < 4u; ++word)
                                passed &= expect(shared[word].load() == expected_shared[word] &&
                                    cpu[word] == expected_cpu[word],
                                    "GPR tracker admission preserves both literal dirty bit sets and previously marked bits");
                        }
                    }
                }
            }
        }
    }
    return passed;
}

struct PsqOrderProbe {
    std::byte* ram{};
    std::uint32_t calls{};
    std::array<std::uint32_t, 2> addresses{}, sizes{};
    std::array<std::array<std::byte, 8>, 2> observed{};
};

void capture_psq_order(void* user, std::uint32_t address, std::uint32_t size) {
    auto& probe = *static_cast<PsqOrderProbe*>(user);
    if (probe.calls < 2u) {
        probe.addresses[probe.calls] = address;
        probe.sizes[probe.calls] = size;
        std::memcpy(probe.observed[probe.calls].data(), probe.ram, 8u);
    }
    ++probe.calls;
}

bool test_psq_complete_ram_instruction() {
    bool passed = true;
    // Literal IEEE field/byte oracle: no memory helper supplies expected bytes.
    constexpr std::array<std::uint32_t, 8> single{
        0x3FC00000u, 0xC0200000u, 0u, 0x80000000u,
        0x7F800000u, 0xFF800000u, 1u, 0x7F800001u};
    constexpr std::array<std::uint64_t, 8> wide{
        0x3FF8000000000000ull, 0xC004000000000000ull, 0ull,
        0x8000000000000000ull, 0x7FF0000000000000ull,
        0xFFF0000000000000ull, 0x36A0000000000000ull, 0x7FF0000020000000ull};
    for (auto alias : {0u, 0x80000000u, 0xC0000000u,
                       0x10000000u, 0x90000000u, 0xD0000000u}) {
        for (bool one : {false, true}) {
            for (std::size_t index = 0u; index < single.size(); ++index) {
                const auto next = (index + 1u) % single.size();
                std::array<std::byte, 64> ram{};
                for (std::uint32_t byte = 0u; byte < 4u; ++byte) {
                    ram[31u + byte] = static_cast<std::byte>(single[index] >> ((3u-byte)*8u));
                    ram[35u + byte] = static_cast<std::byte>(single[next] >> ((3u-byte)*8u));
                }
                galaxy::GuestMemoryV1 memory{};
                memory.fast_regions[alias >> 28u] = {64u, ram.data()};
                galaxy::PpcContext context{};
                context.hid2 = 0xA0000000u;
                context.fpscr = 0xA1B2C3D4u;
                galaxy::psq_load(&context, 7u, &memory, alias + 31u, 0u, one, true, nullptr, 0x80001000u);
                passed &= expect(context.fpr_bits[7] == wide[index] &&
                    context.ps1_bits[7] == (one ? 0x3FF0000000000000ull : wide[next]) &&
                    context.fpscr == 0xA1B2C3D4u,
                    "PSQ RAM admission preserves unaligned aliases, signed zero, infinity, subnormal and NaN lanes");
                std::atomic_uint64_t shared{};
                std::uint64_t cpu{};
                memory.dirty_page_words = &shared;
                memory.dirty_tracked_base = alias & 0x1FFFFFFFu;
                memory.dirty_tracked_size = 64u;
                memory.dirty_page_shift = 5u;
                memory.dirty_page_word_count = 1u;
                memory.cpu_dirty_page_words = &cpu;
                memory.cpu_dirty_tracked_base = alias & 0x1FFFFFFFu;
                memory.cpu_dirty_tracked_size = 64u;
                memory.cpu_dirty_page_shift = 5u;
                memory.cpu_dirty_page_word_count = 1u;
                ram.fill(std::byte{0xCC});
                galaxy::psq_store(&context, 7u, &memory, alias + 31u, 0u, one, true, nullptr, 0x80001004u);
                for (std::uint32_t byte = 0u; byte < ram.size(); ++byte) {
                    std::byte expected{0xCC};
                    if (byte >= 31u && byte < (one ? 35u : 39u)) {
                        const auto lane = byte < 35u ? index : next;
                        const auto bits = lane == 6u ? 0u : single[lane]; // PS stores flush subnormal.
                        expected = static_cast<std::byte>(bits >> ((3u - ((byte-31u)&3u))*8u));
                    }
                    passed &= expect(ram[byte] == expected, "PSQ admitted store has literal big-endian bytes and untouched guards");
                }
                passed &= expect(shared.load() == 3u && cpu == 3u,
                    "PSQ admitted writes mark both sides of the 32-byte dirty boundary in both trackers");
            }
        }
    }
    std::array<std::byte, 8> ram;
    ram.fill(std::byte{0xCC});
    PsqOrderProbe order{};
    order.ram = ram.data();
    galaxy::GuestMemoryV1 memory{};
    memory.fast_regions[8] = {8u, ram.data()};
    memory.user = &order;
    memory.notify_write = capture_psq_order;
    galaxy::PpcContext context{};
    context.hid2 = 0xA0000000u;
    context.fpr_bits[7] = wide[0]; context.ps1_bits[7] = wide[1];
    galaxy::psq_store(&context, 7u, &memory, 0x80000000u, 0u, false, true, nullptr, 0x80001004u);
    constexpr std::array<std::byte, 8> expected{
        std::byte{0x3F},std::byte{0xC0},std::byte{0},std::byte{0},
        std::byte{0xC0},std::byte{0x20},std::byte{0},std::byte{0}};
    passed &= expect(order.calls == 2u && order.addresses[0] == 0x80000000u &&
        order.addresses[1] == 0x80000004u && order.sizes[0] == 4u && order.sizes[1] == 4u &&
        std::memcmp(order.observed[0].data(), expected.data(), 4u) == 0 &&
        order.observed[0][4] == std::byte{0xCC} && order.observed[1] == expected,
        "PSQ writes notify each lane after its bytes become visible, before the next lane write");
    // Complete admission fails before access. The scalar path must retain the
    // first lane before the second faults; one-element form needs only 4 bytes.
    memory.fast_regions[8].size = 4u;
    galaxy::GuestMemoryRegionV1 region{0x80000000u, 4u, ram.data()};
    memory.regions = &region; memory.region_count = 1u;
    galaxy::NativeServicesV1 services{};
    services.fatal = throw_gpr_span_fault;
    context.fpr_bits[7] = context.ps1_bits[7] = 0xDEADBEEFull;
    bool faulted = false;
    try { galaxy::psq_load(&context, 7u, &memory, 0x80000000u, 0u, false, true, &services, 0x80001000u); }
    catch (GprSpanFault f) { faulted = f.pc == 0x80001000u; }
    passed &= expect(faulted && context.fpr_bits[7] == wide[0] && context.ps1_bits[7] == 0xDEADBEEFull,
        "PSQ partial load preserves lane zero and faults before changing lane one");
    context.fpr_bits[7] = wide[1]; context.ps1_bits[7] = wide[0];
    order.calls = 0u; ram.fill(std::byte{0xCC}); faulted = false;
    try { galaxy::psq_store(&context, 7u, &memory, 0x80000000u, 0u, false, true, &services, 0x80001004u); }
    catch (GprSpanFault f) { faulted = f.pc == 0x80001004u; }
    passed &= expect(faulted && order.calls == 1u && ram[0] == std::byte{0xC0} &&
        ram[1] == std::byte{0x20} && ram[4] == std::byte{0xCC},
        "PSQ partial store preserves first lane and notification before the exact second-lane fault");
    galaxy::psq_load(&context, 7u, &memory, 0x80000000u, 0u, true, true, &services, 0x80001000u);
    passed &= expect(context.fpr_bits[7] == wide[1] && context.ps1_bits[7] == 0x3FF0000000000000ull,
        "PSQ one-element admission does not require a mapped second lane");
    struct DevicePair {
        std::uint32_t reads{}, writes{};
        std::array<std::uint32_t, 2> read_addresses{}, write_addresses{}, written{};
    } device;
    memory = {};
    memory.user = &device;
    memory.read_device = [](void* user, std::uint32_t address, std::uint32_t size, std::byte* bytes) {
        auto& probe = *static_cast<DevicePair*>(user);
        if (size != 4u || probe.reads >= 2u) return false;
        probe.read_addresses[probe.reads] = address;
        const auto bits = probe.reads++ == 0u ? 0x3FC00000u : 0xC0200000u;
        for (std::uint32_t byte = 0u; byte < 4u; ++byte)
            bytes[byte] = static_cast<std::byte>(bits >> ((3u-byte)*8u));
        return true;
    };
    memory.write_device = [](void* user, std::uint32_t address, std::uint32_t size, const std::byte* bytes) {
        auto& probe = *static_cast<DevicePair*>(user);
        if (size != 4u || probe.writes >= 2u) return false;
        probe.write_addresses[probe.writes] = address;
        std::uint32_t bits{};
        for (std::uint32_t byte = 0u; byte < 4u; ++byte)
            bits = (bits << 8u) | static_cast<std::uint8_t>(bytes[byte]);
        probe.written[probe.writes++] = bits;
        return true;
    };
    for (auto address : {0xCC008000u, 0x817FFFFCu, 0xFFFFFFFCu}) {
        for (bool one : {false, true}) {
            device = {};
            galaxy::psq_load(&context, 7u, &memory, address, 0u, one, true, &services, 0x80001000u);
            galaxy::psq_store(&context, 7u, &memory, address, 0u, one, true, &services, 0x80001004u);
            passed &= expect(device.reads == (one ? 1u : 2u) && device.writes == device.reads &&
                device.read_addresses[0] == address && device.write_addresses[0] == address &&
                device.written[0] == 0x3FC00000u &&
                (one || (device.read_addresses[1] == address + 4u &&
                 device.write_addresses[1] == address + 4u && device.written[1] == 0xC0200000u)),
                "PSQ device, RAM-end and address-wrap paths retain separate ordered 4-byte accesses");
        }
    }
    for (auto type : {1u, 2u, 3u}) {
        device = {};
        context.gqr[0] = type << 16u;
        faulted = false;
        try { galaxy::psq_load(&context, 7u, &memory, 0x80000000u, 0u, false, true, &services, 0x80001000u); }
        catch (GprSpanFault f) { faulted = f.pc == 0x80001000u; }
        passed &= expect(faulted && device.reads == 0u, "PSQ reserved load type faults before either lane access");
    }
    return passed;
}

// Independent integer field oracle: quantized values are exact binary64
// numbers throughout the GQR scale range. No native conversion/load helper or
// host floating-point operation supplies the expected result.
std::uint64_t expected_psq_integer_fields(std::int32_t value, std::int32_t scale) {
    if (value == 0) return 0u;
    const auto magnitude = static_cast<std::uint32_t>(value < 0 ? -value : value);
    std::uint32_t highest{};
    for (auto remaining = magnitude; remaining > 1u; remaining >>= 1u) ++highest;
    const auto exponent = static_cast<std::uint64_t>(1023 + static_cast<std::int32_t>(highest) - scale);
    const auto fraction = static_cast<std::uint64_t>(magnitude - (1u << highest)) << (52u - highest);
    return (value < 0 ? 0x8000000000000000ull : 0ull) | (exponent << 52u) | fraction;
}

bool test_psq_load_quantization_and_mode_contract() {
    bool passed = true;
    std::array<std::byte, 64> ram{};
    ram[31] = std::byte{0xFF}; ram[32] = std::byte{0x80};
    ram[33] = std::byte{0x00}; ram[34] = std::byte{0x7F};
    const auto original_ram = ram;
    galaxy::PpcContext seed{};
    for (std::uint32_t index = 0u; index < 32u; ++index) {
        seed.gpr[index] = 0x13570000u + index;
        seed.fpr_bits[index] = 0x4012000000000000ull + index;
        seed.ps1_bits[index] = 0xC014000000000000ull + index;
    }
    for (std::uint32_t index = 0u; index < 8u; ++index) seed.gqr[index] = 0x12345678u + index;
    seed.cr = 0xABCDEF01u; seed.fpscr = 0xB2C3D4E5u;
    seed.lr = 0x80002000u; seed.ctr = 0x12345678u;
    seed.hid2 = 0xA0000000u; seed.pc = 0x80001000u;
    for (auto alias : {0u, 0x80000000u, 0xC0000000u,
                       0x10000000u, 0x90000000u, 0xD0000000u}) {
        galaxy::GuestMemoryRegionV1 region{alias, 64u, ram.data()};
        for (bool fast : {false, true}) {
            galaxy::GuestMemoryV1 memory{};
            memory.regions = &region; memory.region_count = 1u;
            if (fast) memory.fast_regions[alias >> 28u] = {64u, ram.data()};
            for (const auto type : {4u, 5u, 6u, 7u}) {
                const std::int32_t first = type == 4u ? 255 : type == 5u ? 65408 : type == 6u ? -1 : -128;
                const std::int32_t second = type == 4u ? 128 : type == 5u ? 127 : type == 6u ? -128 : 127;
                for (std::uint32_t encoded_scale = 0u; encoded_scale < 64u; ++encoded_scale) {
                    const auto scale = encoded_scale < 32u ? static_cast<std::int32_t>(encoded_scale)
                        : static_cast<std::int32_t>(encoded_scale) - 64;
                    for (bool one : {false, true}) {
                        for (bool d_form : {false, true}) {
                            for (const auto target : {0u, 7u, 31u}) {
                                auto context = seed;
                                context.hid2 = d_form ? 0xA0000000u : 0x20000000u;
                                context.gqr[5] = (encoded_scale << 24u) | (type << 16u) | 0xF0FFu;
                                auto expected = context;
                                expected.fpr_bits[target] = expected_psq_integer_fields(first, scale);
                                expected.ps1_bits[target] = one ? 0x3FF0000000000000ull
                                    : expected_psq_integer_fields(second, scale);
                                galaxy::psq_load(&context, target, &memory, alias + 31u, 5u, one, d_form,
                                    nullptr, 0x80001000u);
                                passed &= expect(std::memcmp(&context, &expected, sizeof(context)) == 0 && ram == original_ram,
                                    "PSQ integer loads preserve all other guest state, both forms, all signed scales and six RAM aliases");
                            }
                        }
                    }
                }
            }
        }
    }
    std::uint32_t device_reads{};
    galaxy::GuestMemoryV1 device{};
    device.user = &device_reads;
    device.read_device = [](void* user, std::uint32_t, std::uint32_t, std::byte*) {
        ++*static_cast<std::uint32_t*>(user);
        return false;
    };
    galaxy::NativeServicesV1 services{};
    services.fatal = throw_gpr_span_fault;
    for (auto hid2 : {0u, 0x80000000u, 0x20000000u, 0xA0000000u}) {
        for (bool d_form : {false, true}) {
            const bool mode_valid = (hid2 & 0x20000000u) != 0u && (!d_form || (hid2 & 0x80000000u) != 0u);
            for (auto type : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u}) {
                if (mode_valid && (type == 0u || type >= 4u)) continue;
                for (std::uint32_t scale = 0u; scale < 64u; ++scale) {
                    auto context = seed;
                    context.hid2 = hid2;
                    context.gqr[5] = (scale << 24u) | (type << 16u) | 0xF0FFu;
                    const auto expected = context;
                    device_reads = 0u;
                    bool faulted = false;
                    try { galaxy::psq_load(&context, 31u, &device, 0xCC008000u, 5u, false, d_form,
                        &services, 0x80001000u); }
                    catch (GprSpanFault fault) { faulted = fault.pc == 0x80001000u; }
                    passed &= expect(faulted && device_reads == 0u && std::memcmp(&context, &expected, sizeof(context)) == 0,
                        "PSQ disabled modes and reserved types fault at the exact instruction before any device read or guest-state change");
                }
            }
        }
    }
    return passed;
}

bool test_normal_f32_widening_fields() {
    bool passed = true;
    // IEEE binary32 normals are exactly representable in binary64. Exercise
    // every signed exponent and fraction boundary using a separate field oracle.
    for (const auto sign : {0u, 0x80000000u}) {
        for (std::uint32_t exponent = 1u; exponent < 255u; ++exponent) {
            for (const auto fraction : {0u,1u,0x123456u,0x3FFFFFu,0x400000u,0x7FFFFEu,0x7FFFFFu}) {
                const auto bits = sign | (exponent << 23u) | fraction;
                const auto expected = (static_cast<std::uint64_t>(sign) << 32u) |
                    (static_cast<std::uint64_t>(exponent + 896u) << 52u) |
                    (static_cast<std::uint64_t>(fraction) << 29u);
                passed &= expect(galaxy::widen_f32_bits(bits) == expected,
                                 "normal single widening preserves every signed exponent and fraction boundary");
            }
        }
    }
    for (const auto sign : {0u, 0x80000000u}) {
        passed &= expect(galaxy::widen_f32_bits(sign) == (static_cast<std::uint64_t>(sign) << 32u),
                         "single widening preserves signed zero");
        passed &= expect(galaxy::widen_f32_bits(sign | 0x7F800001u) ==
                         ((static_cast<std::uint64_t>(sign) << 32u) | 0x7FF0000020000000ull),
                         "single widening preserves signaling NaN payload without host arithmetic");
        passed &= expect(galaxy::widen_f32_bits(sign | 1u) ==
                         ((static_cast<std::uint64_t>(sign) << 32u) | 0x36A0000000000000ull),
                         "single widening preserves the minimum subnormal");
    }
    return passed;
}

struct SinglePrecisionFault {
    std::uint32_t pc;
    std::string_view message;
};

void throw_single_precision_fault(void*, std::uint32_t pc, const char* message) {
    throw SinglePrecisionFault{pc, message};
}

bool test_single_precision_operand_proof() {
    galaxy::NativeServicesV1 services{};
    services.fatal = throw_single_precision_fault;
    constexpr std::uint32_t pc = 0x80001234u;
    const auto valid = [&](std::uint32_t bits) {
        // Hardware binary32-to-binary64 widening is exact for every finite
        // input; this oracle does not use the runtime's integer proof.
        const auto wide = std::bit_cast<std::uint64_t>(
            static_cast<double>(std::bit_cast<float>(bits)));
        return expect(galaxy::require_single_precision_bits(wide, &services, pc) == bits,
                      "single operand proof preserves exact finite host widening");
    };
    const auto fault = [&](std::uint64_t bits, std::string_view message) {
        try {
            static_cast<void>(galaxy::require_single_precision_bits(bits, &services, pc));
        } catch (const SinglePrecisionFault& f) {
            return expect(f.pc == pc && f.message == message,
                          "non-single operand preserves precise fault PC and original message");
        }
        return expect(false, "non-single operand must fault");
    };
    constexpr std::string_view non_single =
        "single-precision instruction received a non-single operand";
    bool passed = true;
    for (auto sign : {0u, 0x80000000u}) {
        for (std::uint32_t exponent = 0u; exponent < 255u; ++exponent) {
            for (auto fraction : {0u, 1u, 0x003FFFFFu, 0x00400000u, 0x007FFFFEu, 0x007FFFFFu}) {
                const auto bits = sign | (exponent << 23u) | fraction;
                passed &= valid(bits);
                if (exponent != 0u) {
                    const auto wide = std::bit_cast<std::uint64_t>(
                        static_cast<double>(std::bit_cast<float>(bits)));
                    // Every discarded bit independently makes the value
                    // non-single. Neither sign nor a binade boundary exempts it.
                    for (unsigned low_bit = 0u; low_bit < 29u; ++low_bit) {
                        passed &= fault(wide | (std::uint64_t{1u} << low_bit), non_single);
                    }
                }
            }
        }
    }
    std::uint32_t random = 0x53475052u;
    for (unsigned i = 0u; i < 65536u; ++i) {
        random ^= random << 13u; random ^= random >> 17u; random ^= random << 5u;
        if ((random & 0x7F800000u) != 0x7F800000u) passed &= valid(random);
    }
    // IEEE special encodings use literal payload oracles, avoiding a host
    // conversion that could quiet signaling NaNs or discard their payload.
    for (auto sign : {0u, 0x80000000u}) {
        for (const auto& special : std::array<std::array<std::uint64_t, 2>, 4>{{
                 {0x7FF0000000000000ull, 0x7F800000u},
                 {0x7FF0000020000000ull, 0x7F800001u},
                 {0x7FF8000000000000ull, 0x7FC00000u},
                 {0x7FFFFFFFE0000000ull, 0x7FFFFFFFu}}}) {
            const auto wide = special[0] | (static_cast<std::uint64_t>(sign) << 32u);
            passed &= expect(galaxy::require_single_precision_bits(wide, &services, pc) ==
                                 (static_cast<std::uint32_t>(special[1]) | sign),
                             "single operand fallback preserves infinity and NaN sign/payload");
        }
    }
    for (auto wide : {0x380FFFFFFFFFFFFFull, 0x47EFFFFFF0000001ull,
                     0x47F0000000000000ull, 0x7FF0000000000001ull}) {
        passed &= fault(wide, non_single);
        passed &= fault(wide | 0x8000000000000000ull, non_single);
    }
    passed &= fault(1u, "undefined PowerPC single-precision store conversion");
    passed &= fault(0x8000000000000001ull,
                    "undefined PowerPC single-precision store conversion");
    return passed;
}

}  // namespace

int main() {
    std::array<std::byte, 0x2800> backing{};
    std::array<std::byte, 0x20> sda2_backing{};
    std::array<std::byte, 0x100> sda_backing{};
    std::array<std::byte, 0x100> jpa_table_backing{};
    std::array<galaxy::GuestMemoryRegionV1, 4> regions{{
        {
            .guest_base = 0x80000000u,
            .size = static_cast<std::uint32_t>(backing.size()),
            .host_base = backing.data(),
        },
        {
            .guest_base = 0x8069E140u,
            .size = static_cast<std::uint32_t>(sda2_backing.size()),
            .host_base = sda2_backing.data(),
        },
        {
            .guest_base = 0x806A2400u,
            .size = static_cast<std::uint32_t>(sda_backing.size()),
            .host_base = sda_backing.data(),
        },
        {
            .guest_base = 0x805C7220u,
            .size = static_cast<std::uint32_t>(jpa_table_backing.size()),
            .host_base = jpa_table_backing.data(),
        },
    }};
    galaxy::GuestMemoryV1 memory{};
    memory.region_count = static_cast<std::uint32_t>(regions.size());
    memory.regions = regions.data();
    memory.fast_regions[8] = {
        0x40u,
        backing.data(),
    };
    DeviceProbe device_probe{};
    memory.user = &device_probe;
    memory.read_device = &probe_read;
    memory.write_device = &probe_write;

    bool passed = true;
    LogProbe jutvideo_log_probe{};
    galaxy::NativeServicesV1 jutvideo_trace_services{};
    jutvideo_trace_services.user = &jutvideo_log_probe;
    jutvideo_trace_services.log = &capture_log_message;
    galaxy::PpcContext jutvideo_trace_context{};
    jutvideo_trace_context.gpr[3] = 0x809AE098u;
    jutvideo_trace_context.lr = 0x8039D3E0u;
    _putenv_s("GALAXY_TRACE_JUTVIDEO_MQ", "1");
    galaxy::trace_jutvideo_mq_function_entry(
        &jutvideo_trace_services,
        &jutvideo_trace_context,
        &memory,
        0x804A88E4u);
    passed &= expect(
        jutvideo_log_probe.calls == 1u &&
            std::strcmp(
                jutvideo_log_probe.message.data(),
                "__galaxy_jutvideo_mq_entry:804A88E4:8039D3E0") == 0,
        "JUTVideo queue trace preserves the exact verified caller LR");
    jutvideo_trace_context.gpr[3] = 0x00000000u;
    jutvideo_trace_context.lr = 0x8039FEFCu;
    galaxy::trace_jutvideo_mq_function_entry(
        &jutvideo_trace_services,
        &jutvideo_trace_context,
        nullptr,
        0x80419554u);
    passed &= expect(
        jutvideo_log_probe.calls == 2u &&
            std::strcmp(
                jutvideo_log_probe.message.data(),
                "__galaxy_jutvideo_mq_entry:80419554:8039FEFC") == 0,
        "JUTVideo producer trace preserves its dynamic callback caller LR");
    jutvideo_trace_context.gpr[3] = 0x809AE0D8u;
    galaxy::trace_jutvideo_mq_function_entry(
        &jutvideo_trace_services,
        &jutvideo_trace_context,
        nullptr,
        0x804A89ACu);
    passed &= expect(
        jutvideo_log_probe.calls == 2u,
        "JUTVideo queue trace rejects unrelated OS message queues");

    // Main-frame markers must not enter the runtime log path before the
    // requested VI trace window. This keeps a targeted trace from perturbing
    // real-time audio/video scheduling during its long pre-window route.
    LogProbe main_frame_log_probe{};
    std::atomic_bool main_frame_trace_active{false};
    galaxy::NativeServicesV1 main_frame_trace_services{};
    main_frame_trace_services.user = &main_frame_log_probe;
    main_frame_trace_services.log = &capture_log_message;
    main_frame_trace_services.main_frame_trace_active =
        &main_frame_trace_active;
    _putenv_s("GALAXY_TRACE_MAIN_FRAME", "1");
    galaxy::trace_main_frame_stage(
        &main_frame_trace_services,
        "__galaxy_main_frame_stage:test-pre-window");
    passed &= expect(
        main_frame_log_probe.calls == 0u,
        "main-frame trace does not call the log transport before its VI window");
    main_frame_trace_active.store(true, std::memory_order_relaxed);
    galaxy::trace_main_frame_stage(
        &main_frame_trace_services,
        "__galaxy_main_frame_stage:test-active-window");
    passed &= expect(
        main_frame_log_probe.calls == 1u &&
            std::strcmp(
                main_frame_log_probe.message.data(),
                "__galaxy_main_frame_stage:test-active-window") == 0,
        "main-frame trace emits its exact stage marker inside its VI window");
    galaxy::trace_main_frame_function_entry(
        &main_frame_trace_services, 0x80399AF0u);
    passed &= expect(
        main_frame_log_probe.calls == 2u &&
            std::strcmp(
                main_frame_log_probe.message.data(),
                "__galaxy_main_frame_entry:80399AF0") == 0,
        "main-frame entry trace emits only inside its VI window");
    main_frame_trace_services.main_frame_trace_active = nullptr;
    galaxy::trace_main_frame_stage(
        &main_frame_trace_services,
        "__galaxy_main_frame_stage:test-missing-gate");
    passed &= expect(
        main_frame_log_probe.calls == 2u,
        "main-frame trace requires a runtime-owned active-window gate");

    // Generated direct-call observation is selected entirely by the runtime
    // service table. The hot helper must neither read process environment nor
    // broaden the host boundary beyond the finite audited entry set.
    passed &= expect(
        _putenv_s("GALAXY_PROFILE_GENERATED_DIRECT_CALLS", "0") == 0,
        "generated direct-call observation test disables the broad profiler");
    constexpr std::array<std::uint32_t, 11>
        generated_direct_observation_entries{
            0x802AFD80u,
            0x802B0D0Cu,
            0x802B0E18u,
            0x802B0FE4u,
            0x802B2A28u,
            0x802B321Cu,
            0x802CAC98u,
            0x802CAF9Cu,
            0x8038D2E4u,
            0x8038D34Cu,
            0x8038D868u,
        };
    constexpr std::array<std::uint32_t, 3>
        generated_direct_input_anchor_entries{
            0x802B0D0Cu,
            0x802B0E18u,
            0x802B0FE4u,
        };
    constexpr std::uint32_t generated_direct_observation_flags =
        galaxy::kNativeServiceFlagTraceRouteMarkers |
        galaxy::kNativeServiceFlagRelativeMarioInputAnchor;
    constexpr std::uint32_t preexisting_service_flags =
        galaxy::kNativeServiceFlagTraceWpadReadCopies |
        galaxy::kNativeServiceFlagAdaptiveCallReturnCheckpoints;
    passed &= expect(
        galaxy::kNativeServiceFlagTraceRouteMarkers != 0u &&
            galaxy::kNativeServiceFlagRelativeMarioInputAnchor != 0u &&
            (galaxy::kNativeServiceFlagTraceRouteMarkers &
             galaxy::kNativeServiceFlagRelativeMarioInputAnchor) == 0u &&
            (generated_direct_observation_flags &
             preexisting_service_flags) == 0u,
        "generated direct-call observation flags are nonzero and nonoverlapping");
    for (const std::uint32_t target : generated_direct_observation_entries) {
        passed &= expect(
            galaxy::is_route_marker_generated_direct_entry(target) &&
                !galaxy::is_route_marker_generated_direct_entry(target - 4u) &&
                !galaxy::is_route_marker_generated_direct_entry(target + 4u),
            "generated direct-call observation accepts only each exact audited entry");
    }
    for (const std::uint32_t target : generated_direct_input_anchor_entries) {
        passed &= expect(
            galaxy::is_mario_control_generated_direct_entry(target) &&
                !galaxy::is_mario_control_generated_direct_entry(target - 4u) &&
                !galaxy::is_mario_control_generated_direct_entry(target + 4u),
            "generated direct input anchoring accepts only each exact Mario-control entry");
    }
    const auto exercise_direct_resolved =
        [&](std::uint32_t runtime_flags,
            std::uint32_t target,
            bool expect_service,
            const char* message) {
            DirectResolvedCallProbe probe{};
            galaxy::NativeServicesV1 direct_services{};
            direct_services.user = &probe;
            direct_services.call_guest_resolved =
                &capture_direct_resolved_call;
            direct_services.runtime_flags = runtime_flags;
            probe.services = &direct_services;
            galaxy::PpcContext direct_context{};
            direct_context.pc = 0x80001234u;
            galaxy::call_guest_direct_resolved(
                &direct_services,
                target,
                &capture_direct_resolved_function,
                &direct_context,
                &memory,
                0x80005678u);
            passed &= expect(
                probe.service_calls == (expect_service ? 1u : 0u) &&
                    probe.native_calls == 1u &&
                    probe.caller_visible_pc == target &&
                    direct_context.pc == target &&
                    (!expect_service || probe.guest_address == target),
                message);
        };
    passed &= expect(
        _putenv_s("GALAXY_TRACE_ROUTE_MARKERS", "1") == 0 &&
            _putenv_s(
                "GALAXY_INPUT_STICK_SCRIPT_RELATIVE_MARIO_CONTROL", "1") ==
                0,
        "legacy route environment variables are present for service-table isolation test");
    exercise_direct_resolved(
        0u,
        generated_direct_input_anchor_entries.front(),
        false,
        "route environment variables cannot bypass a zero runtime service table");
    for (const std::uint32_t target : generated_direct_observation_entries) {
        exercise_direct_resolved(
            galaxy::kNativeServiceFlagTraceRouteMarkers,
            target,
            true,
            "route tracing crosses every exact audited route entry once");
        exercise_direct_resolved(
            generated_direct_observation_flags,
            target,
            true,
            "combined route and input observation crosses every route entry once");
    }
    for (const std::uint32_t target :
         generated_direct_input_anchor_entries) {
        exercise_direct_resolved(
            galaxy::kNativeServiceFlagRelativeMarioInputAnchor,
            target,
            true,
            "relative input anchoring crosses each exact Mario-control entry once");
    }
    for (const std::uint32_t target : generated_direct_observation_entries) {
        if (galaxy::is_mario_control_generated_direct_entry(target)) {
            continue;
        }
        exercise_direct_resolved(
            galaxy::kNativeServiceFlagRelativeMarioInputAnchor,
            target,
            false,
            "relative input anchoring leaves unrelated route entries in-module");
    }
    {
        DirectResolvedCallProbe probe{};
        galaxy::NativeServicesV1 direct_services{};
        direct_services.user = &probe;
        direct_services.call_guest_resolved = &capture_direct_resolved_call;
        direct_services.runtime_flags =
            galaxy::kNativeServiceFlagRelativeMarioInputAnchor;
        probe.services = &direct_services;
        galaxy::PpcContext direct_context{};
        const std::uint32_t target =
            generated_direct_input_anchor_entries.front();
        galaxy::call_guest_direct_resolved(
            &direct_services,
            target,
            &capture_direct_resolved_function,
            &direct_context,
            &memory,
            0x80005678u);
        direct_services.runtime_flags &=
            ~galaxy::kNativeServiceFlagRelativeMarioInputAnchor;
        galaxy::call_guest_direct_resolved(
            &direct_services,
            target,
            &capture_direct_resolved_function,
            &direct_context,
            &memory,
            0x80005678u);
        passed &= expect(
            probe.service_calls == 1u && probe.native_calls == 2u,
            "clearing the one-shot relative anchor removes every later Mario-control host boundary");
    }
    {
        DirectResolvedCallProbe probe{};
        galaxy::NativeServicesV1 direct_services{};
        direct_services.user = &probe;
        direct_services.call_guest_resolved = &capture_direct_resolved_call;
        direct_services.runtime_flags = generated_direct_observation_flags;
        probe.services = &direct_services;
        galaxy::PpcContext direct_context{};
        const std::uint32_t target =
            generated_direct_input_anchor_entries.front();
        galaxy::call_guest_direct_resolved(
            &direct_services,
            target,
            &capture_direct_resolved_function,
            &direct_context,
            &memory,
            0x80005678u);
        direct_services.runtime_flags &=
            ~galaxy::kNativeServiceFlagRelativeMarioInputAnchor;
        galaxy::call_guest_direct_resolved(
            &direct_services,
            target,
            &capture_direct_resolved_function,
            &direct_context,
            &memory,
            0x80005678u);
        passed &= expect(
            probe.service_calls == 2u && probe.native_calls == 2u &&
                direct_services.runtime_flags ==
                    galaxy::kNativeServiceFlagTraceRouteMarkers,
            "clearing the one-shot relative anchor preserves independent broad route tracing");
    }
    exercise_direct_resolved(
        preexisting_service_flags | 1u | (1u << 31u),
        generated_direct_observation_entries.front(),
        false,
        "unrelated runtime flags cannot enable generated direct-call observation");
    exercise_direct_resolved(
        generated_direct_observation_flags,
        generated_direct_observation_entries.front() + 4u,
        false,
        "an adjacent direct-call target cannot cross the route observation boundary");
    exercise_direct_resolved(
        generated_direct_observation_flags,
        0x804ACF08u,
        false,
        "an unmarked direct-call target remains a native in-module call");
    passed &= expect(
        _putenv_s("GALAXY_TRACE_ROUTE_MARKERS", "0") == 0 &&
            _putenv_s(
                "GALAXY_INPUT_STICK_SCRIPT_RELATIVE_MARIO_CONTROL", "0") ==
                0,
        "generated direct-call observation test restores route environment variables");

    // Direct-edge samples must cross the native ABI.  Header-local profile
    // tables in a generated DLL are not visible to the runtime executable that
    // writes the deferred summary, so accept no local fallback here.
    DirectEdgeProfileProbe direct_edge_profile_probe{};
    galaxy::NativeServicesV1 direct_edge_profile_services{};
    direct_edge_profile_services.user = &direct_edge_profile_probe;
    direct_edge_profile_services.record_direct_edge_profile =
        &capture_direct_edge_profile;
    galaxy::report_direct_edge_profile(
        &direct_edge_profile_services,
        0x80344894u,
        0x802617B4u,
        123456u);
    passed &= expect(
        direct_edge_profile_probe.calls == 1u &&
            direct_edge_profile_probe.caller_pc == 0x80344894u &&
            direct_edge_profile_probe.callee_pc == 0x802617B4u &&
            direct_edge_profile_probe.cycles == 123456u,
        "direct-edge profiling reports samples through the runtime-owned service");

    // Full generated modules lower their resolved PPC calls through the
    // cacheable service boundary, so the profiler must sample that path rather
    // than only the direct-resolved helper.  Enable the cached environment
    // predicate before its first use and hold the runtime's main-frame gate
    // open for this isolated ABI test.
    passed &= expect(
        _putenv_s("GALAXY_PROFILE_GENERATED_DIRECT_EDGES", "1") == 0,
        "direct-edge profiler test enables its explicit process environment");
    std::atomic_bool cached_edge_profile_active{false};
    CachedEdgeProfileProbe cached_edge_profile_probe{};
    galaxy::NativeServicesV1 cached_edge_profile_services{};
    cached_edge_profile_services.user = &cached_edge_profile_probe;
    cached_edge_profile_services.call_guest_cached = &capture_cached_edge_call;
    cached_edge_profile_services.record_direct_edge_profile =
        &capture_cached_edge_profile;
    cached_edge_profile_services.main_frame_trace_active =
        &cached_edge_profile_active;
    std::uint32_t cached_edge_target = 0u;
    galaxy::NativeGameFunction cached_edge_function = nullptr;
    galaxy::PpcContext cached_edge_context{};
    galaxy::call_guest_cached(
        &cached_edge_profile_services,
        0x802617B4u,
        &cached_edge_target,
        &cached_edge_function,
        &cached_edge_context,
        &memory,
        0x80344894u);
    passed &= expect(
        cached_edge_profile_probe.cached.calls == 1u &&
            cached_edge_profile_probe.cached.guest_address == 0x802617B4u &&
            cached_edge_context.pc == 0x802617B4u,
        "cached guest-call profile test reaches the generated-call boundary");
    passed &= expect(
        cached_edge_profile_probe.edge.calls == 0u,
        "cached generated calls do not profile before the explicit VI window");

    cached_edge_profile_active.store(true, std::memory_order_relaxed);
    galaxy::call_guest_cached(
        &cached_edge_profile_services,
        0x802617B4u,
        &cached_edge_target,
        &cached_edge_function,
        &cached_edge_context,
        &memory,
        0x80344894u);
    passed &= expect(
        cached_edge_profile_probe.cached.calls == 2u &&
            cached_edge_context.pc == 0x802617B4u,
        "cached guest-call profile test reaches the generated-call boundary in its VI window");
    passed &= expect(
        cached_edge_profile_probe.edge.calls == 1u &&
            cached_edge_profile_probe.edge.caller_pc == 0x80344894u &&
            cached_edge_profile_probe.edge.callee_pc == 0x802617B4u,
        "cached generated calls report exact edges only through the runtime-owned profiler window");

    // The compact closure-profile checkpoint helper uses the same bounded
    // runtime-owned sink while retaining the ordinary checkpoint when the
    // window is closed. Its F-prefixed pseudo target is not a guest address.
    std::atomic_uint32_t checkpoint_profile_pending{0u};
    cached_edge_profile_services.branch_checkpoint =
        &capture_branch_checkpoint;
    cached_edge_profile_services.pending_event_mask =
        &checkpoint_profile_pending;
    cached_edge_profile_active.store(false, std::memory_order_relaxed);
    galaxy::crcp(
        &cached_edge_profile_services,
        0x804F2914u,
        0x804F2918u,
        &cached_edge_context,
        &memory);
    passed &= expect(
        cached_edge_profile_probe.edge.calls == 1u,
        "closure checkpoint profiling stays inactive outside the explicit VI window");
    cached_edge_profile_active.store(true, std::memory_order_relaxed);
    galaxy::crcp(
        &cached_edge_profile_services,
        0x804F2914u,
        0x804F2918u,
        &cached_edge_context,
        &memory);
    passed &= expect(
        cached_edge_profile_probe.edge.calls == 2u &&
            cached_edge_profile_probe.edge.caller_pc == 0x804F2914u &&
            cached_edge_profile_probe.edge.callee_pc == 0xF04F2918u &&
            cached_edge_profile_probe.edge.cycles >=
                galaxy::kCallReturnCheckpointProfileRankBias,
        "closure checkpoint profiling reports an exact non-guest pseudo edge only inside the VI window");

    // Bit zero was the retired fast-cache flag.  Even a populated cache must
    // reach the service callback: that is the sole owner of deadline servicing
    // and the exception-intercept decision.
    CachedCallProbe retired_fast_cache_probe{};
    galaxy::NativeServicesV1 retired_fast_cache_services{};
    retired_fast_cache_services.user = &retired_fast_cache_probe;
    retired_fast_cache_services.call_guest_cached = &capture_cached_call;
    retired_fast_cache_services.runtime_flags = 1u;
    std::uint32_t retired_fast_cache_target = 0x80261B70u;
    galaxy::NativeGameFunction retired_fast_cache_function =
        &capture_direct_cached_function;
    galaxy::PpcContext retired_fast_cache_context{};
    galaxy::call_guest_cached(
        &retired_fast_cache_services,
        retired_fast_cache_target,
        &retired_fast_cache_target,
        &retired_fast_cache_function,
        &retired_fast_cache_context,
        &memory,
        0x80443420u);
    passed &= expect(
        retired_fast_cache_probe.calls == 1u &&
            !retired_fast_cache_probe.direct_cached_function_invoked &&
            retired_fast_cache_context.pc == retired_fast_cache_target,
        "retired fast-cache flag cannot bypass the native cached-call service");

    galaxy::guest_store_u16(&memory, 0x80000000u, 0x1234u, nullptr, 0);
    galaxy::guest_store_u32(&memory, 0x80000002u, 0x89ABCDEFu, nullptr, 0);
    galaxy::guest_store_u64(
        &memory, 0x80000008u, 0x0123456789ABCDEFull, nullptr, 0);
    passed &= expect(backing[0] == std::byte{0x12} && backing[1] == std::byte{0x34},
                     "16-bit stores are big endian");
    passed &= expect(galaxy::guest_load_u16(&memory, 0x80000000u, nullptr, 0) == 0x1234u,
                     "16-bit load round trip");
    passed &= expect(galaxy::guest_load_u32(&memory, 0x80000002u, nullptr, 0) ==
                         0x89ABCDEFu,
                     "32-bit load round trip");
    passed &= expect(galaxy::guest_load_u64(&memory, 0x80000008u, nullptr, 0) ==
                         0x0123456789ABCDEFull,
                     "64-bit load round trip");
    passed &= expect(
        device_probe.reads == 0 && device_probe.writes == 0,
        "direct RAM accesses bypass the MMIO callbacks");

    constexpr std::uint32_t memset_dst = 0x80000200u;
    galaxy::PpcContext memset_context{};
    memset_context.gpr[3] = memset_dst;
    memset_context.gpr[4] = 0xA5u;
    memset_context.gpr[5] = 17u;
    galaxy::native_memset_80004388(
        &memset_context, &memory, nullptr, 0x80004388u);
    bool memset_ok = true;
    for (std::uint32_t index = 0; index < 17u; ++index) {
        memset_ok &= backing[(memset_dst - 0x80000000u) + index] == std::byte{0xA5};
    }
    passed &= expect(memset_ok, "native memset fills guest bytes");

    constexpr std::uint32_t resource_base = 0x80000240u;
    constexpr std::uint32_t resource_owner = 0x800002A0u;
    constexpr std::uint32_t resource_entries = 0x80000300u;
    galaxy::guest_store_u32(&memory, resource_base + 0x44u, resource_owner, nullptr, 0);
    galaxy::guest_store_u32(&memory, resource_base + 0x4Cu, resource_entries, nullptr, 0);
    galaxy::guest_store_u32(&memory, resource_owner + 0x08u, 4u, nullptr, 0);
    galaxy::guest_store_u32(&memory, resource_entries + 0x04u, 0x00000000u, nullptr, 0);
    galaxy::guest_store_u32(&memory, resource_entries + 0x18u, 0x01000000u, nullptr, 0);
    galaxy::guest_store_u32(&memory, resource_entries + 0x2Cu, 0x01000000u, nullptr, 0);
    galaxy::guest_store_u32(&memory, resource_entries + 0x40u, 0x00000000u, nullptr, 0);
    galaxy::PpcContext resource_count_context{};
    resource_count_context.gpr[3] = resource_base;
    galaxy::native_resource_entry_count_8040FC80(
        &resource_count_context, &memory, nullptr, 0x8040FC80u);
    passed &= expect(resource_count_context.gpr[3] == 2u,
                     "native resource-entry count matches flag scan");
    passed &= expect(resource_count_context.gpr[4] == 0x50u &&
                         resource_count_context.gpr[5] == resource_entries + 0x3Cu &&
                         resource_count_context.gpr[6] == 2u &&
                         resource_count_context.ctr == 0u,
                     "native resource-entry count preserves loop tail registers");

    constexpr std::uint32_t strlen_text = 0x80000380u;
    const char* native_strlen_text = "Galaxy";
    std::memcpy(
        backing.data() + (strlen_text - 0x80000000u),
        native_strlen_text,
        std::strlen(native_strlen_text) + 1u);
    galaxy::PpcContext strlen_context{};
    strlen_context.gpr[3] = strlen_text;
    galaxy::native_strlen_80516E80(&strlen_context, &memory, nullptr, 0x80516E80u);
    passed &= expect(strlen_context.gpr[3] == 6u &&
                         strlen_context.gpr[4] == strlen_text + 6u &&
                         strlen_context.gpr[0] == 0u,
                     "native strlen reports length and terminal pointer");

    constexpr std::uint32_t yaz0_src = 0x80000300u;
    constexpr std::uint32_t yaz0_dst = 0x80000340u;
    backing[yaz0_src - 0x80000000u + 0x00u] = std::byte{'Y'};
    backing[yaz0_src - 0x80000000u + 0x01u] = std::byte{'a'};
    backing[yaz0_src - 0x80000000u + 0x02u] = std::byte{'z'};
    backing[yaz0_src - 0x80000000u + 0x03u] = std::byte{'0'};
    galaxy::guest_store_u32(&memory, yaz0_src + 0x04u, 6u, nullptr, 0);
    backing[yaz0_src - 0x80000000u + 0x10u] = std::byte{0xE0};
    backing[yaz0_src - 0x80000000u + 0x11u] = std::byte{'A'};
    backing[yaz0_src - 0x80000000u + 0x12u] = std::byte{'B'};
    backing[yaz0_src - 0x80000000u + 0x13u] = std::byte{'C'};
    backing[yaz0_src - 0x80000000u + 0x14u] = std::byte{0x10};
    backing[yaz0_src - 0x80000000u + 0x15u] = std::byte{0x02};
    galaxy::PpcContext yaz0_context{};
    yaz0_context.gpr[3] = yaz0_src;
    yaz0_context.gpr[4] = yaz0_dst;
    yaz0_context.gpr[13] = 0x806A4CA0u;
    galaxy::native_yaz0_decode_or_fallback_803989BC(
        &yaz0_context, &memory, nullptr, 0x803989BCu);
    passed &= expect(yaz0_context.gpr[3] == 1u, "native Yaz0 reports success");
    passed &= expect(
        backing[yaz0_dst - 0x80000000u + 0u] == std::byte{'A'} &&
            backing[yaz0_dst - 0x80000000u + 1u] == std::byte{'B'} &&
            backing[yaz0_dst - 0x80000000u + 2u] == std::byte{'C'} &&
            backing[yaz0_dst - 0x80000000u + 3u] == std::byte{'A'} &&
            backing[yaz0_dst - 0x80000000u + 4u] == std::byte{'B'} &&
            backing[yaz0_dst - 0x80000000u + 5u] == std::byte{'C'},
        "native Yaz0 decodes literal and back-reference bytes");

    constexpr std::uint32_t yaz0_dict_src = 0x80000380u;
    constexpr std::uint32_t yaz0_dict_dst = 0x800003C0u;
    backing[yaz0_dict_dst - 0x80000000u - 3u] = std::byte{'X'};
    backing[yaz0_dict_dst - 0x80000000u - 2u] = std::byte{'Y'};
    backing[yaz0_dict_dst - 0x80000000u - 1u] = std::byte{'Z'};
    backing[yaz0_dict_src - 0x80000000u + 0x00u] = std::byte{'Y'};
    backing[yaz0_dict_src - 0x80000000u + 0x01u] = std::byte{'a'};
    backing[yaz0_dict_src - 0x80000000u + 0x02u] = std::byte{'z'};
    backing[yaz0_dict_src - 0x80000000u + 0x03u] = std::byte{'0'};
    galaxy::guest_store_u32(&memory, yaz0_dict_src + 0x04u, 3u, nullptr, 0);
    backing[yaz0_dict_src - 0x80000000u + 0x10u] = std::byte{0x00};
    backing[yaz0_dict_src - 0x80000000u + 0x11u] = std::byte{0x10};
    backing[yaz0_dict_src - 0x80000000u + 0x12u] = std::byte{0x02};
    galaxy::PpcContext yaz0_dict_context{};
    yaz0_dict_context.gpr[3] = yaz0_dict_src;
    yaz0_dict_context.gpr[4] = yaz0_dict_dst;
    yaz0_dict_context.gpr[13] = 0x806A4CA0u;
    galaxy::native_yaz0_decode_or_fallback_803989BC(
        &yaz0_dict_context, &memory, nullptr, 0x803989BCu);
    passed &= expect(
        backing[yaz0_dict_dst - 0x80000000u + 0u] == std::byte{'X'} &&
            backing[yaz0_dict_dst - 0x80000000u + 1u] == std::byte{'Y'} &&
            backing[yaz0_dict_dst - 0x80000000u + 2u] == std::byte{'Z'},
        "native Yaz0 can copy from the existing guest-memory dictionary");

    {
        NotifyProbe notify{};
        memory.user = &notify;
        memory.notify_write = &capture_notify_write;
        const auto reset_notify = [&] {
            notify = {};
        };

        constexpr std::uint32_t vec_src = 0x80000220u;
        constexpr std::uint32_t vec_dst = 0x80000240u;
        for (std::uint32_t i = 0; i < 12u; ++i) {
            backing[(vec_src - 0x80000000u) + i] =
                static_cast<std::byte>(0x30u + i);
        }
        galaxy::PpcContext vec_copy_context{};
        vec_copy_context.msr = galaxy::kMsrFloatingPointAvailable;
        vec_copy_context.gpr[3] = vec_dst;
        vec_copy_context.gpr[4] = vec_src;
        reset_notify();
        galaxy::native_vec_copy_12(
            &vec_copy_context, &memory, nullptr, 0x8001CF64u);
        passed &= expect(
            notify.writes == 3u && notify.address == vec_dst + 8u &&
                notify.size == 4u,
            "native scalar vec copy notifies each original ordered word store");

        galaxy::PpcContext vec_zero_context{};
        vec_zero_context.gpr[3] = vec_dst;
        reset_notify();
        galaxy::native_vec_zero(
            &vec_zero_context, &memory, nullptr, 0x8001D000u);
        passed &= expect(
            notify.writes == 1u && notify.address == vec_dst &&
                notify.size == 12u,
            "native vec zero notifies dirty guest memory");

        galaxy::PpcContext vec_set_context{};
        vec_set_context.gpr[3] = vec_dst;
        vec_set_context.fpr_bits[1] =
            galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(1.0f));
        vec_set_context.fpr_bits[2] =
            galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(2.0f));
        vec_set_context.fpr_bits[3] =
            galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(3.0f));
        reset_notify();
        galaxy::native_vec_set_from_fprs(
            &vec_set_context, &memory, nullptr, 0x8001D010u);
        passed &= expect(
            notify.writes == 1u && notify.address == vec_dst &&
                notify.size == 12u,
            "native vec set notifies dirty guest memory");

        constexpr std::uint32_t mtx_addr = 0x80000280u;
        galaxy::PpcContext mtx_scale_context{};
        mtx_scale_context.msr = galaxy::kMsrFloatingPointAvailable;
        mtx_scale_context.gpr[3] = mtx_addr;
        mtx_scale_context.fpr_bits[1] =
            galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(2.0f));
        mtx_scale_context.fpr_bits[2] =
            galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(3.0f));
        for (std::uint32_t i = 0; i < 11u; ++i) {
            galaxy::guest_store_u32(
                &memory,
                mtx_addr + i * 4u,
                std::bit_cast<std::uint32_t>(1.0f),
                nullptr,
                0);
        }
        reset_notify();
        galaxy::native_mtx_scale_803A387C(
            &mtx_scale_context, &memory, nullptr, 0x803A387Cu);
        passed &= expect(
            notify.writes == 9u && notify.address == mtx_addr + 0x28u &&
                notify.size == 4u,
            "native matrix scale notifies each original ordered word store");

        memory.notify_write = nullptr;
        memory.user = &device_probe;
    }

    {
        std::array<std::atomic_uint64_t, 1> shared_dirty{};
        std::array<std::uint64_t, 1> cpu_dirty{};
        NotifyProbe notify{};
        memory.user = &notify;
        memory.notify_write = &capture_notify_write;
        memory.dirty_page_words = shared_dirty.data();
        memory.dirty_tracked_base = 0u;
        memory.dirty_tracked_size = 0x100u;
        memory.dirty_page_shift = 7u;
        memory.dirty_page_word_count = 1u;
        memory.cpu_dirty_page_words = cpu_dirty.data();
        memory.cpu_dirty_tracked_base = 0u;
        memory.cpu_dirty_tracked_size = 0x100u;
        memory.cpu_dirty_page_shift = 7u;
        memory.cpu_dirty_page_word_count = 1u;

        galaxy::guest_store_u16(
            &memory, 0x8000007Fu, 0x1234u, nullptr, 0u);
        passed &= expect(
            shared_dirty[0].load(std::memory_order_relaxed) == 0x3u &&
                cpu_dirty[0] == 0x3u && notify.writes == 0u,
            "guest store marks independent shared and CPU-owned dirty pages");

        shared_dirty[0].store(0u, std::memory_order_relaxed);
        cpu_dirty[0] = 0u;
        memory.dirty_page_words = nullptr;
        memory.cpu_dirty_tracked_base = 0x10000800u;
        memory.cpu_dirty_tracked_size = 0x100u;
        galaxy::guest_notify_write(&memory, 0xD000087Fu, 2u);
        passed &= expect(
            cpu_dirty[0] == 0x3u && notify.writes == 0u,
            "CPU-owned dirty tracker normalizes MEM2 aliases and whole spans");

        galaxy::guest_notify_write(&memory, 0xD0000900u, 1u);
        passed &= expect(
            notify.writes == 1u && notify.address == 0xD0000900u &&
                notify.size == 1u,
            "write outside both dirty trackers preserves notify fallback");

        memory.notify_write = nullptr;
        memory.dirty_page_words = nullptr;
        memory.dirty_tracked_base = 0u;
        memory.dirty_tracked_size = 0u;
        memory.dirty_page_shift = 0u;
        memory.dirty_page_word_count = 0u;
        memory.cpu_dirty_page_words = nullptr;
        memory.cpu_dirty_tracked_base = 0u;
        memory.cpu_dirty_tracked_size = 0u;
        memory.cpu_dirty_page_shift = 0u;
        memory.cpu_dirty_page_word_count = 0u;
        memory.user = &device_probe;
    }

    {
        // A dirty bit is an invalidation, not a write count. Coalesce repeated
        // stores, preserve independently added bits, and reassert every page
        // after the simulation thread consumes the preceding capture.
        std::array<std::atomic_uint64_t, 4> shared_dirty{};
        std::array<std::uint64_t, 4> cpu_dirty{};
        galaxy::GuestMemoryV1 tracked{};
        tracked.dirty_page_words = shared_dirty.data();
        tracked.dirty_tracked_size = 0x8000u;
        tracked.dirty_page_shift = 7u;
        tracked.dirty_page_word_count = 4u;
        tracked.cpu_dirty_page_words = cpu_dirty.data();
        tracked.cpu_dirty_tracked_size = 0x8000u;
        tracked.cpu_dirty_page_shift = 7u;
        tracked.cpu_dirty_page_word_count = 4u;
        constexpr std::array<std::array<std::uint32_t, 2>, 5> spans{{
            {0u, 4u}, {0x7Fu, 4u}, {0x1FFCu, 8u},
            {0x1FFFu, 0x2100u}, {0x7FFCu, 4u}}};
        for (const auto physical_base : {0u, 0x10000000u}) {
            tracked.dirty_tracked_base = physical_base;
            tracked.cpu_dirty_tracked_base = physical_base;
            for (const auto alias : {0u, 0x80000000u, 0xC0000000u}) {
                for (const auto& span : spans) {
                    for (unsigned capture = 0; capture < 3u; ++capture) {
                        std::array<std::uint64_t, 4> expected{};
                        // Model an independent producer's invalidation on a
                        // different page of the same shared bitmap.
                        expected[0] = 1ull << 17u;
                        shared_dirty[0].fetch_or(expected[0], std::memory_order_relaxed);
                        std::array<std::uint64_t, 4> expected_cpu{};
                        for (auto page = span[0] / 128u;
                             page <= (span[0] + span[1] - 1u) / 128u; ++page) {
                            expected[page / 64u] |= 1ull << (page % 64u);
                            expected_cpu[page / 64u] |= 1ull << (page % 64u);
                        }
                        const auto address = alias | (physical_base + span[0]);
                        galaxy::guest_notify_write(&tracked, address, span[1]);
                        galaxy::guest_notify_write(&tracked, address, span[1]);
                        for (std::size_t word = 0; word < expected.size(); ++word) {
                            passed &= expect(
                                shared_dirty[word].exchange(0u, std::memory_order_acquire) == expected[word] &&
                                    cpu_dirty[word] == expected_cpu[word],
                                "dirty recapture preserves all alias/span pages and independent producer bits");
                            cpu_dirty[word] = 0u;
                        }
                    }
                }
            }
        }
        // Fast rejection must not turn wrapping, crossing, non-RAM or
        // untracked spans into apparently handled notifications.
        for (const auto& span : std::array<std::array<std::uint32_t, 2>, 6>{{
                 {0x90008000u, 1u}, {0x90007FFFu, 2u}, {0xFFFFFFFFu, 2u},
                 {0xCC008000u, 4u}, {0xB0000000u, 4u}, {0x80000000u, 0x40000001u}}}) {
            passed &= expect(
                !galaxy::guest_mark_dirty_page_fast(&tracked, span[0], span[1]) &&
                    !galaxy::guest_mark_cpu_dirty_page_fast(&tracked, span[0], span[1]),
                "dirty fast path rejects non-RAM and incomplete tracked spans");
        }
    }

    {
        // A bulk write may be fully covered by renderer tracking while only
        // crossing the edge of the independently pinned DSP audio range.
        // Both overlaps must survive; full coverage alone controls fallback.
        for (const auto physical_base : {0u, 0x10000000u}) {
            for (const auto alias : {0u, 0x80000000u, 0xC0000000u}) {
                for (const auto& span : std::array<std::array<std::uint32_t, 2>, 5>{{
                        {0x70u, 0x20u}, {0xB0u, 0x20u}, {0x50u, 0x100u},
                        {0x80u, 0x40u}, {0xE0u, 4u}}}) {
                    std::atomic_uint64_t shared{};
                    std::uint64_t cpu{};
                    NotifyProbe probe{};
                    galaxy::GuestMemoryV1 tracked{};
                    tracked.user = &probe;
                    tracked.notify_write = capture_notify_write;
                    tracked.dirty_page_words = &shared;
                    tracked.dirty_tracked_base = physical_base + 0x60u;
                    tracked.dirty_tracked_size = 0x80u;
                    tracked.dirty_page_shift = 4u;
                    tracked.dirty_page_word_count = 1u;
                    tracked.cpu_dirty_page_words = &cpu;
                    tracked.cpu_dirty_tracked_base = physical_base + 0x80u;
                    tracked.cpu_dirty_tracked_size = 0x40u;
                    tracked.cpu_dirty_page_shift = 4u;
                    tracked.cpu_dirty_page_word_count = 1u;
                    galaxy::guest_notify_write(&tracked, alias | (physical_base + span[0]), span[1]);
                    std::uint64_t expected_shared = 0u, expected_cpu = 0u;
                    for (std::uint32_t byte = span[0]; byte < span[0] + span[1]; ++byte) {
                        if (byte >= 0x60u && byte < 0xE0u) expected_shared |= UINT64_C(1) << ((byte - 0x60u) / 16u);
                        if (byte >= 0x80u && byte < 0xC0u) expected_cpu |= UINT64_C(1) << ((byte - 0x80u) / 16u);
                    }
                    const bool full_shared = span[0] >= 0x60u && span[0] + span[1] <= 0xE0u;
                    const bool full_cpu = span[0] >= 0x80u && span[0] + span[1] <= 0xC0u;
                    passed &= expect(shared.load() == expected_shared && cpu == expected_cpu &&
                            probe.writes == (full_shared || full_cpu ? 0u : 1u),
                        "bulk notification preserves each partial dirty overlap and fallback coverage");
                }
            }
        }
    }

    constexpr std::uint32_t audio_left = 0x80000240u;
    constexpr std::uint32_t audio_right = 0x80000250u;
    constexpr std::uint32_t audio_dst = 0x80000280u;
    galaxy::guest_store_u16(&memory, audio_left + 0u, 0x1111u, nullptr, 0);
    galaxy::guest_store_u16(&memory, audio_left + 2u, 0x2222u, nullptr, 0);
    galaxy::guest_store_u16(&memory, audio_left + 4u, 0x3333u, nullptr, 0);
    galaxy::guest_store_u16(&memory, audio_right + 0u, 0xAAAAu, nullptr, 0);
    galaxy::guest_store_u16(&memory, audio_right + 2u, 0xBBBBu, nullptr, 0);
    galaxy::guest_store_u16(&memory, audio_right + 4u, 0xCCCCu, nullptr, 0);
    galaxy::PpcContext interleave_context{};
    interleave_context.gpr[3] = audio_left;
    interleave_context.gpr[4] = audio_right;
    interleave_context.gpr[5] = audio_dst;
    interleave_context.gpr[6] = 3u;
    galaxy::native_audio_interleave_i16_804878BC(
        &interleave_context, &memory, nullptr, 0x804878BCu);
    passed &= expect(
        galaxy::guest_load_u16(&memory, audio_dst + 0u, nullptr, 0) == 0x1111u &&
            galaxy::guest_load_u16(&memory, audio_dst + 2u, nullptr, 0) == 0xAAAAu &&
            galaxy::guest_load_u16(&memory, audio_dst + 4u, nullptr, 0) == 0x2222u &&
            galaxy::guest_load_u16(&memory, audio_dst + 6u, nullptr, 0) == 0xBBBBu &&
            galaxy::guest_load_u16(&memory, audio_dst + 8u, nullptr, 0) == 0x3333u &&
            galaxy::guest_load_u16(&memory, audio_dst + 10u, nullptr, 0) == 0xCCCCu,
        "native audio interleave preserves big-endian stereo sample order");
    passed &= expect(
        interleave_context.gpr[3] == audio_left + 6u &&
            interleave_context.gpr[4] == audio_right + 6u &&
            interleave_context.gpr[5] == audio_dst + 12u &&
            interleave_context.gpr[6] == 0x00003333u &&
            interleave_context.gpr[0] == 0xFFFFCCCCu &&
            interleave_context.ctr == 0u &&
            !galaxy::cr_bit(&interleave_context, 0u) &&
            galaxy::cr_bit(&interleave_context, 1u) &&
            !galaxy::cr_bit(&interleave_context, 2u),
        "native audio interleave matches guest register advancement");

    constexpr std::uint32_t gpr_spill_base = 0x80000600u;
    galaxy::PpcContext savegpr_context{};
    savegpr_context.gpr[11] = gpr_spill_base;
    for (std::uint32_t reg = 14u; reg <= 31u; ++reg) {
        savegpr_context.gpr[reg] = 0xA5000000u + reg;
        galaxy::guest_store_u32(
            &memory,
            gpr_spill_base + 0xFFFFFF80u + (reg * 4u),
            0x55000000u + reg,
            nullptr,
            0);
    }
    galaxy::native_savegpr_805174FC(
        &savegpr_context, &memory, nullptr, 0x80517538u);
    passed &= expect(
        savegpr_context.pc == 0x80517538u,
        "native savegpr records the interior entry pc");
    for (std::uint32_t reg = 14u; reg <= 28u; ++reg) {
        passed &= expect(
            galaxy::guest_load_u32(
                &memory,
                gpr_spill_base + 0xFFFFFF80u + (reg * 4u),
                nullptr,
                0) == 0x55000000u + reg,
            "native savegpr leaves earlier spill slots untouched");
    }
    for (std::uint32_t reg = 29u; reg <= 31u; ++reg) {
        passed &= expect(
            galaxy::guest_load_u32(
                &memory,
                gpr_spill_base + 0xFFFFFF80u + (reg * 4u),
                nullptr,
                0) == 0xA5000000u + reg,
            "native savegpr writes the requested callee-save slots");
    }

    galaxy::PpcContext restgpr_context{};
    restgpr_context.gpr[11] = gpr_spill_base;
    for (std::uint32_t reg = 14u; reg <= 31u; ++reg) {
        restgpr_context.gpr[reg] = 0xCC000000u + reg;
        galaxy::guest_store_u32(
            &memory,
            gpr_spill_base + 0xFFFFFF80u + (reg * 4u),
            0x66000000u + reg,
            nullptr,
            0);
    }
    galaxy::native_restgpr_80517548(
        &restgpr_context, &memory, nullptr, 0x80517584u);
    passed &= expect(
        restgpr_context.pc == 0x80517584u,
        "native restgpr records the interior entry pc");
    for (std::uint32_t reg = 14u; reg <= 28u; ++reg) {
        passed &= expect(
            restgpr_context.gpr[reg] == 0xCC000000u + reg,
            "native restgpr leaves earlier registers untouched");
    }
    for (std::uint32_t reg = 29u; reg <= 31u; ++reg) {
        passed &= expect(
            restgpr_context.gpr[reg] == 0x66000000u + reg,
            "native restgpr loads the requested callee-save registers");
    }

    const std::uint32_t writes_before_slow_mapper_check = device_probe.writes;
    galaxy::guest_store_u32(
        &memory, 0x80000040u, 0x11223344u, nullptr, 0);
    passed &= expect(
        device_probe.writes == writes_before_slow_mapper_check + 1u,
        "an address outside the fast segment falls back to the slow mapper");

    galaxy::PpcContext context{};
    galaxy::guest_store_u32(
        &memory, 0x80000010u, std::bit_cast<std::uint32_t>(1.5F), nullptr, 0);
    galaxy::load_fpr_single(&context, 2, &memory, 0x80000010u, nullptr, 0);
    passed &= expect(galaxy::fpr_as_double(&context, 2) == 1.5,
                     "single load widens to the FPR double representation");
    galaxy::guest_store_u32(&memory, 0x80000014u, 0x7F800001u, nullptr, 0);
    galaxy::load_fpr_single(&context, 3, &memory, 0x80000014u, nullptr, 0);
    passed &= expect(context.fpr_bits[3] == 0x7FF0000020000000ull,
                     "single load preserves a signaling NaN payload");
    galaxy::guest_store_u32(&memory, 0x80000018u, 0x00000001u, nullptr, 0);
    galaxy::load_fpr_single(&context, 4, &memory, 0x80000018u, nullptr, 0);
    passed &= expect(context.fpr_bits[4] == 0x36A0000000000000ull,
                     "single load widens the minimum subnormal exactly");
    context.fpr_bits[5] = 0x3FF8000000000000ull;
    galaxy::store_fpr_single(&context, 5, &memory, 0x8000001Cu, nullptr, 0);
    passed &= expect(galaxy::guest_load_u32(&memory, 0x8000001Cu, nullptr, 0) ==
                         0x3FC00000u,
                     "single store converts a normal single-format value");
    context.fpr_bits[5] = 0x7FF8000000000000ull;
    galaxy::store_fpr_single(&context, 5, &memory, 0x8000001Cu, nullptr, 0);
    passed &= expect(galaxy::guest_load_u32(&memory, 0x8000001Cu, nullptr, 0) ==
                         0x7FC00000u,
                     "single store preserves the architectural NaN payload slice");
    context.fpr_bits[5] = 0x36A0000000000000ull;
    galaxy::store_fpr_single(&context, 5, &memory, 0x8000001Cu, nullptr, 0);
    passed &= expect(galaxy::guest_load_u32(&memory, 0x8000001Cu, nullptr, 0) == 1u,
                     "single store denormalizes the minimum subnormal");
    context.fpscr = 2u;
    context.fpr_bits[5] = 0x3FF0000010000000ull;
    galaxy::store_fpr_single(&context, 5, &memory, 0x8000001Cu, nullptr, 0);
    passed &= expect(galaxy::guest_load_u32(&memory, 0x8000001Cu, nullptr, 0) ==
                         0x3F800001u,
                     "single store honors FPSCR rounding for double-format operands");
    context.fpscr = 0u;

    galaxy::PpcContext noop_context{};
    noop_context.gpr[3] = 0x33333333u;
    noop_context.gpr[4] = 0x44444444u;
    noop_context.lr = 0x81234567u;
    noop_context.cr = 0x89ABCDEFu;
    galaxy::native_noop_803A33AC(
        &noop_context, &memory, nullptr, 0x803A33ACu);
    passed &= expect(
        noop_context.gpr[3] == 0x33333333u &&
            noop_context.gpr[4] == 0x44444444u &&
            noop_context.lr == 0x81234567u &&
            noop_context.cr == 0x89ABCDEFu,
        "JPA no-op helper preserves register and CR state");

    constexpr std::uint32_t jpa_vec_src = 0x80000100u;
    constexpr std::uint32_t jpa_vec_dst = 0x80000180u;
    galaxy::guest_store_u32(&memory, jpa_vec_src + 0x24u, 0x01020304u, nullptr, 0);
    galaxy::guest_store_u32(&memory, jpa_vec_src + 0x28u, 0x11121314u, nullptr, 0);
    galaxy::guest_store_u32(&memory, jpa_vec_src + 0x2Cu, 0x21222324u, nullptr, 0);
    galaxy::PpcContext jpa_vec_context{};
    jpa_vec_context.msr = galaxy::kMsrFloatingPointAvailable;
    jpa_vec_context.gpr[4] = jpa_vec_src;
    jpa_vec_context.gpr[5] = jpa_vec_dst;
    galaxy::native_jpa_vec_copy_803A35DC(
        &jpa_vec_context, &memory, nullptr, 0x803A35DCu);
    passed &= expect(
        galaxy::guest_load_u32(&memory, jpa_vec_dst + 0x00u, nullptr, 0) == 0x01020304u &&
            galaxy::guest_load_u32(&memory, jpa_vec_dst + 0x04u, nullptr, 0) == 0x11121314u &&
            galaxy::guest_load_u32(&memory, jpa_vec_dst + 0x08u, nullptr, 0) == 0x21222324u,
        "JPA vector-copy helper copies exactly the translated 12-byte payload");
    passed &= expect(
        jpa_vec_context.gpr[3] == jpa_vec_dst &&
            jpa_vec_context.gpr[4] == jpa_vec_src + 0x24u,
        "JPA vector-copy helper preserves translated volatile register side effects");

    constexpr std::uint32_t jpa_dispatch_dst = 0x800001C0u;
    std::uint32_t jpa_dispatch_cached_target = 0u;
    galaxy::NativeGameFunction jpa_dispatch_cached_function = nullptr;
    CachedCallProbe jpa_dispatch_probe{};
    galaxy::NativeServicesV1 jpa_dispatch_services{};
    jpa_dispatch_services.user = &jpa_dispatch_probe;
    jpa_dispatch_services.call_guest_cached = &capture_cached_call;
    galaxy::PpcContext jpa_dispatch_context{};
    jpa_dispatch_context.msr = galaxy::kMsrFloatingPointAvailable;
    jpa_dispatch_context.gpr[4] = jpa_vec_src;
    jpa_dispatch_context.gpr[5] = jpa_dispatch_dst;
    galaxy::native_jpa_direction_callback_803A3BB4(
        &jpa_dispatch_services,
        0x803A35DCu,
        &jpa_dispatch_cached_target,
        &jpa_dispatch_cached_function,
        &jpa_dispatch_context,
        &memory);
    passed &= expect(
        jpa_dispatch_probe.calls == 0u &&
            jpa_dispatch_context.lr == 0x803A3BB8u &&
            galaxy::guest_load_u32(&memory, jpa_dispatch_dst + 0x00u, nullptr, 0) ==
                0x01020304u &&
            galaxy::guest_load_u32(&memory, jpa_dispatch_dst + 0x04u, nullptr, 0) ==
                0x11121314u &&
            galaxy::guest_load_u32(&memory, jpa_dispatch_dst + 0x08u, nullptr, 0) ==
                0x21222324u,
        "JPA direction callback helper directly handles the hot 0x803A35DC target");

    constexpr std::uint32_t jpa_dispatch_dst_e8 = 0x800001D0u;
    galaxy::guest_store_u32(&memory, jpa_vec_src + 0x0Cu, 0x31323334u, nullptr, 0);
    galaxy::guest_store_u32(&memory, jpa_vec_src + 0x10u, 0x41424344u, nullptr, 0);
    galaxy::guest_store_u32(&memory, jpa_vec_src + 0x14u, 0x51525354u, nullptr, 0);
    jpa_dispatch_context = {};
    jpa_dispatch_context.msr = galaxy::kMsrFloatingPointAvailable;
    jpa_dispatch_context.gpr[4] = jpa_vec_src;
    jpa_dispatch_context.gpr[5] = jpa_dispatch_dst_e8;
    galaxy::native_jpa_direction_callback_803A3BB4(
        &jpa_dispatch_services,
        0x803A35E8u,
        &jpa_dispatch_cached_target,
        &jpa_dispatch_cached_function,
        &jpa_dispatch_context,
        &memory);
    passed &= expect(
        jpa_dispatch_probe.calls == 0u &&
            jpa_dispatch_context.lr == 0x803A3BB8u &&
            galaxy::guest_load_u32(&memory, jpa_dispatch_dst_e8 + 0x00u, nullptr, 0) ==
                0x31323334u &&
            galaxy::guest_load_u32(&memory, jpa_dispatch_dst_e8 + 0x04u, nullptr, 0) ==
                0x41424344u &&
            galaxy::guest_load_u32(&memory, jpa_dispatch_dst_e8 + 0x08u, nullptr, 0) ==
                0x51525354u,
        "JPA direction callback helper directly handles the sibling 0x803A35E8 target");

    galaxy::PpcContext jpa_dispatch_fallback_context{};
    galaxy::native_jpa_direction_callback_803A3BB4(
        &jpa_dispatch_services,
        0x803A3640u,
        &jpa_dispatch_cached_target,
        &jpa_dispatch_cached_function,
        &jpa_dispatch_fallback_context,
        &memory);
    passed &= expect(
        jpa_dispatch_probe.calls == 1u &&
            jpa_dispatch_probe.guest_address == 0x803A3640u &&
            jpa_dispatch_probe.lr == 0x803A3BB8u &&
            jpa_dispatch_probe.cache_slots_present,
        "JPA direction callback helper falls back to cached guest dispatch for unknown targets");

    constexpr std::uint32_t jpa_projection_matrix = 0x80000500u;
    galaxy::guest_store_u32(
        &memory, jpa_projection_matrix + 0x00u, std::bit_cast<std::uint32_t>(1.0F), nullptr, 0);
    galaxy::guest_store_u32(
        &memory, jpa_projection_matrix + 0x04u, std::bit_cast<std::uint32_t>(2.0F), nullptr, 0);
    galaxy::guest_store_u32(
        &memory, jpa_projection_matrix + 0x08u, std::bit_cast<std::uint32_t>(3.0F), nullptr, 0);
    galaxy::guest_store_u32(
        &memory, jpa_projection_matrix + 0x10u, std::bit_cast<std::uint32_t>(4.0F), nullptr, 0);
    galaxy::guest_store_u32(
        &memory, jpa_projection_matrix + 0x14u, std::bit_cast<std::uint32_t>(5.0F), nullptr, 0);
    galaxy::guest_store_u32(
        &memory, jpa_projection_matrix + 0x18u, std::bit_cast<std::uint32_t>(6.0F), nullptr, 0);
    galaxy::guest_store_u32(
        &memory, jpa_projection_matrix + 0x20u, std::bit_cast<std::uint32_t>(7.0F), nullptr, 0);
    galaxy::guest_store_u32(
        &memory, jpa_projection_matrix + 0x24u, std::bit_cast<std::uint32_t>(8.0F), nullptr, 0);
    galaxy::guest_store_u32(
        &memory, jpa_projection_matrix + 0x28u, std::bit_cast<std::uint32_t>(9.0F), nullptr, 0);
    CachedCallProbe jpa_projection_probe{};
    galaxy::NativeServicesV1 jpa_projection_services{};
    jpa_projection_services.user = &jpa_projection_probe;
    jpa_projection_services.call_guest_cached = &capture_cached_call;
    std::uint32_t jpa_projection_cached_target = 0u;
    galaxy::NativeGameFunction jpa_projection_cached_function = nullptr;
    galaxy::PpcContext jpa_projection_context{};
    jpa_projection_context.gpr[3] = jpa_projection_matrix;
    jpa_projection_context.fpr_bits[1] =
        galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(2.0F));
    jpa_projection_context.fpr_bits[2] =
        galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(3.0F));
    galaxy::native_jpa_projection_callback_803A3CA8(
        &jpa_projection_services,
        0x803A387Cu,
        &jpa_projection_cached_target,
        &jpa_projection_cached_function,
        &jpa_projection_context,
        &memory);
    passed &= expect(
        jpa_projection_probe.calls == 0u &&
            jpa_projection_context.lr == 0x803A3CACu &&
            galaxy::guest_load_u32(&memory, jpa_projection_matrix + 0x00u, nullptr, 0) ==
                std::bit_cast<std::uint32_t>(2.0F) &&
            galaxy::guest_load_u32(&memory, jpa_projection_matrix + 0x04u, nullptr, 0) ==
                std::bit_cast<std::uint32_t>(6.0F) &&
            galaxy::guest_load_u32(&memory, jpa_projection_matrix + 0x08u, nullptr, 0) ==
                std::bit_cast<std::uint32_t>(6.0F),
        "JPA projection callback helper directly handles the hot 0x803A387C target");

    CachedCallProbe jpa_draw_probe{};
    galaxy::NativeServicesV1 jpa_draw_services{};
    jpa_draw_services.user = &jpa_draw_probe;
    jpa_draw_services.call_guest_cached = &capture_cached_call;
    std::uint32_t jpa_draw_list_cached_target = 0u;
    galaxy::NativeGameFunction jpa_draw_list_cached_function = nullptr;
    galaxy::PpcContext jpa_draw_context{};
    jpa_draw_context.gpr[3] = 0x33333333u;
    jpa_draw_context.gpr[4] = 0x44444444u;
    galaxy::native_jpa_draw_callback_803A3CE4(
        &jpa_draw_services,
        0x803A33ACu,
        &jpa_draw_list_cached_target,
        &jpa_draw_list_cached_function,
        &jpa_draw_context,
        &memory);
    passed &= expect(
        jpa_draw_probe.calls == 0u &&
            jpa_draw_context.lr == 0x803A3CE8u &&
            jpa_draw_context.gpr[3] == 0x33333333u &&
            jpa_draw_context.gpr[4] == 0x44444444u,
        "JPA draw callback helper directly handles the hot no-op target");

    constexpr std::uint32_t jpa_alpha_sda2 = 0x80000000u;
    constexpr std::uint32_t jpa_alpha_work = 0x80000600u;
    constexpr std::uint32_t jpa_alpha_stack = 0x80001000u;
    galaxy::guest_store_u32(
        &memory, jpa_alpha_sda2 + 0x1FD0u, std::bit_cast<std::uint32_t>(100.0F), nullptr, 0);
    galaxy::guest_store_u32(
        &memory, jpa_alpha_sda2 + 0x1FD4u, std::bit_cast<std::uint32_t>(3.0F), nullptr, 0);
    galaxy::guest_store_u32(
        &memory, jpa_alpha_work + 0x84u, std::bit_cast<std::uint32_t>(1.0F), nullptr, 0);
    galaxy::PpcContext jpa_alpha_context{};
    jpa_alpha_context.hid2 = 0xA0000000u;
    jpa_alpha_context.gqr[2] = 4u;
    jpa_alpha_context.gpr[1] = jpa_alpha_stack;
    jpa_alpha_context.gpr[2] = jpa_alpha_sda2;
    jpa_alpha_context.gpr[4] = jpa_alpha_work;
    galaxy::native_jpa_alpha_callback_804465A0(
        &jpa_alpha_context, &memory, nullptr, 0x804465A0u);
    passed &= expect(
        jpa_alpha_context.gpr[1] == jpa_alpha_stack &&
            jpa_alpha_context.gpr[0] == 200u &&
            galaxy::guest_load_u32(&memory, jpa_alpha_stack - 0x10u, nullptr, 0) ==
                jpa_alpha_stack &&
            galaxy::guest_load_u8(&memory, jpa_alpha_stack - 0x08u, nullptr, 0) == 200u &&
            galaxy::guest_load_u8(&memory, jpa_alpha_work + 0x96u, nullptr, 0) == 200u,
        "JPA alpha callback helper preserves stack and unsigned-byte psq_st side effects");
    passed &= expect(
        jpa_alpha_context.fpr_bits[1] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(2.0F)) &&
            jpa_alpha_context.fpr_bits[0] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(200.0F)),
        "JPA alpha callback helper preserves translated scalar float side effects");

    galaxy::guest_store_u32(
        &memory, jpa_alpha_work + 0x68u, std::bit_cast<std::uint32_t>(2.0F), nullptr, 0);
    galaxy::guest_store_u32(
        &memory, jpa_alpha_work + 0x6Cu, std::bit_cast<std::uint32_t>(-4.0F), nullptr, 0);
    galaxy::guest_store_u32(
        &memory, jpa_alpha_work + 0x84u, std::bit_cast<std::uint32_t>(1.5F), nullptr, 0);
    galaxy::guest_store_u32(
        &memory, jpa_alpha_sda2 + 0x1FD4u, std::bit_cast<std::uint32_t>(5.0F), nullptr, 0);
    galaxy::PpcContext jpa_scale_context{};
    jpa_scale_context.gpr[2] = jpa_alpha_sda2;
    jpa_scale_context.gpr[4] = jpa_alpha_work;
    galaxy::native_jpa_scale_callback_804465CC(
        &jpa_scale_context, &memory, nullptr, 0x804465CCu);
    passed &= expect(
        galaxy::guest_load_u32(&memory, jpa_alpha_work + 0x60u, nullptr, 0) ==
                std::bit_cast<std::uint32_t>(7.0F) &&
            galaxy::guest_load_u32(&memory, jpa_alpha_work + 0x64u, nullptr, 0) ==
                std::bit_cast<std::uint32_t>(-14.0F),
        "JPA scale callback helper writes the translated scaled float outputs");
    passed &= expect(
        jpa_scale_context.fpr_bits[2] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(3.5F)) &&
            jpa_scale_context.fpr_bits[1] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(7.0F)) &&
            jpa_scale_context.fpr_bits[0] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(-14.0F)),
        "JPA scale callback helper preserves translated FPR side effects");

    constexpr std::uint32_t utf16_reader = 0x80000200u;
    constexpr std::uint32_t utf16_text = 0x80000240u;
    galaxy::guest_store_u32(&memory, utf16_reader, utf16_text, nullptr, 0);
    galaxy::guest_store_u16(&memory, utf16_text, 0x30ABu, nullptr, 0);
    galaxy::PpcContext utf16_context{};
    utf16_context.gpr[3] = utf16_reader;
    galaxy::native_read_next_char_utf16_800072B4(
        &utf16_context, &memory, nullptr, 0x800072B4u);
    passed &= expect(
        utf16_context.gpr[3] == 0x30ABu &&
            utf16_context.gpr[4] == utf16_text &&
            utf16_context.gpr[5] == utf16_reader &&
            utf16_context.gpr[0] == utf16_text + 2u &&
            galaxy::guest_load_u32(&memory, utf16_reader, nullptr, 0) == utf16_text + 2u,
        "UTF-16 reader helper loads one big-endian code unit and advances the stream");

    constexpr std::uint32_t font_object = 0x80000280u;
    constexpr std::uint32_t font_vtable = 0x800002C0u;
    constexpr std::uint32_t font_stack = 0x80000340u;
    constexpr std::uint32_t font_target_raw = 0x80412345u;
    galaxy::guest_store_u32(&memory, font_object, font_vtable, nullptr, 0);
    galaxy::guest_store_u32(&memory, font_vtable + 0x4Cu, font_target_raw, nullptr, 0);
    CachedCallProbe font_probe{};
    font_probe.return_gpr3 = 0x0011FE33u;
    galaxy::NativeServicesV1 font_services{};
    font_services.user = &font_probe;
    font_services.call_guest_cached = &capture_cached_call;
    galaxy::PpcContext font_context{};
    font_context.gpr[1] = font_stack;
    font_context.gpr[3] = font_object;
    font_context.gpr[4] = 0x3042u;
    font_context.lr = 0x81234567u;
    galaxy::native_resfont_get_char_width_80007D2C(
        &font_context, &memory, &font_services, 0x80007D2Cu);
    passed &= expect(
        font_probe.calls == 1u &&
            font_probe.guest_address == (font_target_raw & 0xFFFFFFFCu) &&
            font_probe.gpr3 == font_object &&
            font_probe.gpr4 == 0x3042u &&
            font_probe.gpr12 == font_target_raw &&
            font_probe.ctr == font_target_raw &&
            font_probe.lr == 0x80007D48u &&
            font_probe.cache_slots_present,
        "font-width helper dispatches through the original vtable slot");
    passed &= expect(
        font_context.gpr[1] == font_stack &&
            font_context.lr == 0x81234567u &&
            font_context.gpr[3] == 0xFFFFFFFEu &&
            galaxy::guest_load_u8(&memory, font_stack - 0x08u, nullptr, 0) == 0x00u &&
            galaxy::guest_load_u8(&memory, font_stack - 0x07u, nullptr, 0) == 0x11u &&
            galaxy::guest_load_u8(&memory, font_stack - 0x06u, nullptr, 0) == 0xFEu,
        "font-width helper preserves stack scratch bytes and signed char-width return");

    constexpr std::uint32_t flag_dispatch_object = 0x80000380u;
    constexpr std::uint32_t flag_dispatch_vtable = 0x800003C0u;
    constexpr std::uint32_t flag_dispatch_target_raw = 0x80423457u;
    galaxy::guest_store_u32(&memory, flag_dispatch_object, flag_dispatch_vtable, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, flag_dispatch_vtable + 0x14u, flag_dispatch_target_raw, nullptr, 0);
    galaxy::guest_store_u16(&memory, flag_dispatch_object + 0x08u, 0x0001u, nullptr, 0);
    galaxy::PpcContext flag_context{};
    flag_context.gpr[3] = flag_dispatch_object;
    flag_context.lr = 0x88776655u;
    galaxy::native_vtable_flag_dispatch_80261254(
        &flag_context, &memory, nullptr, 0x80261254u);
    passed &= expect(
        flag_context.gpr[0] == 1u &&
            flag_context.lr == 0x88776655u &&
            ((flag_context.cr >> 28) & 0xFu) == 0x2u,
        "flag-dispatch helper returns early when the translated flag bit is set");

    CachedCallProbe flag_probe{};
    galaxy::NativeServicesV1 flag_services{};
    flag_services.user = &flag_probe;
    flag_services.call_guest_cached = &capture_cached_call;
    galaxy::guest_store_u16(&memory, flag_dispatch_object + 0x08u, 0x0000u, nullptr, 0);
    flag_context = {};
    flag_context.gpr[3] = flag_dispatch_object;
    flag_context.lr = 0x88776655u;
    galaxy::native_vtable_flag_dispatch_80261254(
        &flag_context, &memory, &flag_services, 0x80261254u);
    passed &= expect(
        flag_probe.calls == 1u &&
            flag_probe.guest_address == (flag_dispatch_target_raw & 0xFFFFFFFCu) &&
            flag_probe.gpr3 == flag_dispatch_object &&
            flag_probe.gpr12 == flag_dispatch_target_raw &&
            flag_probe.ctr == flag_dispatch_target_raw &&
            flag_probe.lr == 0x88776655u &&
            flag_probe.cache_slots_present,
        "flag-dispatch helper preserves the original tail-call target boundary");

    galaxy::PpcContext stride_context{};
    stride_context.gpr[3] = 0x80001000u;
    stride_context.gpr[4] = 0xFFFFFFFFu;
    galaxy::native_stride6_offset8_8042E100(
        &stride_context, &memory, nullptr, 0x8042E100u);
    passed &= expect(
        stride_context.gpr[0] == 0xFFFFFFFAu &&
            stride_context.gpr[3] == 0x80001002u,
        "stride-six helper preserves signed multiply and offset side effects");

    constexpr std::uint32_t indexed_table = 0x80000190u;
    galaxy::guest_store_u32(&memory, indexed_table + 0x00u, 0x10203040u, nullptr, 0);
    galaxy::guest_store_u32(&memory, indexed_table + 0x04u, 0x50607080u, nullptr, 0);
    galaxy::guest_store_u32(&memory, indexed_table + 0x08u, 0x90A0B0C0u, nullptr, 0);
    galaxy::PpcContext indexed_context{};
    indexed_context.gpr[3] = indexed_table;
    indexed_context.gpr[4] = 2u;
    galaxy::native_indexed_word_load_80344A54(
        &indexed_context, &memory, nullptr, 0x80344A54u);
    passed &= expect(
        indexed_context.gpr[0] == 8u &&
            indexed_context.gpr[3] == 0x90A0B0C0u &&
            indexed_context.gpr[4] == 2u,
        "indexed-word-load helper preserves translated index and result registers");

    galaxy::PpcContext add_12_context{};
    add_12_context.gpr[3] = 0x800001F4u;
    galaxy::native_add_12_80097278(
        &add_12_context, &memory, nullptr, 0x80097278u);
    passed &= expect(
        add_12_context.gpr[3] == 0x80000200u,
        "add-12 helper advances the pointer argument exactly one translated field");

    constexpr std::uint32_t matrix_base = 0x80000050u;
    const std::array<float, 9> matrix_values{
        1.0F, 2.0F, 3.0F,
        4.0F, 5.0F, 6.0F,
        7.0F, 8.0F, 9.0F,
    };
    const std::array<std::uint32_t, 9> matrix_offsets{
        0x00u, 0x04u, 0x08u,
        0x10u, 0x14u, 0x18u,
        0x20u, 0x24u, 0x28u,
    };
    for (std::size_t i = 0; i < matrix_values.size(); ++i) {
        galaxy::guest_store_u32(
            &memory,
            matrix_base + matrix_offsets[i],
            std::bit_cast<std::uint32_t>(matrix_values[i]),
            nullptr,
            0);
    }
    context.gpr[3] = matrix_base;
    context.msr |= galaxy::kMsrFloatingPointAvailable;
    context.hid2 = 0x20000000u;
    context.fpscr = 0;
    context.fpr_bits[1] =
        galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(2.0F));
    context.fpr_bits[2] =
        galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(-3.0F));
    galaxy::native_mtx_scale_803A387C(
        &context, &memory, nullptr, 0x803A387Cu);
    const std::array<float, 9> expected_matrix{
        2.0F, -6.0F, 6.0F,
        8.0F, -15.0F, 12.0F,
        14.0F, -24.0F, 18.0F,
    };
    for (std::size_t i = 0; i < expected_matrix.size(); ++i) {
        passed &= expect(
            galaxy::guest_load_u32(&memory, matrix_base + matrix_offsets[i], nullptr, 0) ==
                std::bit_cast<std::uint32_t>(expected_matrix[i]),
            "native matrix scale stores the translated single-precision result");
    }
    passed &= expect(
        context.fpr_bits[10] ==
            galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(2.0F)) &&
            context.fpr_bits[9] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(8.0F)) &&
            context.fpr_bits[8] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(14.0F)) &&
            context.fpr_bits[7] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(-6.0F)) &&
            context.fpr_bits[6] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(-15.0F)),
        "native matrix scale preserves upper FPR result side effects");
    passed &= expect(
        context.fpr_bits[5] ==
            galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(-24.0F)) &&
            context.fpr_bits[4] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(6.0F)) &&
            context.fpr_bits[2] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(12.0F)) &&
            context.fpr_bits[0] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(18.0F)) &&
            context.fpr_bits[3] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(6.0F)),
        "native matrix scale preserves lower FPR/load side effects");
    passed &= expect(
        context.ps1_bits[0] == context.fpr_bits[0] &&
            context.ps1_bits[2] == context.fpr_bits[2] &&
            context.ps1_bits[10] == context.fpr_bits[10],
        "native matrix scale duplicates committed singles into PS1 when PSE is enabled");

    const auto store_f32 = [&](std::uint32_t address, float value) {
        galaxy::guest_store_u32(
            &memory, address, std::bit_cast<std::uint32_t>(value), nullptr, 0);
    };

    constexpr std::uint32_t identity_matrix = 0x800001C0u;
    store_f32(0x800024B0u, 1.0F);
    store_f32(0x800024B4u, 0.0F);
    galaxy::PpcContext identity_context{};
    identity_context.hid2 = 0xA0000000u;
    identity_context.gpr[2] = 0x80000000u;
    identity_context.gpr[3] = identity_matrix;
    identity_context.gqr[0] = 0u;
    galaxy::native_psmtx_identity_804B5EDC(
        &identity_context, &memory, nullptr, 0x804B5EDCu);
    const std::array<float, 12> expected_identity{
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F, 0.0F,
    };
    for (std::size_t i = 0; i < expected_identity.size(); ++i) {
        passed &= expect(
            galaxy::guest_load_u32(
                &memory,
                identity_matrix + static_cast<std::uint32_t>(i * 4u),
                nullptr,
                0) == std::bit_cast<std::uint32_t>(expected_identity[i]),
            "native PSMTXIdentity helper stores the translated identity matrix");
    }
    const std::uint64_t zero_f64 =
        galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(0.0F));
    const std::uint64_t one_f64 =
        galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(1.0F));
    passed &= expect(
        identity_context.fpr_bits[0] == zero_f64 &&
            identity_context.ps1_bits[0] == zero_f64 &&
            identity_context.fpr_bits[1] == zero_f64 &&
            identity_context.ps1_bits[1] == one_f64 &&
            identity_context.fpr_bits[2] == one_f64 &&
            identity_context.ps1_bits[2] == zero_f64,
        "native PSMTXIdentity helper preserves translated paired-register side effects");

    constexpr std::uint32_t copy_matrix_src = 0x800002C0u;
    constexpr std::uint32_t copy_matrix_dst = 0x80000300u;
    const std::array<float, 12> copy_matrix_values{
        1.25F, -2.5F, 3.75F, -4.5F,
        5.25F, -6.5F, 7.75F, -8.5F,
        9.25F, -10.5F, 11.75F, -12.5F,
    };
    for (std::size_t i = 0; i < copy_matrix_values.size(); ++i) {
        store_f32(
            copy_matrix_src + static_cast<std::uint32_t>(i * 4u),
            copy_matrix_values[i]);
        galaxy::guest_store_u32(
            &memory,
            copy_matrix_dst + static_cast<std::uint32_t>(i * 4u),
            0xDEADBEEFu,
            nullptr,
            0);
    }
    galaxy::PpcContext copy_context{};
    copy_context.hid2 = 0xA0000000u;
    copy_context.gpr[3] = copy_matrix_src;
    copy_context.gpr[4] = copy_matrix_dst;
    copy_context.gqr[0] = 0u;
    galaxy::native_psmtx_copy_804B5F08(
        &copy_context, &memory, nullptr, 0x804B5F08u);
    for (std::size_t i = 0; i < copy_matrix_values.size(); ++i) {
        passed &= expect(
            galaxy::guest_load_u32(
                &memory,
                copy_matrix_dst + static_cast<std::uint32_t>(i * 4u),
                nullptr,
                0) == std::bit_cast<std::uint32_t>(copy_matrix_values[i]),
            "native PSMTXCopy helper copies the translated 3x4 matrix payload");
    }
    for (std::uint32_t pair = 0; pair < 6u; ++pair) {
        const std::uint32_t first = pair * 2u;
        passed &= expect(
            copy_context.fpr_bits[pair] ==
                    galaxy::widen_f32_bits(
                        std::bit_cast<std::uint32_t>(copy_matrix_values[first])) &&
            copy_context.ps1_bits[pair] ==
                    galaxy::widen_f32_bits(
                        std::bit_cast<std::uint32_t>(copy_matrix_values[first + 1u])),
            "native PSMTXCopy helper preserves translated paired-load side effects");
    }

    const auto setup_normalize_vector = [&](std::uint32_t address) {
        store_f32(address + 0u, 3.0F);
        store_f32(address + 4u, 4.0F);
        store_f32(address + 8u, 12.0F);
    };
    const auto setup_normalize_context = [&](
        galaxy::PpcContext& normalize_context,
        std::uint32_t source,
        std::uint32_t destination) {
        normalize_context = {};
        normalize_context.hid2 = 0xA0000000u;
        normalize_context.gpr[2] = 0x80000000u;
        normalize_context.gpr[3] = source;
        normalize_context.gpr[4] = destination;
        normalize_context.gqr[0] = 0u;
    };
    store_f32(0x800024E8u, 0.5F);
    store_f32(0x800024ECu, 3.0F);

    constexpr std::uint32_t normalize_source = 0x80000120u;
    constexpr std::uint32_t normalize_destination = 0x80000130u;
    setup_normalize_vector(normalize_source);
    galaxy::PpcContext normalize_reference{};
    setup_normalize_context(
        normalize_reference, normalize_source, normalize_destination);
    run_reference_psvec_normalize_804B6BCC(&normalize_reference, &memory);
    const std::array<std::uint32_t, 3> expected_normalized{
        galaxy::guest_load_u32(&memory, normalize_destination + 0u, nullptr, 0),
        galaxy::guest_load_u32(&memory, normalize_destination + 4u, nullptr, 0),
        galaxy::guest_load_u32(&memory, normalize_destination + 8u, nullptr, 0),
    };
    galaxy::guest_store_u32(&memory, normalize_destination + 0u, 0u, nullptr, 0);
    galaxy::guest_store_u32(&memory, normalize_destination + 4u, 0u, nullptr, 0);
    galaxy::guest_store_u32(&memory, normalize_destination + 8u, 0u, nullptr, 0);
    setup_normalize_vector(normalize_source);
    galaxy::PpcContext normalize_native{};
    setup_normalize_context(normalize_native, normalize_source, normalize_destination);
    galaxy::native_psvec_normalize_804B6BCC(
        &normalize_native, &memory, nullptr, 0x804B6BCCu);
    for (std::size_t i = 0; i < expected_normalized.size(); ++i) {
        passed &= expect(
            galaxy::guest_load_u32(
                &memory,
                normalize_destination + static_cast<std::uint32_t>(i * 4u),
                nullptr,
                0) == expected_normalized[i],
            "native PS vector normalize stores the translated result bits");
    }
    passed &= expect(
        normalize_native.fpscr == normalize_reference.fpscr,
        "native PS vector normalize preserves FPSCR side effects");
    for (std::uint32_t i = 0; i <= 6; ++i) {
        passed &= expect(
            normalize_native.fpr_bits[i] == normalize_reference.fpr_bits[i],
            "native PS vector normalize preserves FPR side effects");
        passed &= expect(
            normalize_native.ps1_bits[i] == normalize_reference.ps1_bits[i],
            "native PS vector normalize preserves PS1 side effects");
    }

    constexpr std::uint32_t normalize_in_place_address = 0x80000150u;
    setup_normalize_vector(normalize_in_place_address);
    galaxy::PpcContext in_place_reference{};
    setup_normalize_context(
        in_place_reference, normalize_in_place_address, normalize_in_place_address);
    run_reference_psvec_normalize_804B6BCC(&in_place_reference, &memory);
    const std::array<std::uint32_t, 3> expected_in_place{
        galaxy::guest_load_u32(&memory, normalize_in_place_address + 0u, nullptr, 0),
        galaxy::guest_load_u32(&memory, normalize_in_place_address + 4u, nullptr, 0),
        galaxy::guest_load_u32(&memory, normalize_in_place_address + 8u, nullptr, 0),
    };
    setup_normalize_vector(normalize_in_place_address);
    galaxy::PpcContext in_place_native{};
    setup_normalize_context(in_place_native, normalize_in_place_address, 0xDEADBEEFu);
    galaxy::native_vec_normalize_in_place_803E4D24(
        &in_place_native, &memory, nullptr, 0x803E4D24u);
    for (std::size_t i = 0; i < expected_in_place.size(); ++i) {
        passed &= expect(
            galaxy::guest_load_u32(
                &memory,
                normalize_in_place_address + static_cast<std::uint32_t>(i * 4u),
                nullptr,
                0) == expected_in_place[i],
            "native in-place vector normalize stores translated result bits");
    }
    passed &= expect(
        in_place_native.gpr[4] == normalize_in_place_address &&
            in_place_native.pc == 0x804B6BCCu,
        "native in-place vector normalize preserves wrapper register side effects");

    constexpr std::uint32_t threshold_vector = 0x80000170u;
    store_f32(threshold_vector + 0u, 0.25F);
    store_f32(threshold_vector + 4u, -0.25F);
    store_f32(threshold_vector + 8u, 0.0F);
    galaxy::PpcContext threshold_context{};
    threshold_context.gpr[3] = threshold_vector;
    threshold_context.fpr_bits[1] =
        galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(0.25F));
    galaxy::native_vec3_abs_le_threshold_803E595C(
        &threshold_context, &memory, nullptr, 0x803E595Cu);
    passed &= expect(
        threshold_context.gpr[3] == 1u &&
            threshold_context.gpr[0] == 32u &&
            threshold_context.fpr_bits[2] ==
                (galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(0.25F)) ^
                 0x8000000000000000ull),
        "native vector threshold helper accepts components inside the signed threshold");

    store_f32(threshold_vector + 0u, 0.5F);
    store_f32(threshold_vector + 4u, 0.0F);
    store_f32(threshold_vector + 8u, 0.0F);
    galaxy::PpcContext threshold_high_context{};
    threshold_high_context.gpr[3] = threshold_vector;
    threshold_high_context.fpr_bits[1] =
        galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(0.25F));
    galaxy::native_vec3_abs_le_threshold_803E595C(
        &threshold_high_context, &memory, nullptr, 0x803E595Cu);
    passed &= expect(
        threshold_high_context.gpr[3] == 0u &&
            galaxy::cr_bit(&threshold_high_context, 1u),
        "native vector threshold helper rejects a component above threshold");

    store_f32(threshold_vector + 0u, 0.0F);
    store_f32(threshold_vector + 4u, -0.5F);
    store_f32(threshold_vector + 8u, 0.0F);
    galaxy::PpcContext threshold_low_context{};
    threshold_low_context.gpr[3] = threshold_vector;
    threshold_low_context.fpr_bits[1] =
        galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(0.25F));
    galaxy::native_vec3_abs_le_threshold_803E595C(
        &threshold_low_context, &memory, nullptr, 0x803E595Cu);
    passed &= expect(
        threshold_low_context.gpr[3] == 0u &&
            galaxy::cr_bit(&threshold_low_context, 0u),
        "native vector threshold helper rejects a component below negative threshold");

    constexpr std::uint32_t concat_lhs = 0x80000200u;
    constexpr std::uint32_t concat_rhs = 0x80000240u;
    constexpr std::uint32_t concat_dst = 0x80000280u;
    constexpr std::uint32_t concat_stack = 0x80000480u;
    const std::array<float, 12> concat_lhs_values{
        1.0F, 0.0F, 0.0F, 10.0F,
        0.0F, 1.0F, 0.0F, 20.0F,
        0.0F, 0.0F, 1.0F, 30.0F,
    };
    const std::array<float, 12> concat_rhs_values{
        2.0F, 3.0F, 4.0F, 5.0F,
        6.0F, 7.0F, 8.0F, 9.0F,
        10.0F, 11.0F, 12.0F, 13.0F,
    };
    for (std::size_t i = 0; i < concat_lhs_values.size(); ++i) {
        const std::uint32_t offset = static_cast<std::uint32_t>(i * 4u);
        store_f32(concat_lhs + offset, concat_lhs_values[i]);
        store_f32(concat_rhs + offset, concat_rhs_values[i]);
        galaxy::guest_store_u32(&memory, concat_dst + offset, 0u, nullptr, 0);
    }
    store_f32(0x8069E148u, 0.0F);
    store_f32(0x8069E14Cu, 1.0F);

    galaxy::PpcContext concat_context{};
    concat_context.hid2 = 0xA0000000u;
    concat_context.gpr[1] = concat_stack;
    concat_context.gpr[3] = concat_lhs;
    concat_context.gpr[4] = concat_rhs;
    concat_context.gpr[5] = concat_dst;
    concat_context.gqr[0] = 0u;
    const std::uint64_t saved_f14 =
        galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(101.0F));
    const std::uint64_t saved_f15 =
        galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(202.0F));
    const std::uint64_t saved_f31 =
        galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(303.0F));
    concat_context.fpr_bits[14] = saved_f14;
    concat_context.fpr_bits[15] = saved_f15;
    concat_context.fpr_bits[31] = saved_f31;

    galaxy::native_psmtx_concat_804B5F3C(
        &concat_context, &memory, nullptr, 0x804B5F3Cu);
    const std::array<float, 12> expected_concat{
        2.0F, 3.0F, 4.0F, 15.0F,
        6.0F, 7.0F, 8.0F, 29.0F,
        10.0F, 11.0F, 12.0F, 43.0F,
    };
    for (std::size_t i = 0; i < expected_concat.size(); ++i) {
        const std::uint32_t offset = static_cast<std::uint32_t>(i * 4u);
        passed &= expect(
            galaxy::guest_load_u32(&memory, concat_dst + offset, nullptr, 0) ==
                std::bit_cast<std::uint32_t>(expected_concat[i]),
            "native PSMTXConcat helper stores the translated matrix result");
    }
    passed &= expect(
        concat_context.gpr[1] == concat_stack &&
            concat_context.gpr[6] == 0x8069E148u &&
            galaxy::guest_load_u32(&memory, concat_stack - 0x40u, nullptr, 0) ==
                concat_stack,
        "native PSMTXConcat helper preserves stack and SDA2 side effects");
    passed &= expect(
        galaxy::guest_load_u64(&memory, concat_stack - 0x38u, nullptr, 0) ==
            saved_f14 &&
            galaxy::guest_load_u64(&memory, concat_stack - 0x30u, nullptr, 0) ==
                saved_f15 &&
            galaxy::guest_load_u64(&memory, concat_stack - 0x18u, nullptr, 0) ==
                saved_f31,
        "native PSMTXConcat helper writes the translated FPR save slots");
    passed &= expect(
        concat_context.fpr_bits[14] == saved_f14 &&
            concat_context.fpr_bits[15] == saved_f15 &&
            concat_context.fpr_bits[31] == saved_f31,
        "native PSMTXConcat helper restores saved FPR lower lanes");
    passed &= expect(
        concat_context.fpr_bits[12] ==
            galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(2.0F)) &&
            concat_context.ps1_bits[12] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(3.0F)) &&
            concat_context.fpr_bits[13] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(4.0F)) &&
            concat_context.ps1_bits[13] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(15.0F)) &&
            concat_context.ps1_bits[14] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(7.0F)) &&
            concat_context.ps1_bits[15] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(29.0F)) &&
            concat_context.fpr_bits[0] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(12.0F)) &&
            concat_context.ps1_bits[0] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(43.0F)) &&
            concat_context.fpr_bits[2] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(10.0F)) &&
            concat_context.ps1_bits[2] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(11.0F)) &&
            concat_context.ps1_bits[31] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(1.0F)),
        "native PSMTXConcat helper preserves observable volatile FPR/PS1 side effects");

    constexpr std::uint32_t trans_apply_src = 0x80001200u;
    constexpr std::uint32_t trans_apply_dst = 0x80001280u;
    const std::array<float, 12> trans_apply_input{
        1.0F, 2.0F, 3.0F, 4.0F,
        5.0F, 6.0F, 7.0F, 8.0F,
        9.0F, 10.0F, 11.0F, 12.0F,
    };
    for (std::size_t i = 0; i < trans_apply_input.size(); ++i) {
        galaxy::guest_store_u32(
            &memory,
            trans_apply_src + static_cast<std::uint32_t>(i * 4u),
            std::bit_cast<std::uint32_t>(trans_apply_input[i]),
            nullptr,
            0);
        galaxy::guest_store_u32(
            &memory,
            trans_apply_dst + static_cast<std::uint32_t>(i * 4u),
            0xDEADBEEFu,
            nullptr,
            0);
    }
    galaxy::PpcContext trans_apply_context{};
    trans_apply_context.hid2 = 0xA0000000u;
    trans_apply_context.gpr[3] = trans_apply_src;
    trans_apply_context.gpr[4] = trans_apply_dst;
    trans_apply_context.fpr_bits[1] = std::bit_cast<std::uint64_t>(10.0);
    trans_apply_context.fpr_bits[2] = std::bit_cast<std::uint64_t>(20.0);
    trans_apply_context.fpr_bits[3] = std::bit_cast<std::uint64_t>(30.0);
    galaxy::native_psmtx_trans_apply_804B63D8(
        &trans_apply_context, &memory, nullptr, 0x804B63D8u);
    const std::array<float, 12> expected_trans_apply{
        1.0F, 2.0F, 3.0F, 14.0F,
        5.0F, 6.0F, 7.0F, 28.0F,
        9.0F, 10.0F, 11.0F, 42.0F,
    };
    for (std::size_t i = 0; i < expected_trans_apply.size(); ++i) {
        passed &= expect(
            galaxy::guest_load_u32(
                &memory,
                trans_apply_dst + static_cast<std::uint32_t>(i * 4u),
                nullptr,
                0) == std::bit_cast<std::uint32_t>(expected_trans_apply[i]),
            "native PSMTXTransApply helper stores translated matrix result bits");
    }
    passed &= expect(
        trans_apply_context.fpr_bits[1] ==
            galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(10.0F)) &&
            trans_apply_context.fpr_bits[2] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(20.0F)) &&
            trans_apply_context.fpr_bits[3] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(30.0F)) &&
            trans_apply_context.fpr_bits[5] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(3.0F)) &&
            trans_apply_context.ps1_bits[5] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(14.0F)) &&
            trans_apply_context.fpr_bits[7] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(7.0F)) &&
            trans_apply_context.ps1_bits[7] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(28.0F)) &&
            trans_apply_context.fpr_bits[8] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(11.0F)) &&
            trans_apply_context.ps1_bits[8] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(42.0F)),
        "native PSMTXTransApply helper preserves FPR/PS1 side effects");

    constexpr std::uint32_t f32_denorm_min_bits = 0x00000001u;
    const std::uint32_t psq_denorm_expected =
        galaxy::narrow_paired_single_ftz(
            galaxy::widen_f32_bits(f32_denorm_min_bits));
    constexpr std::uint32_t trans_apply_ftz_src = 0x80001400u;
    constexpr std::uint32_t trans_apply_ftz_dst = 0x80001480u;
    for (std::size_t i = 0; i < trans_apply_input.size(); ++i) {
        galaxy::guest_store_u32(
            &memory,
            trans_apply_ftz_src + static_cast<std::uint32_t>(i * 4u),
            0u,
            nullptr,
            0);
    }
    galaxy::guest_store_u32(
        &memory, trans_apply_ftz_src, f32_denorm_min_bits, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, trans_apply_ftz_src + 4u, f32_denorm_min_bits, nullptr, 0);
    galaxy::PpcContext trans_apply_ftz_context{};
    trans_apply_ftz_context.hid2 = 0xA0000000u;
    trans_apply_ftz_context.gpr[3] = trans_apply_ftz_src;
    trans_apply_ftz_context.gpr[4] = trans_apply_ftz_dst;
    galaxy::native_psmtx_trans_apply_804B63D8(
        &trans_apply_ftz_context, &memory, nullptr, 0x804B63D8u);
    passed &= expect(
        galaxy::guest_load_u32(&memory, trans_apply_ftz_dst, nullptr, 0) ==
                psq_denorm_expected &&
            galaxy::guest_load_u32(&memory, trans_apply_ftz_dst + 4u, nullptr, 0) ==
                psq_denorm_expected,
        "native PSMTXTransApply paired stores use PSQ FTZ narrowing");

    constexpr std::uint32_t scale_matrix_dst = 0x80001300u;
    galaxy::guest_store_u32(
        &memory,
        0x800024B4u,
        std::bit_cast<std::uint32_t>(0.0F),
        nullptr,
        0);
    galaxy::PpcContext scale_matrix_context{};
    scale_matrix_context.hid2 = 0xA0000000u;
    scale_matrix_context.gpr[2] = 0x80000000u;
    scale_matrix_context.gpr[3] = scale_matrix_dst;
    scale_matrix_context.fpr_bits[1] = std::bit_cast<std::uint64_t>(2.0);
    scale_matrix_context.fpr_bits[2] = std::bit_cast<std::uint64_t>(3.0);
    scale_matrix_context.fpr_bits[3] = std::bit_cast<std::uint64_t>(4.0);
    galaxy::native_psmtx_scale_804B6424(
        &scale_matrix_context, &memory, nullptr, 0x804B6424u);
    const std::array<float, 12> expected_scale_matrix{
        2.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 3.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 4.0F, 0.0F,
    };
    for (std::size_t i = 0; i < expected_scale_matrix.size(); ++i) {
        passed &= expect(
            galaxy::guest_load_u32(
                &memory,
                scale_matrix_dst + static_cast<std::uint32_t>(i * 4u),
                nullptr,
                0) == std::bit_cast<std::uint32_t>(expected_scale_matrix[i]),
            "native PSMTXScale helper stores translated matrix result bits");
    }
    passed &= expect(
        scale_matrix_context.fpr_bits[0] ==
            galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(0.0F)) &&
            scale_matrix_context.ps1_bits[0] ==
                galaxy::widen_f32_bits(std::bit_cast<std::uint32_t>(0.0F)),
        "native PSMTXScale helper preserves loaded zero constant lanes");

    constexpr std::uint32_t scale_matrix_ftz_dst = 0x80001380u;
    galaxy::guest_store_u32(
        &memory,
        0x800024B4u,
        f32_denorm_min_bits,
        nullptr,
        0);
    galaxy::PpcContext scale_matrix_ftz_context{};
    scale_matrix_ftz_context.hid2 = 0xA0000000u;
    scale_matrix_ftz_context.gpr[2] = 0x80000000u;
    scale_matrix_ftz_context.gpr[3] = scale_matrix_ftz_dst;
    scale_matrix_ftz_context.fpr_bits[1] = std::bit_cast<std::uint64_t>(2.0);
    scale_matrix_ftz_context.fpr_bits[2] = std::bit_cast<std::uint64_t>(3.0);
    scale_matrix_ftz_context.fpr_bits[3] = std::bit_cast<std::uint64_t>(4.0);
    galaxy::native_psmtx_scale_804B6424(
        &scale_matrix_ftz_context, &memory, nullptr, 0x804B6424u);
    passed &= expect(
        galaxy::guest_load_u32(&memory, scale_matrix_ftz_dst + 4u, nullptr, 0) ==
                psq_denorm_expected &&
            galaxy::guest_load_u32(
                &memory, scale_matrix_ftz_dst + 8u, nullptr, 0) ==
                psq_denorm_expected,
        "native PSMTXScale paired stores use PSQ FTZ narrowing");

    constexpr std::uint32_t tchannel = 0x80001800u;
    galaxy::PpcContext bus_connect_context{};
    bus_connect_context.gpr[3] = tchannel;
    bus_connect_context.gpr[4] = 2u;
    bus_connect_context.gpr[5] = 9u;
    galaxy::native_jas_dsp_set_bus_connect_80495C3C(
        &bus_connect_context, &memory, nullptr, 0x80495C3Cu);
    passed &= expect(
        galaxy::guest_load_u16(&memory, tchannel + 0x20u, nullptr, 0) == 0x0FA0u,
        "native JASDsp bus-connect helper stores audited route table entry");
    passed &= expect(
        bus_connect_context.gpr[0] == 0x0FA0u &&
            bus_connect_context.gpr[3] == tchannel + 0x10u &&
            bus_connect_context.gpr[4] == 18u &&
            bus_connect_context.gpr[6] == 0x8054D2B0u,
        "native JASDsp bus-connect helper preserves translated volatile register effects");

    bus_connect_context = {};
    bus_connect_context.gpr[3] = tchannel;
    bus_connect_context.gpr[4] = 0u;
    bus_connect_context.gpr[5] = 0xFFu;
    galaxy::guest_store_u16(&memory, tchannel + 0x10u, 0x4861u, nullptr, 0);
    galaxy::native_jas_dsp_set_bus_connect_80495C3C(
        &bus_connect_context, &memory, nullptr, 0x80495C3Cu);
    passed &= expect(
        galaxy::guest_load_u16(&memory, tchannel + 0x10u, nullptr, 0) == 0u,
        "native JASDsp bus-connect helper treats 0xFF route as no connection");
    passed &= expect(
        bus_connect_context.gpr[0] == 0u &&
            bus_connect_context.gpr[3] == tchannel &&
            bus_connect_context.gpr[4] == 0x1FEu &&
            bus_connect_context.gpr[6] == 0x8054D2B0u,
        "native JASDsp bus-connect helper preserves 0xFF sentinel register effects");

    constexpr std::uint32_t dsp_running_flag = 0x80000180u;
    constexpr std::uint32_t dsp_r13 = dsp_running_flag + 0x20A8u;
    DspRunningProbe dsp_probe{};
    dsp_probe.flag_address = dsp_running_flag;
    galaxy::NativeServicesV1 dsp_services{};
    dsp_services.user = &dsp_probe;
    dsp_services.branch_checkpoint = &set_dsp_running_on_checkpoint;
    galaxy::PpcContext dsp_context{};
    dsp_context.gpr[13] = dsp_r13;
    galaxy::guest_store_u8(&memory, dsp_running_flag, 0u, nullptr, 0);
    galaxy::native_dsp_running_check_80496A60(
        &dsp_context, &memory, &dsp_services, 0x80496A60u);
    passed &= expect(
        dsp_probe.calls == 1u && dsp_probe.guest_pc == 0x80496A60u,
        "native DSP running check checkpoints a cold boot flag once");
    passed &= expect(
        galaxy::guest_load_u8(&memory, dsp_running_flag, nullptr, 0) == 1u &&
            dsp_context.gpr[3] == 1u,
        "native DSP running check re-reads the flag set by the checkpoint");

    dsp_probe.calls = 0u;
    dsp_probe.guest_pc = 0u;
    dsp_context = {};
    dsp_context.gpr[13] = dsp_r13;
    galaxy::guest_store_u8(&memory, dsp_running_flag, 1u, nullptr, 0);
    galaxy::native_dsp_running_check_80496A60(
        &dsp_context, &memory, &dsp_services, 0x80496A60u);
    passed &= expect(
        dsp_probe.calls == 0u && dsp_context.gpr[3] == 1u,
        "native DSP running check does not checkpoint an already-running flag");

    context.hid2 = 0xA0000000u;
    galaxy::guest_store_u32(
        &memory, 0x80000020u, std::bit_cast<std::uint32_t>(-2.5F), nullptr, 0);
    galaxy::load_fpr_single(&context, 6, &memory, 0x80000020u, nullptr, 0);
    passed &= expect(context.fpr_bits[6] == context.ps1_bits[6],
                     "single loads duplicate both lanes when HID2[PSE] is enabled");

    context.gqr[0] = 0;
    galaxy::guest_store_u32(
        &memory, 0x80000028u, std::bit_cast<std::uint32_t>(1.5F), nullptr, 0);
    galaxy::guest_store_u32(&memory, 0x8000002Cu, 0x00000001u, nullptr, 0);
    galaxy::psq_load(
        &context, 7, &memory, 0x80000028u, 0, false, true, nullptr, 0);
    passed &= expect(galaxy::fpr_as_double(&context, 7) == 1.5,
                     "paired float load widens lane zero");
    passed &= expect(context.ps1_bits[7] == 0x36A0000000000000ull,
                     "paired float load preserves a subnormal lane");
    galaxy::psq_load(
        &context, 7, &memory, 0x80000028u, 0, true, true, nullptr, 0);
    passed &= expect(context.ps1_bits[7] == 0x3FF0000000000000ull,
                     "single-element paired load sets lane one to 1.0");

    context.gqr[1] = (1u << 24) | (4u << 16);
    galaxy::guest_store_u8(&memory, 0x80000030u, 100u, nullptr, 0);
    galaxy::guest_store_u8(&memory, 0x80000031u, 200u, nullptr, 0);
    galaxy::psq_load(
        &context, 8, &memory, 0x80000030u, 1, false, true, nullptr, 0);
    passed &= expect(galaxy::fpr_as_double(&context, 8) == 50.0,
                     "unsigned byte paired load applies LD_SCALE");
    passed &= expect(galaxy::ps1_as_double(&context, 8) == 100.0,
                     "unsigned byte paired load advances one byte");

    context.gqr[2] = (63u << 24) | (7u << 16);
    galaxy::guest_store_u16(&memory, 0x80000034u, 0xFFFEu, nullptr, 0);
    galaxy::guest_store_u16(&memory, 0x80000036u, 3u, nullptr, 0);
    galaxy::psq_load(
        &context, 9, &memory, 0x80000034u, 2, false, true, nullptr, 0);
    passed &= expect(galaxy::fpr_as_double(&context, 9) == -4.0,
                     "signed halfword paired load sign-extends and applies negative scale");
    passed &= expect(galaxy::ps1_as_double(&context, 9) == 6.0,
                     "signed halfword paired load advances two bytes");

    context.gqr[3] = (1u << 8) | 4u;
    context.fpr_bits[10] = std::bit_cast<std::uint64_t>(100.0);
    context.ps1_bits[10] = std::bit_cast<std::uint64_t>(200.0);
    galaxy::psq_store(
        &context, 10, &memory, 0x80000038u, 3, false, true, nullptr, 0);
    passed &= expect(galaxy::guest_load_u8(&memory, 0x80000038u, nullptr, 0) == 200u,
                     "unsigned byte paired store applies ST_SCALE");
    passed &= expect(galaxy::guest_load_u8(&memory, 0x80000039u, nullptr, 0) == 255u,
                     "unsigned byte paired store saturates positive overflow");

    context.gqr[4] = 6u;
    context.fpr_bits[11] = 0xFFF0000000000000ull;
    context.ps1_bits[11] = 0x7FF8000000000001ull;
    galaxy::psq_store(
        &context, 11, &memory, 0x8000003Au, 4, false, true, nullptr, 0);
    passed &= expect(galaxy::guest_load_u8(&memory, 0x8000003Au, nullptr, 0) == 0x80u,
                     "signed byte paired store saturates negative infinity");
    passed &= expect(galaxy::guest_load_u8(&memory, 0x8000003Bu, nullptr, 0) == 0x7Fu,
                     "signed byte paired store maps NaN to positive overflow");

    context.gqr[5] = 4u;
    context.fpr_bits[12] = std::bit_cast<std::uint64_t>(-1.0);
    galaxy::psq_store(
        &context, 12, &memory, 0x8000003Cu, 5, true, true, nullptr, 0);
    passed &= expect(galaxy::guest_load_u8(&memory, 0x8000003Cu, nullptr, 0) == 0u,
                     "unsigned paired store clamps negative values to zero");

    context.gqr[6] = 0;
    context.fpr_bits[13] = 0xB6A0000000000000ull;
    context.ps1_bits[13] = 0x36A0000000000000ull;
    galaxy::psq_store(
        &context, 13, &memory, 0x80000040u, 6, false, true, nullptr, 0);
    passed &= expect(galaxy::guest_load_u32(&memory, 0x80000040u, nullptr, 0) ==
                         0x80000000u,
                     "paired float store flushes a negative denormal to signed zero");
    passed &= expect(galaxy::guest_load_u32(&memory, 0x80000044u, nullptr, 0) == 0u,
                     "paired float store flushes a positive denormal to zero");

    passed &= expect(galaxy::psq_load_scale(0x3F070000u) == -1,
                     "GQR load scale is a signed six-bit value");
    passed &= expect(galaxy::psq_store_scale(0x00003F07u) == -1,
                     "GQR store scale is a signed six-bit value");
    passed &= expect(galaxy::psq_load_type(0x3F070000u) == 7u,
                     "GQR load type extraction");
    passed &= expect(galaxy::psq_store_type(0x00003F07u) == 7u,
                     "GQR store type extraction");

    context.xer = 0x80000000u;
    galaxy::set_xer_ca(&context, true);
    passed &= expect(context.xer == 0xA0000000u, "setting XER carry preserves SO");
    galaxy::set_xer_ca(&context, false);
    passed &= expect(context.xer == 0x80000000u, "clearing XER carry preserves SO");

    passed &= expect(galaxy::arithmetic_shift_right(0x80000000u, 1) == 0xC0000000u,
                     "negative arithmetic right shift");
    passed &= expect(galaxy::arithmetic_shift_right(0x70000000u, 4) == 0x07000000u,
                     "positive arithmetic right shift");
    passed &= expect(galaxy::arithmetic_shift_right(0x81234567u, 0) == 0x81234567u,
                     "zero arithmetic right shift");

    galaxy::PpcContext cache_context{};
    cache_context.gpr[3] = 0x80000003u;
    cache_context.gpr[4] = 61u;
    cache_context.ctr = 123u;
    galaxy::native_cache_maintenance_range(
        &cache_context, &memory, nullptr, 0x804A2EF4u);
    passed &= expect(cache_context.gpr[3] == 0x80000043u,
                     "cache range advances r3 once per touched line");
    passed &= expect(cache_context.gpr[4] == 2u,
                     "cache range stores the computed line count in r4");
    passed &= expect(cache_context.gpr[5] == 3u,
                     "cache range stores the line offset in r5");
    passed &= expect(cache_context.ctr == 0u,
                     "cache range consumes the loop counter natively");
    passed &= expect((cache_context.cr >> 28) == 0x4u,
                     "cache range preserves the entry compare result");

    cache_context = {};
    cache_context.gpr[3] = 0x80000020u;
    cache_context.gpr[4] = 0u;
    cache_context.ctr = 7u;
    galaxy::native_cache_maintenance_range(
        &cache_context, &memory, nullptr, 0x804A2F80u);
    passed &= expect(cache_context.gpr[3] == 0x80000020u &&
                         cache_context.gpr[4] == 0u &&
                         cache_context.ctr == 7u,
                     "zero-size cache range returns before loop setup");
    passed &= expect((cache_context.cr >> 28) == 0x2u,
                     "zero-size cache range preserves the equality compare");

    cache_context = {};
    cache_context.gpr[3] = 0x80000020u;
    cache_context.ctr = 3u;
    galaxy::native_cache_maintenance_loop_tail(
        &cache_context, &memory, nullptr, 0x804A2F18u);
    passed &= expect(cache_context.gpr[3] == 0x80000060u,
                     "cache tail resume advances remaining loop iterations");
    passed &= expect(cache_context.ctr == 0u,
                     "cache tail resume consumes the remaining counter");

    SystemCallProbe system_call_probe{};
    galaxy::NativeServicesV1 cache_services{};
    cache_services.user = &system_call_probe;
    cache_services.system_call = &probe_system_call;
    cache_context = {};
    cache_context.gpr[3] = 0x80000000u;
    cache_context.gpr[4] = 32u;
    galaxy::native_cache_maintenance_range(
        &cache_context, &memory, &cache_services, 0x804A2F20u);
    passed &= expect(system_call_probe.calls == 1u &&
                         system_call_probe.guest_pc == 0x804A2F48u &&
                         system_call_probe.instruction == 0x44000002u,
                     "cache range preserves the generated system-call boundary");

    constexpr std::uint32_t gx_global_address = 0x80002600u;
    constexpr std::uint32_t gx_global_pointer_slot = 0x80002500u;
    constexpr std::uint32_t gx_color_address = 0x80000100u;
    galaxy::guest_store_u32(
        &memory, gx_global_pointer_slot, gx_global_address, nullptr, 0);
    galaxy::guest_store_u16(
        &memory, gx_global_address + 0x02u, 0xFFFFu, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, gx_color_address, 0x11223344u, nullptr, 0);
    FifoWriteProbe fifo_probe{};
    memory.user = &fifo_probe;
    memory.read_device = nullptr;
    memory.write_device = &capture_fifo_write;
    const std::array<std::byte, 8> translated_store_batch{{
        std::byte{0x61},
        std::byte{0x12},
        std::byte{0x34},
        std::byte{0x89},
        std::byte{0xAB},
        std::byte{0xCD},
        std::byte{0xEF},
        std::byte{0x55},
    }};
    galaxy::guest_write_wgpipe_bytes(
        &memory,
        0xCC008000u,
        translated_store_batch,
        nullptr,
        0x80400000u);
    passed &= expect(
        fifo_probe.writes == 1u &&
            fifo_probe.address == 0xCC008000u &&
            fifo_probe.size == translated_store_batch.size(),
        "statically proven WGPIPE batch performs one device publication");
    for (std::size_t i = 0; i < translated_store_batch.size(); ++i) {
        passed &= expect(
            fifo_probe.bytes[i] == translated_store_batch[i],
            "translated WGPIPE store batch preserves scalar big-endian order");
    }
    passed &= expect(
        galaxy::guest_is_wgpipe_address(0x0C008000u) &&
            galaxy::guest_is_wgpipe_address(0xCC008FFFu) &&
            !galaxy::guest_is_wgpipe_address(0x80000008u) &&
            fifo_probe.writes == 1u,
        "WGPIPE aliases remain explicit while ordinary memory is excluded");
    fifo_probe = {};
    galaxy::PpcContext gx_context{};
    gx_context.gpr[2] = 0x80000000u;
    gx_context.gpr[3] = 1u;
    gx_context.gpr[4] = gx_color_address;
    galaxy::native_gx_set_chan_color_804BD28C(
        &gx_context, &memory, nullptr, 0x804BD28Cu);
    memory.user = &device_probe;
    memory.read_device = &probe_read;
    memory.write_device = &probe_write;
    const std::uint32_t expected_first =
        (0xE2u << 24u) |
        (std::rotl(0x11223344u, 8) & 0x000000FFu) |
        (std::rotl(0x11223344u, 12) & 0x000FF000u);
    const std::uint32_t expected_second =
        (0xE3u << 24u) |
        (std::rotl(0x11223344u, 24) & 0x000000FFu) |
        (std::rotl(0x11223344u, 28) & 0x000FF000u);
    std::array<std::byte, 20> expected_fifo{};
    const auto write_packet = [&expected_fifo](std::size_t offset, std::uint32_t word) {
        expected_fifo[offset] = std::byte{0x61};
        expected_fifo[offset + 1] = static_cast<std::byte>((word >> 24u) & 0xFFu);
        expected_fifo[offset + 2] = static_cast<std::byte>((word >> 16u) & 0xFFu);
        expected_fifo[offset + 3] = static_cast<std::byte>((word >> 8u) & 0xFFu);
        expected_fifo[offset + 4] = static_cast<std::byte>(word & 0xFFu);
    };
    write_packet(0, expected_first);
    write_packet(5, expected_second);
    write_packet(10, expected_second);
    write_packet(15, expected_second);
    passed &= expect(
        fifo_probe.writes == 1u &&
            fifo_probe.address == 0xCC008000u &&
            fifo_probe.size == expected_fifo.size(),
        "GX channel color helper emits one native WGPIPE byte stream");
    for (std::size_t i = 0; i < expected_fifo.size(); ++i) {
        passed &= expect(
            fifo_probe.bytes[i] == expected_fifo[i],
            "GX channel color helper preserves BP packet bytes");
    }
    passed &= expect(
        galaxy::guest_load_u16(&memory, gx_global_address + 0x02u, nullptr, 0) == 0,
        "GX channel color helper clears the GX dirty flag");
    passed &= expect(
        gx_context.gpr[0] == 0u &&
            gx_context.gpr[3] == gx_global_address &&
            gx_context.gpr[4] == 0xCC010000u &&
            gx_context.gpr[5] == 0x61u &&
            gx_context.gpr[6] == expected_second &&
            gx_context.gpr[7] == expected_first &&
            gx_context.gpr[8] == 0x11223344u,
        "GX channel color helper preserves translated volatile register side effects");

    galaxy::guest_store_u16(
        &memory, gx_global_address + 0x02u, 0xFFFFu, nullptr, 0);
    galaxy::guest_store_u16(
        &memory, gx_global_address + 0x04u, 5u, nullptr, 0);
    galaxy::guest_store_u16(
        &memory, gx_global_address + 0x06u, 7u, nullptr, 0);
    FifoWriteProbe zero_prim_probe{};
    memory.user = &zero_prim_probe;
    memory.read_device = nullptr;
    memory.write_device = &capture_fifo_write;
    galaxy::PpcContext zero_prim_context{};
    zero_prim_context.gpr[2] = 0x80000000u;
    galaxy::native_gx_send_zero_primitive_804BA7FC(
        &zero_prim_context, &memory, nullptr, 0x804BA7FCu);
    memory.user = &device_probe;
    memory.read_device = &probe_read;
    memory.write_device = &probe_write;

    std::array<std::byte, 39> expected_zero_primitive{};
    expected_zero_primitive[0] = std::byte{0x98};
    expected_zero_primitive[1] = std::byte{0x00};
    expected_zero_primitive[2] = std::byte{0x05};
    passed &= expect(
        zero_prim_probe.writes == 3u &&
            zero_prim_probe.address == 0xCC008000u &&
            zero_prim_probe.size == expected_zero_primitive.size(),
        "GX zero primitive helper emits the native WGPIPE byte stream");
    for (std::size_t i = 0; i < expected_zero_primitive.size(); ++i) {
        passed &= expect(
            zero_prim_probe.bytes[i] == expected_zero_primitive[i],
            "GX zero primitive helper preserves packet and padding bytes");
    }
    passed &= expect(
        galaxy::guest_load_u16(&memory, gx_global_address + 0x02u, nullptr, 0) == 1u,
        "GX zero primitive helper marks the GX dirty flag");
    passed &= expect(
        zero_prim_context.gpr[0] == 1u &&
            zero_prim_context.gpr[3] == 0xCC010000u &&
            zero_prim_context.gpr[4] == 0u &&
            zero_prim_context.gpr[5] == 3u &&
            zero_prim_context.gpr[6] == gx_global_address &&
            zero_prim_context.gpr[7] == 32u &&
            zero_prim_context.gpr[8] == 35u &&
            zero_prim_context.ctr == 0u,
        "GX zero primitive helper preserves translated volatile register side effects");

    constexpr std::uint32_t primitive_begin_gx_address = 0x80001800u;
    galaxy::guest_store_u32(
        &memory, gx_global_pointer_slot, primitive_begin_gx_address, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, primitive_begin_gx_address + 0x00u, 1u, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, primitive_begin_gx_address + 0x5FCu, 0u, nullptr, 0);
    FifoWriteProbe primitive_begin_probe{};
    memory.user = &primitive_begin_probe;
    memory.read_device = nullptr;
    memory.write_device = &capture_fifo_write;
    constexpr std::uint32_t primitive_begin_stack = 0x80002700u;
    galaxy::PpcContext primitive_begin_context{};
    primitive_begin_context.gpr[1] = primitive_begin_stack;
    primitive_begin_context.gpr[2] = 0x80000000u;
    primitive_begin_context.gpr[3] = 0x80u;
    primitive_begin_context.gpr[4] = 0u;
    primitive_begin_context.gpr[5] = 4u;
    primitive_begin_context.gpr[28] = 0x28282828u;
    primitive_begin_context.gpr[29] = 0x29292929u;
    primitive_begin_context.gpr[30] = 0x30303030u;
    primitive_begin_context.gpr[31] = 0x31313131u;
    primitive_begin_context.lr = 0x81818181u;
    galaxy::native_gx_begin_804BA6B0(
        &primitive_begin_context, &memory, nullptr, 0x804BA6B0u);
    galaxy::guest_store_u32(
        &memory, gx_global_pointer_slot, gx_global_address, nullptr, 0);
    memory.user = &device_probe;
    memory.read_device = &probe_read;
    memory.write_device = &probe_write;
    const std::array<std::byte, 3> expected_primitive_begin{
        std::byte{0x80}, std::byte{0x00}, std::byte{0x04}};
    passed &= expect(
        primitive_begin_probe.writes == 2u &&
            primitive_begin_probe.address == 0xCC008000u &&
            primitive_begin_probe.size == expected_primitive_begin.size(),
        "GX primitive-begin helper emits opcode plus u16 count");
    for (std::size_t i = 0; i < expected_primitive_begin.size(); ++i) {
        passed &= expect(
            primitive_begin_probe.bytes[i] == expected_primitive_begin[i],
            "GX primitive-begin helper preserves count bytes");
    }
    passed &= expect(
        primitive_begin_context.gpr[1] == primitive_begin_stack &&
            primitive_begin_context.lr == 0x81818181u &&
            primitive_begin_context.gpr[0] == 0x81818181u &&
            primitive_begin_context.gpr[28] == 0x28282828u &&
            primitive_begin_context.gpr[29] == 0x29292929u &&
            primitive_begin_context.gpr[30] == 0x30303030u &&
            primitive_begin_context.gpr[31] == 0x31313131u,
        "GX primitive-begin helper preserves translated stack and callee-save state");

    // The helper's nested GX dirty-state call is an architectural interrupt
    // boundary. Force that return checkpoint to unwind and prove that the
    // exact guest frame needed by continuation 0x804BA6EC survives without
    // relying on any C++ local.
    galaxy::guest_store_u32(
        &memory, gx_global_pointer_slot, primitive_begin_gx_address, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, primitive_begin_gx_address + 0x00u, 1u, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, primitive_begin_gx_address + 0x5FCu, 1u, nullptr, 0);
    RestartabilityProbe restartability_probe{};
    std::atomic<std::uint32_t> restartability_pending_event_mask{1u};
    galaxy::NativeServicesV1 restartability_services{};
    restartability_services.user = &restartability_probe;
    restartability_services.call_guest_cached =
        &return_from_cached_call_for_restartability;
    restartability_services.branch_checkpoint =
        &throw_guest_transfer_on_checkpoint;
    restartability_services.pending_event_mask =
        &restartability_pending_event_mask;
    galaxy::PpcContext restartability_context{};
    restartability_context.gpr[1] = primitive_begin_stack;
    restartability_context.gpr[2] = 0x80000000u;
    restartability_context.gpr[3] = 0x80u;
    restartability_context.gpr[4] = 2u;
    restartability_context.gpr[5] = 4u;
    restartability_context.gpr[28] = 0x28282828u;
    restartability_context.gpr[29] = 0x29292929u;
    restartability_context.gpr[30] = 0x30303030u;
    restartability_context.gpr[31] = 0x31313131u;
    restartability_context.lr = 0x81818181u;
    bool restartability_transfer_observed = false;
    try {
        galaxy::native_gx_begin_804BA6B0(
            &restartability_context,
            &memory,
            &restartability_services,
            0x804BA6B0u);
    } catch (const SimulatedGuestTransfer&) {
        restartability_transfer_observed = true;
    }
    constexpr std::uint32_t restartability_frame =
        primitive_begin_stack - 0x20u;
    passed &= expect(
        restartability_transfer_observed &&
            restartability_probe.guest_calls == 1u &&
            restartability_probe.guest_address == 0x804BA438u &&
            restartability_probe.checkpoints == 1u &&
            restartability_probe.checkpoint_pc == 0x804BA6E8u &&
            restartability_context.pc == 0x804BA6ECu &&
            restartability_context.lr == 0x804BA6ECu &&
            restartability_context.gpr[1] == restartability_frame,
        "GX primitive-begin helper leaves an exact resumable continuation on guest transfer");
    passed &= expect(
        galaxy::guest_load_u32(
            &memory, restartability_frame + 0x00u, nullptr, 0) ==
                primitive_begin_stack &&
            galaxy::guest_load_u32(
                &memory, restartability_frame + 0x24u, nullptr, 0) == 0x81818181u &&
            galaxy::guest_load_u32(
                &memory, restartability_frame + 0x10u, nullptr, 0) == 0x28282828u &&
            galaxy::guest_load_u32(
                &memory, restartability_frame + 0x14u, nullptr, 0) == 0x29292929u &&
            galaxy::guest_load_u32(
                &memory, restartability_frame + 0x18u, nullptr, 0) == 0x30303030u &&
            galaxy::guest_load_u32(
                &memory, restartability_frame + 0x1Cu, nullptr, 0) == 0x31313131u &&
            restartability_context.gpr[28] == 0x80u &&
            restartability_context.gpr[29] == 2u &&
            restartability_context.gpr[30] == 4u &&
            restartability_context.gpr[31] == primitive_begin_gx_address,
        "GX primitive-begin helper materializes every continuation-owned register in guest memory");
    galaxy::guest_store_u32(
        &memory, gx_global_pointer_slot, gx_global_address, nullptr, 0);

    constexpr std::uint32_t load_mtx_address = 0x80000300u;
    std::array<std::uint32_t, 12> load_mtx_words{};
    for (std::size_t i = 0; i < load_mtx_words.size(); ++i) {
        load_mtx_words[i] =
            std::bit_cast<std::uint32_t>(static_cast<float>(i + 1u) + 0.25F);
        galaxy::guest_store_u32(
            &memory,
            load_mtx_address + static_cast<std::uint32_t>(i * 4u),
            load_mtx_words[i],
            nullptr,
            0);
    }
    FifoWriteProbe load_mtx_fifo_probe{};
    memory.user = &load_mtx_fifo_probe;
    memory.read_device = nullptr;
    memory.write_device = &capture_fifo_write;
    galaxy::PpcContext load_mtx_context{};
    load_mtx_context.hid2 = 0xA0000000u;
    load_mtx_context.gpr[3] = load_mtx_address;
    load_mtx_context.gpr[4] = 3u;
    galaxy::native_gx_load_pos_mtx_imm_804BE1D8(
        &load_mtx_context, &memory, nullptr, 0x804BE1D8u);
    memory.user = &device_probe;
    memory.read_device = &probe_read;
    memory.write_device = &probe_write;

    std::array<std::byte, 53> expected_load_mtx_fifo{};
    expected_load_mtx_fifo[0] = std::byte{0x10};
    const std::uint32_t expected_load_mtx_index = 0x000B000Cu;
    expected_load_mtx_fifo[1] =
        static_cast<std::byte>((expected_load_mtx_index >> 24u) & 0xFFu);
    expected_load_mtx_fifo[2] =
        static_cast<std::byte>((expected_load_mtx_index >> 16u) & 0xFFu);
    expected_load_mtx_fifo[3] =
        static_cast<std::byte>((expected_load_mtx_index >> 8u) & 0xFFu);
    expected_load_mtx_fifo[4] =
        static_cast<std::byte>(expected_load_mtx_index & 0xFFu);
    for (std::size_t i = 0; i < load_mtx_words.size(); ++i) {
        const std::uint32_t word = load_mtx_words[i];
        const std::size_t offset = 5u + i * 4u;
        expected_load_mtx_fifo[offset] =
            static_cast<std::byte>((word >> 24u) & 0xFFu);
        expected_load_mtx_fifo[offset + 1u] =
            static_cast<std::byte>((word >> 16u) & 0xFFu);
        expected_load_mtx_fifo[offset + 2u] =
            static_cast<std::byte>((word >> 8u) & 0xFFu);
        expected_load_mtx_fifo[offset + 3u] =
            static_cast<std::byte>(word & 0xFFu);
    }
    passed &= expect(
        load_mtx_fifo_probe.writes == 1u &&
            load_mtx_fifo_probe.address == 0xCC008000u &&
            load_mtx_fifo_probe.size == expected_load_mtx_fifo.size(),
        "GX position matrix helper batches the native WGPIPE byte stream");
    for (std::size_t i = 0; i < expected_load_mtx_fifo.size(); ++i) {
        passed &= expect(
            load_mtx_fifo_probe.bytes[i] == expected_load_mtx_fifo[i],
            "GX position matrix helper preserves matrix FIFO bytes");
    }
    passed &= expect(
        load_mtx_context.gpr[0] == expected_load_mtx_index &&
            load_mtx_context.gpr[4] == 0xCC008000u &&
            load_mtx_context.gpr[5] == 0xCC010000u &&
            load_mtx_context.fpr_bits[5] ==
                galaxy::widen_f32_bits(load_mtx_words[0]) &&
            load_mtx_context.ps1_bits[5] ==
                galaxy::widen_f32_bits(load_mtx_words[1]) &&
            load_mtx_context.fpr_bits[0] ==
                galaxy::widen_f32_bits(load_mtx_words[10]) &&
            load_mtx_context.ps1_bits[0] ==
                galaxy::widen_f32_bits(load_mtx_words[11]),
        "GX position matrix helper preserves volatile register and PSQ side effects");

    constexpr std::uint32_t gx_begin_global_address = 0x80001800u;
    galaxy::guest_store_u32(
        &memory, gx_global_pointer_slot, gx_begin_global_address, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, gx_begin_global_address + 0x00u, 1u, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, gx_begin_global_address + 0x5FCu, 1u, nullptr, 0);
    FifoWriteProbe gx_begin_fifo_probe{};
    CachedCallProbe gx_begin_call_probe{};
    galaxy::NativeServicesV1 gx_begin_services{};
    gx_begin_services.user = &gx_begin_call_probe;
    gx_begin_services.call_guest_cached = &capture_cached_call;
    memory.user = &gx_begin_fifo_probe;
    memory.read_device = nullptr;
    memory.write_device = &capture_fifo_write;
    galaxy::PpcContext gx_begin_context{};
    gx_begin_context.gpr[1] = 0x80002700u;
    gx_begin_context.gpr[2] = 0x80000000u;
    gx_begin_context.gpr[3] = 0x98u;
    gx_begin_context.gpr[4] = 0x1234u;
    gx_begin_context.gpr[29] = 0x29292929u;
    gx_begin_context.gpr[30] = 0x30303030u;
    gx_begin_context.gpr[31] = 0x31313131u;
    gx_begin_context.lr = 0x81234567u;
    galaxy::native_gx_begin_804BDEA8(
        &gx_begin_context, &memory, &gx_begin_services, 0x804BDEA8u);
    memory.user = &device_probe;
    memory.read_device = &probe_read;
    memory.write_device = &probe_write;

    const std::array<std::byte, 9> expected_gx_begin_fifo{
        std::byte{0x40},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x98},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x12}, std::byte{0x34},
    };
    passed &= expect(
        gx_begin_call_probe.calls == 1u &&
            gx_begin_call_probe.guest_address == 0x804BA438u &&
            gx_begin_call_probe.lr == 0x804BDEDCu &&
            gx_begin_call_probe.cache_slots_present,
        "GX begin helper preserves the hard-failing dirty-state guest-call boundary");
    passed &= expect(
        gx_begin_fifo_probe.writes == 3u &&
            gx_begin_fifo_probe.address == 0xCC008000u &&
            gx_begin_fifo_probe.size == expected_gx_begin_fifo.size(),
        "GX begin helper emits primitive command FIFO writes");
    for (std::size_t i = 0; i < expected_gx_begin_fifo.size(); ++i) {
        passed &= expect(
            gx_begin_fifo_probe.bytes[i] == expected_gx_begin_fifo[i],
        "GX begin helper preserves primitive command bytes");
    }
    passed &= expect(
        gx_begin_context.gpr[1] == 0x80002700u &&
            gx_begin_context.lr == 0x81234567u &&
            gx_begin_context.gpr[0] == 0x81234567u &&
            gx_begin_context.gpr[29] == 0x29292929u &&
            gx_begin_context.gpr[30] == 0x30303030u &&
            gx_begin_context.gpr[31] == 0x31313131u,
        "GX begin helper preserves translated stack and callee-save side effects");

    galaxy::guest_store_u32(
        &memory, gx_begin_global_address + 0x00u, 0u, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, gx_begin_global_address + 0x5FCu, 0u, nullptr, 0);
    galaxy::guest_store_u16(
        &memory, gx_begin_global_address + 0x04u, 0u, nullptr, 0);
    galaxy::guest_store_u16(
        &memory, gx_begin_global_address + 0x06u, 0u, nullptr, 0);
    FifoWriteProbe gx_begin_zero_fifo_probe{};
    memory.user = &gx_begin_zero_fifo_probe;
    memory.read_device = nullptr;
    memory.write_device = &capture_fifo_write;
    galaxy::PpcContext gx_begin_zero_context{};
    gx_begin_zero_context.gpr[1] = 0x80002760u;
    gx_begin_zero_context.gpr[2] = 0x80000000u;
    gx_begin_zero_context.gpr[3] = 0x80u;
    gx_begin_zero_context.gpr[4] = 0x0002u;
    gx_begin_zero_context.lr = 0x87654321u;
    galaxy::native_gx_begin_804BDEA8(
        &gx_begin_zero_context, &memory, &gx_begin_services, 0x804BDEA8u);
    memory.user = &device_probe;
    memory.read_device = &probe_read;
    memory.write_device = &probe_write;
    const std::array<std::byte, 12> expected_gx_begin_zero_fifo{
        std::byte{0x98}, std::byte{0x00}, std::byte{0x00},
        std::byte{0x40},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x80},
        std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x02},
    };
    passed &= expect(
        gx_begin_zero_fifo_probe.writes == 4u &&
            gx_begin_zero_fifo_probe.size == expected_gx_begin_zero_fifo.size(),
        "GX begin helper preserves zero-primitive integration writes");
    for (std::size_t i = 0; i < expected_gx_begin_zero_fifo.size(); ++i) {
        passed &= expect(
            gx_begin_zero_fifo_probe.bytes[i] == expected_gx_begin_zero_fifo[i],
            "GX begin helper preserves zero-primitive integration bytes");
    }
    passed &= expect(
        galaxy::guest_load_u16(&memory, gx_begin_global_address + 0x02u, nullptr, 0) == 1u &&
            gx_begin_zero_context.gpr[1] == 0x80002760u &&
            gx_begin_zero_context.lr == 0x87654321u,
        "GX begin helper keeps zero-primitive side effects and restores its frame");
    galaxy::guest_store_u32(
        &memory, gx_global_pointer_slot, gx_global_address, nullptr, 0);

    constexpr std::uint32_t jpa_regist_holder = 0x80001080u;
    constexpr std::uint32_t jpa_regist_child_shape = 0x80001100u;
    constexpr std::uint32_t jpa_regist_work = 0x80001200u;
    constexpr std::uint32_t jpa_regist_stack = 0x80002000u;
    constexpr std::uint32_t jpa_regist_frame = jpa_regist_stack - 0x30u;
    constexpr std::uint32_t jpa_regist_lr = 0x81234567u;
    galaxy::guest_store_u32(
        &memory, jpa_regist_holder, jpa_regist_child_shape, nullptr, 0);
    galaxy::guest_store_u8(&memory, jpa_regist_child_shape + 0xB8u, 0xFFu, nullptr, 0);
    galaxy::guest_store_u8(&memory, jpa_regist_child_shape + 0xB9u, 0x7Fu, nullptr, 0);
    galaxy::guest_store_u8(&memory, jpa_regist_child_shape + 0xBAu, 0x3Fu, nullptr, 0);
    galaxy::guest_store_u8(&memory, jpa_regist_child_shape + 0xBBu, 0x7Fu, nullptr, 0);
    galaxy::guest_store_u8(&memory, jpa_regist_child_shape + 0xBCu, 0x0Fu, nullptr, 0);
    galaxy::guest_store_u8(&memory, jpa_regist_child_shape + 0xBDu, 0xFFu, nullptr, 0);
    galaxy::guest_store_u8(&memory, jpa_regist_child_shape + 0xBEu, 0x7Fu, nullptr, 0);
    galaxy::guest_store_u8(&memory, jpa_regist_work + 0x8Cu, 0x80u, nullptr, 0);
    galaxy::guest_store_u8(&memory, jpa_regist_work + 0x8Du, 0x40u, nullptr, 0);
    galaxy::guest_store_u8(&memory, jpa_regist_work + 0x8Eu, 0x20u, nullptr, 0);
    galaxy::guest_store_u8(&memory, jpa_regist_work + 0x8Fu, 0xF0u, nullptr, 0);
    galaxy::guest_store_u8(&memory, jpa_regist_work + 0x90u, 0x10u, nullptr, 0);
    galaxy::guest_store_u8(&memory, jpa_regist_work + 0x91u, 0x80u, nullptr, 0);
    galaxy::guest_store_u8(&memory, jpa_regist_work + 0x92u, 0xC0u, nullptr, 0);
    galaxy::guest_store_u8(&memory, jpa_regist_work + 0x93u, 0x7Fu, nullptr, 0);
    galaxy::guest_store_u8(&memory, jpa_regist_work + 0x96u, 0x80u, nullptr, 0);
    galaxy::guest_store_u16(
        &memory, gx_global_address + 0x02u, 0xFFFFu, nullptr, 0);
    FifoWriteProbe jpa_regist_fifo_probe{};
    memory.user = &jpa_regist_fifo_probe;
    memory.read_device = nullptr;
    memory.write_device = &capture_fifo_write;
    galaxy::PpcContext jpa_regist_context{};
    jpa_regist_context.gpr[1] = jpa_regist_stack;
    jpa_regist_context.gpr[2] = 0x80000000u;
    jpa_regist_context.gpr[3] = jpa_regist_holder;
    jpa_regist_context.gpr[4] = jpa_regist_work;
    jpa_regist_context.gpr[26] = 0x26262626u;
    jpa_regist_context.gpr[27] = 0x27272727u;
    jpa_regist_context.gpr[28] = 0x28282828u;
    jpa_regist_context.gpr[29] = 0x29292929u;
    jpa_regist_context.gpr[30] = 0x30303030u;
    jpa_regist_context.gpr[31] = 0x31313131u;
    jpa_regist_context.lr = jpa_regist_lr;
    galaxy::native_jpa_regist_child_prm_env_8044432C(
        &jpa_regist_context, &memory, nullptr, 0x8044432Cu);
    memory.user = &device_probe;
    memory.read_device = &probe_read;
    memory.write_device = &probe_write;

    const std::uint32_t expected_jpa_prm_color = 0x8020083Cu;
    const std::uint32_t expected_jpa_env_color = 0x0180607Fu;
    std::array<std::byte, 40> expected_jpa_regist_fifo{};
    const auto write_jpa_color_packets =
        [&expected_jpa_regist_fifo](std::size_t offset,
                                    std::uint32_t channel_index,
                                    std::uint32_t color) {
            const std::uint32_t channel = (channel_index << 1u) & 0xFFFFFFFEu;
            const std::uint32_t first =
                (((channel + 0xE0u) << 24u) & 0xFF000000u) |
                (std::rotl(color, 8) & 0x000000FFu) |
                (std::rotl(color, 12) & 0x000FF000u);
            const std::uint32_t second =
                (((channel + 0xE1u) << 24u) & 0xFF000000u) |
                (std::rotl(color, 24) & 0x000000FFu) |
                (std::rotl(color, 28) & 0x000FF000u);
            const auto write_word = [&expected_jpa_regist_fifo](
                                        std::size_t packet_offset,
                                        std::uint32_t word) {
                expected_jpa_regist_fifo[packet_offset] = std::byte{0x61};
                expected_jpa_regist_fifo[packet_offset + 1] =
                    static_cast<std::byte>((word >> 24u) & 0xFFu);
                expected_jpa_regist_fifo[packet_offset + 2] =
                    static_cast<std::byte>((word >> 16u) & 0xFFu);
                expected_jpa_regist_fifo[packet_offset + 3] =
                    static_cast<std::byte>((word >> 8u) & 0xFFu);
                expected_jpa_regist_fifo[packet_offset + 4] =
                    static_cast<std::byte>(word & 0xFFu);
            };
            write_word(offset, first);
            write_word(offset + 5u, second);
            write_word(offset + 10u, second);
            write_word(offset + 15u, second);
        };
    write_jpa_color_packets(0u, 1u, expected_jpa_prm_color);
    write_jpa_color_packets(20u, 2u, expected_jpa_env_color);
    passed &= expect(
        jpa_regist_fifo_probe.writes == 1u &&
            jpa_regist_fifo_probe.address == 0xCC008000u &&
            jpa_regist_fifo_probe.size == expected_jpa_regist_fifo.size(),
        "JPA child color helper batches two GX color FIFO streams");
    for (std::size_t i = 0; i < expected_jpa_regist_fifo.size(); ++i) {
        passed &= expect(
            jpa_regist_fifo_probe.bytes[i] == expected_jpa_regist_fifo[i],
            "JPA child color helper preserves GX packet bytes");
    }
    passed &= expect(
        galaxy::guest_load_u32(&memory, jpa_regist_frame, nullptr, 0) ==
                jpa_regist_stack &&
            galaxy::guest_load_u32(&memory, jpa_regist_frame + 0x34u, nullptr, 0) ==
                jpa_regist_lr &&
            galaxy::guest_load_u32(&memory, jpa_regist_frame + 0x18u, nullptr, 0) ==
                0x26262626u &&
            galaxy::guest_load_u32(&memory, jpa_regist_frame + 0x2Cu, nullptr, 0) ==
                0x31313131u,
        "JPA child color helper preserves generated stack frame stores");
    passed &= expect(
        galaxy::guest_load_u32(&memory, jpa_regist_frame + 0x0Cu, nullptr, 0) ==
                expected_jpa_prm_color &&
            galaxy::guest_load_u32(&memory, jpa_regist_frame + 0x08u, nullptr, 0) ==
                expected_jpa_env_color,
        "JPA child color helper preserves generated color scratch structs");
    passed &= expect(
        galaxy::guest_load_u16(&memory, gx_global_address + 0x02u, nullptr, 0) == 0u,
        "JPA child color helper clears the GX dirty flag through GXSetTevColor");
    const std::uint32_t expected_jpa_env_first =
        (0xE4u << 24u) |
        (std::rotl(expected_jpa_env_color, 8) & 0x000000FFu) |
        (std::rotl(expected_jpa_env_color, 12) & 0x000FF000u);
    const std::uint32_t expected_jpa_env_second =
        (0xE5u << 24u) |
        (std::rotl(expected_jpa_env_color, 24) & 0x000000FFu) |
        (std::rotl(expected_jpa_env_color, 28) & 0x000FF000u);
    passed &= expect(
        jpa_regist_context.gpr[1] == jpa_regist_stack &&
            jpa_regist_context.gpr[0] == jpa_regist_lr &&
            jpa_regist_context.lr == jpa_regist_lr &&
            jpa_regist_context.gpr[3] == gx_global_address &&
            jpa_regist_context.gpr[4] == 0xCC010000u &&
            jpa_regist_context.gpr[5] == 0x61u &&
            jpa_regist_context.gpr[6] == expected_jpa_env_second &&
            jpa_regist_context.gpr[7] == expected_jpa_env_first &&
            jpa_regist_context.gpr[8] == expected_jpa_env_color &&
            jpa_regist_context.gpr[9] == 0x78u &&
            jpa_regist_context.gpr[10] == 0x08u &&
            jpa_regist_context.gpr[11] == jpa_regist_stack &&
            jpa_regist_context.gpr[12] == 0x80u &&
            jpa_regist_context.gpr[26] == 0x26262626u &&
            jpa_regist_context.gpr[27] == 0x27272727u &&
            jpa_regist_context.gpr[28] == 0x28282828u &&
            jpa_regist_context.gpr[29] == 0x29292929u &&
            jpa_regist_context.gpr[30] == 0x30303030u &&
            jpa_regist_context.gpr[31] == 0x31313131u &&
            jpa_regist_context.pc == 0x80517578u,
        "JPA child color helper preserves translated epilogue and volatile side effects");

    constexpr std::uint32_t jpa_owner_slot = 0x80000200u;
    constexpr std::uint32_t jpa_owner = 0x80000220u;
    constexpr std::uint32_t jpa_callback = 0x80000340u;
    constexpr std::uint32_t jpa_vtable = 0x80000380u;
    constexpr std::uint32_t jpa_particle = 0x80000400u;
    constexpr std::uint32_t jpa_target_raw = 0x80401235u;
    galaxy::guest_store_u32(&memory, jpa_owner_slot, jpa_owner, nullptr, 0);
    galaxy::guest_store_u32(&memory, jpa_owner + 0xF0u, 0u, nullptr, 0);
    galaxy::PpcContext jpa_context{};
    jpa_context.gpr[3] = jpa_owner_slot;
    jpa_context.gpr[4] = jpa_particle;
    jpa_context.ctr = 0x12345678u;
    galaxy::native_jpa_emitter_callback_8044592C(
        &jpa_context, &memory, nullptr, 0x8044592Cu);
    passed &= expect(
        jpa_context.gpr[3] == 0u &&
            jpa_context.gpr[4] == jpa_particle &&
            jpa_context.gpr[5] == jpa_particle &&
            jpa_context.gpr[6] == jpa_owner &&
            jpa_context.ctr == 0x12345678u &&
            ((jpa_context.cr >> 28) & 0xFu) == 0x2u,
        "JPA callback helper preserves null-callback register and CR side effects");

    galaxy::guest_store_u32(&memory, jpa_owner + 0xF0u, jpa_callback, nullptr, 0);
    galaxy::guest_store_u32(&memory, jpa_callback, jpa_vtable, nullptr, 0);
    galaxy::guest_store_u32(&memory, jpa_vtable + 0x10u, jpa_target_raw, nullptr, 0);
    CachedCallProbe cached_probe{};
    galaxy::NativeServicesV1 cached_services{};
    cached_services.user = &cached_probe;
    cached_services.call_guest_cached = &capture_cached_call;
    jpa_context = {};
    jpa_context.gpr[3] = jpa_owner_slot;
    jpa_context.gpr[4] = jpa_particle;
    jpa_context.lr = 0x80443424u;
    galaxy::native_jpa_emitter_callback_8044592C(
        &jpa_context, &memory, &cached_services, 0x8044592Cu);
    passed &= expect(
        cached_probe.calls == 1u &&
            cached_probe.guest_address == (jpa_target_raw & 0xFFFFFFFCu) &&
            cached_probe.cache_slots_present,
        "JPA callback helper dispatches through the cached guest-call service");
    passed &= expect(
        cached_probe.gpr3 == jpa_callback &&
            cached_probe.gpr4 == jpa_owner &&
            cached_probe.gpr5 == jpa_particle &&
            cached_probe.gpr6 == jpa_owner &&
            cached_probe.gpr12 == jpa_target_raw &&
            cached_probe.ctr == jpa_target_raw &&
            cached_probe.lr == 0x80443424u,
        "JPA callback helper preserves the original bctr argument setup");
    passed &= expect(
        jpa_context.gpr[3] == 0xC0DEC0DEu &&
            jpa_context.pc == (jpa_target_raw & 0xFFFFFFFCu),
        "JPA callback helper returns after the native guest-call boundary");

    constexpr std::uint32_t jpa_resource = 0x80001000u;
    constexpr std::uint32_t jpa_work = 0x80001200u;
    constexpr std::uint32_t jpa_child_particle = 0x80001300u;
    constexpr std::uint32_t jpa_calc_child_list = 0x80001400u;
    constexpr std::uint32_t jpa_stack = 0x80002000u;
    constexpr std::uint32_t jpa_calc_child_target0 = 0x80402021u;
    constexpr std::uint32_t jpa_calc_child_target1 = 0x80403045u;
    constexpr std::uint32_t jpa_caller_lr = 0x81234567u;
    galaxy::guest_store_u32(&memory, jpa_resource + 0x14u, 0u, nullptr, 0);
    galaxy::PpcContext jpa_calc_context{};
    jpa_calc_context.gpr[1] = jpa_stack;
    jpa_calc_context.gpr[3] = jpa_resource;
    jpa_calc_context.gpr[4] = jpa_work;
    jpa_calc_context.gpr[5] = jpa_child_particle;
    jpa_calc_context.gpr[27] = 0x27272727u;
    jpa_calc_context.gpr[28] = 0x28282828u;
    jpa_calc_context.gpr[29] = 0x29292929u;
    jpa_calc_context.gpr[30] = 0x30303030u;
    jpa_calc_context.gpr[31] = 0x31313131u;
    jpa_calc_context.lr = jpa_caller_lr;
    galaxy::native_jpa_calc_child_func_list_80443904(
        &jpa_calc_context, &memory, nullptr, 0x80443904u);
    passed &= expect(
        jpa_calc_context.gpr[1] == jpa_stack &&
            jpa_calc_context.gpr[3] == jpa_resource &&
            jpa_calc_context.gpr[27] == 0x27272727u &&
            jpa_calc_context.gpr[28] == 0x28282828u &&
            jpa_calc_context.gpr[29] == 0x29292929u &&
            jpa_calc_context.gpr[30] == 0x30303030u &&
            jpa_calc_context.gpr[31] == 0x31313131u &&
            jpa_calc_context.gpr[11] == jpa_stack &&
            jpa_calc_context.lr == jpa_caller_lr &&
            ((jpa_calc_context.cr >> 28) & 0xFu) == 0x2u,
        "JPA calc_c helper preserves null-list epilogue and register state");

    galaxy::guest_store_u32(&memory, jpa_resource + 0x14u, jpa_calc_child_list, nullptr, 0);
    galaxy::guest_store_u8(&memory, jpa_resource + 0x46u, 2u, nullptr, 0);
    galaxy::guest_store_u32(&memory, jpa_calc_child_list, jpa_calc_child_target0, nullptr, 0);
    galaxy::guest_store_u32(&memory, jpa_calc_child_list + 4u, jpa_calc_child_target1, nullptr, 0);
    CachedCallProbe jpa_calc_probe{};
    galaxy::NativeServicesV1 jpa_calc_services{};
    jpa_calc_services.user = &jpa_calc_probe;
    jpa_calc_services.call_guest_cached = &capture_cached_call;
    jpa_calc_services.branch_checkpoint = &capture_branch_checkpoint;
    jpa_calc_context = {};
    jpa_calc_context.gpr[1] = jpa_stack;
    jpa_calc_context.gpr[3] = jpa_resource;
    jpa_calc_context.gpr[4] = jpa_work;
    jpa_calc_context.gpr[5] = jpa_child_particle;
    jpa_calc_context.gpr[27] = 0x27272727u;
    jpa_calc_context.gpr[28] = 0x28282828u;
    jpa_calc_context.gpr[29] = 0x29292929u;
    jpa_calc_context.gpr[30] = 0x30303030u;
    jpa_calc_context.gpr[31] = 0x31313131u;
    jpa_calc_context.lr = jpa_caller_lr;
    galaxy::native_jpa_calc_child_func_list_80443904(
        &jpa_calc_context, &memory, &jpa_calc_services, 0x80443904u);
    passed &= expect(
        jpa_calc_probe.calls == 2u &&
            jpa_calc_probe.guest_addresses[0] == (jpa_calc_child_target1 & 0xFFFFFFFCu) &&
            jpa_calc_probe.guest_addresses[1] == (jpa_calc_child_target0 & 0xFFFFFFFCu) &&
            jpa_calc_probe.cache_slots_present,
        "JPA calc_c helper dispatches child callbacks in descending table order");
    passed &= expect(
        jpa_calc_probe.branch_checkpoints == 2u &&
            jpa_calc_probe.branch_pc == 0x80443964u &&
            jpa_calc_probe.branch_resume_pc == 0x80443940u &&
            jpa_calc_probe.branch_gpr30 == 0u &&
            jpa_calc_probe.branch_gpr31 == 0u,
        "JPA calc_c helper preserves one loop checkpoint per callback");
    passed &= expect(
        jpa_calc_probe.gpr3 == jpa_work &&
            jpa_calc_probe.gpr4 == jpa_child_particle &&
            jpa_calc_probe.gpr5 == jpa_calc_child_list &&
            jpa_calc_probe.gpr12 == jpa_calc_child_target0 &&
            jpa_calc_probe.ctr == jpa_calc_child_target0 &&
            jpa_calc_probe.lr == 0x80443958u,
        "JPA calc_c helper preserves the original bctrl argument setup");
    passed &= expect(
        jpa_calc_context.gpr[1] == jpa_stack &&
            jpa_calc_context.gpr[3] == 0xC0DEC0DEu &&
            jpa_calc_context.gpr[27] == 0x27272727u &&
            jpa_calc_context.gpr[28] == 0x28282828u &&
            jpa_calc_context.gpr[29] == 0x29292929u &&
            jpa_calc_context.gpr[30] == 0x30303030u &&
            jpa_calc_context.gpr[31] == 0x31313131u &&
            jpa_calc_context.gpr[11] == jpa_stack &&
            jpa_calc_context.lr == jpa_caller_lr &&
            jpa_calc_context.pc == (jpa_calc_child_target0 & 0xFFFFFFFCu) &&
            ((jpa_calc_context.cr >> 28) & 0xFu) == 0x8u,
        "JPA calc_c helper preserves callback return and epilogue side effects");

    RestartabilityProbe jpa_calc_restart_probe{};
    galaxy::NativeServicesV1 jpa_calc_restart_services{};
    jpa_calc_restart_services.user = &jpa_calc_restart_probe;
    jpa_calc_restart_services.call_guest_cached =
        &return_from_cached_call_for_restartability;
    jpa_calc_restart_services.branch_checkpoint =
        &throw_guest_transfer_on_checkpoint;
    jpa_calc_context = {};
    jpa_calc_context.gpr[1] = jpa_stack;
    jpa_calc_context.gpr[3] = jpa_resource;
    jpa_calc_context.gpr[4] = jpa_work;
    jpa_calc_context.gpr[5] = jpa_child_particle;
    jpa_calc_context.gpr[27] = 0x27272727u;
    jpa_calc_context.gpr[28] = 0x28282828u;
    jpa_calc_context.gpr[29] = 0x29292929u;
    jpa_calc_context.gpr[30] = 0x30303030u;
    jpa_calc_context.gpr[31] = 0x31313131u;
    jpa_calc_context.lr = jpa_caller_lr;
    bool jpa_calc_transfer_observed = false;
    try {
        galaxy::native_jpa_calc_child_func_list_80443904(
            &jpa_calc_context,
            &memory,
            &jpa_calc_restart_services,
            0x80443904u);
    } catch (const SimulatedGuestTransfer&) {
        jpa_calc_transfer_observed = true;
    }
    constexpr std::uint32_t jpa_calc_frame = jpa_stack - 0x20u;
    passed &= expect(
        jpa_calc_transfer_observed &&
            jpa_calc_restart_probe.checkpoints == 1u &&
            jpa_calc_restart_probe.checkpoint_pc == 0x80443964u &&
            jpa_calc_context.pc == 0x80443940u &&
            jpa_calc_context.gpr[1] == jpa_calc_frame &&
            jpa_calc_context.gpr[27] == jpa_resource &&
            jpa_calc_context.gpr[28] == jpa_work &&
            jpa_calc_context.gpr[29] == jpa_child_particle &&
            jpa_calc_context.gpr[30] == 1u &&
            jpa_calc_context.gpr[31] == 4u &&
            galaxy::guest_load_u32(
                &memory, jpa_calc_frame, nullptr, 0) == jpa_stack &&
            galaxy::guest_load_u32(
                &memory, jpa_calc_frame + 0x24u, nullptr, 0) == jpa_caller_lr,
        "JPA child-list helper publishes the taken target with an exact resumable frame");

    galaxy::guest_store_u32(&memory, jpa_calc_child_list, 0x804465A0u, nullptr, 0);
    galaxy::guest_store_u32(&memory, jpa_calc_child_list + 4u, 0x804465CCu, nullptr, 0);
    constexpr std::uint32_t jpa_calc_direct_sda2 = 0x80000100u;
    galaxy::guest_store_u32(
        &memory, jpa_calc_direct_sda2 + 0x1FD0u,
        std::bit_cast<std::uint32_t>(50.0F), nullptr, 0);
    galaxy::guest_store_u32(
        &memory, jpa_calc_direct_sda2 + 0x1FD4u,
        std::bit_cast<std::uint32_t>(5.0F), nullptr, 0);
    galaxy::guest_store_u32(
        &memory, jpa_child_particle + 0x68u,
        std::bit_cast<std::uint32_t>(2.0F), nullptr, 0);
    galaxy::guest_store_u32(
        &memory, jpa_child_particle + 0x6Cu,
        std::bit_cast<std::uint32_t>(-4.0F), nullptr, 0);
    galaxy::guest_store_u32(
        &memory, jpa_child_particle + 0x84u,
        std::bit_cast<std::uint32_t>(1.5F), nullptr, 0);
    galaxy::guest_store_u8(&memory, jpa_child_particle + 0x96u, 0u, nullptr, 0);
    jpa_calc_probe = {};
    jpa_calc_context = {};
    jpa_calc_context.hid2 = 0xA0000000u;
    jpa_calc_context.gqr[2] = 4u;
    jpa_calc_context.gpr[1] = jpa_stack;
    jpa_calc_context.gpr[2] = jpa_calc_direct_sda2;
    jpa_calc_context.gpr[3] = jpa_resource;
    jpa_calc_context.gpr[4] = jpa_work;
    jpa_calc_context.gpr[5] = jpa_child_particle;
    jpa_calc_context.gpr[27] = 0x27272727u;
    jpa_calc_context.gpr[28] = 0x28282828u;
    jpa_calc_context.gpr[29] = 0x29292929u;
    jpa_calc_context.gpr[30] = 0x30303030u;
    jpa_calc_context.gpr[31] = 0x31313131u;
    jpa_calc_context.lr = jpa_caller_lr;
    galaxy::native_jpa_calc_child_func_list_80443904(
        &jpa_calc_context, &memory, &jpa_calc_services, 0x80443904u);
    passed &= expect(
        jpa_calc_probe.calls == 0u &&
            jpa_calc_probe.branch_checkpoints == 2u &&
            jpa_calc_probe.branch_pc == 0x80443964u &&
            jpa_calc_probe.branch_resume_pc == 0x80443940u,
        "JPA calc_c helper directly dispatches known native child callbacks");
    const std::uint32_t direct_scale_x =
        galaxy::guest_load_u32(&memory, jpa_child_particle + 0x60u, nullptr, 0);
    const std::uint32_t direct_scale_y =
        galaxy::guest_load_u32(&memory, jpa_child_particle + 0x64u, nullptr, 0);
    const std::uint8_t direct_alpha =
        galaxy::guest_load_u8(&memory, jpa_child_particle + 0x96u, nullptr, 0);
    passed &= expect(
        direct_scale_x == std::bit_cast<std::uint32_t>(7.0F) &&
            direct_scale_y == std::bit_cast<std::uint32_t>(-14.0F) &&
            direct_alpha == 175u,
        "JPA calc_c direct callbacks preserve alpha and scale side effects");
    passed &= expect(
        jpa_calc_context.gpr[1] == jpa_stack &&
            jpa_calc_context.gpr[27] == 0x27272727u &&
            jpa_calc_context.gpr[28] == 0x28282828u &&
            jpa_calc_context.gpr[29] == 0x29292929u &&
            jpa_calc_context.gpr[30] == 0x30303030u &&
            jpa_calc_context.gpr[31] == 0x31313131u &&
            jpa_calc_context.lr == jpa_caller_lr,
        "JPA calc_c direct callback path preserves epilogue state");

    constexpr std::uint32_t actor_list_owner = 0x80000D00u;
    constexpr std::uint32_t actor_list_table = 0x80000E00u;
    constexpr std::uint32_t actor_list_node = 0x80000F00u;
    constexpr std::uint32_t actor_list_actor = 0x80001000u;
    constexpr std::uint32_t actor_list_stack = 0x80002400u;
    constexpr std::uint32_t actor_list_lr = 0x81828384u;
    galaxy::guest_store_u32(
        &memory, actor_list_owner, actor_list_table, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, actor_list_table, actor_list_node, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, actor_list_node, actor_list_actor, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, actor_list_node + 0x0Cu, 0u, nullptr, 0);
    RestartabilityProbe actor_list_restart_probe{};
    galaxy::NativeServicesV1 actor_list_restart_services{};
    actor_list_restart_services.user = &actor_list_restart_probe;
    actor_list_restart_services.call_guest_cached =
        &return_from_cached_call_for_restartability;
    actor_list_restart_services.branch_checkpoint =
        &throw_guest_transfer_on_checkpoint;
    galaxy::PpcContext actor_list_context{};
    actor_list_context.pc = 0x80448710u;
    actor_list_context.gpr[1] = actor_list_stack;
    actor_list_context.gpr[3] = actor_list_owner;
    actor_list_context.gpr[4] = 0u;
    actor_list_context.gpr[29] = 0x29292929u;
    actor_list_context.gpr[30] = 0x30303030u;
    actor_list_context.gpr[31] = 0x31313131u;
    actor_list_context.lr = actor_list_lr;
    bool actor_list_transfer_observed = false;
    try {
        galaxy::native_actor_list_apply_80448710(
            &actor_list_context,
            &memory,
            &actor_list_restart_services,
            0x80448710u);
    } catch (const SimulatedGuestTransfer&) {
        actor_list_transfer_observed = true;
    }
    constexpr std::uint32_t actor_list_frame = actor_list_stack - 0x20u;
    passed &= expect(
        actor_list_transfer_observed &&
            actor_list_restart_probe.checkpoints == 1u &&
            actor_list_restart_probe.checkpoint_pc == 0x80448774u &&
            actor_list_context.pc == 0x80448738u &&
            actor_list_context.gpr[1] == actor_list_frame &&
            actor_list_context.gpr[29] == actor_list_owner &&
            actor_list_context.gpr[30] == actor_list_node &&
            galaxy::guest_load_u32(
                &memory, actor_list_frame, nullptr, 0) == actor_list_stack &&
            galaxy::guest_load_u32(
                &memory, actor_list_frame + 0x24u, nullptr, 0) == actor_list_lr,
        "actor-list helper publishes the taken loop target with an exact resumable frame");

    constexpr std::uint32_t jpa_actor = 0x80000300u;
    constexpr std::uint32_t jpa_actor_resource = 0x80000500u;
    constexpr std::uint32_t jpa_actor_nested = 0x80000600u;
    constexpr std::uint32_t jpa_actor_params = 0x80000700u;
    galaxy::guest_store_u32(&memory, jpa_actor + 0xE8u, jpa_actor_resource, nullptr, 0);
    galaxy::guest_store_u32(&memory, jpa_actor_resource + 0x2Cu, jpa_actor_nested, nullptr, 0);
    galaxy::guest_store_u32(&memory, jpa_actor_nested, jpa_actor_params, nullptr, 0);
    galaxy::guest_store_u16(&memory, jpa_actor_params + 0x70u, 3u, nullptr, 0);
    galaxy::guest_store_u16(&memory, jpa_actor + 0x104u, 5u, nullptr, 0);
    galaxy::PpcContext jpa_actor_context{};
    jpa_actor_context.gpr[3] = jpa_actor;
    galaxy::native_jpa_actor_check_80448D54(
        &jpa_actor_context, &memory, nullptr, 0x80448D54u);
    passed &= expect(
        jpa_actor_context.gpr[3] == 1u &&
            galaxy::guest_load_u16(&memory, jpa_actor + 0x104u, nullptr, 0) == 5u,
        "JPA actor check 80448D54 returns busy without touching the counter");

    galaxy::guest_store_u16(&memory, jpa_actor + 0x104u, 1u, nullptr, 0);
    galaxy::guest_store_u32(&memory, jpa_actor + 0xF4u, 0u, nullptr, 0);
    jpa_actor_context = {};
    jpa_actor_context.gpr[3] = jpa_actor;
    galaxy::native_jpa_actor_check_80448D54(
        &jpa_actor_context, &memory, nullptr, 0x80448D54u);
    passed &= expect(
        jpa_actor_context.gpr[3] == 0u &&
            galaxy::guest_load_u16(&memory, jpa_actor + 0x104u, nullptr, 0) == 2u,
        "JPA actor check 80448D54 increments the counter only when flag bit 1 is clear");

    galaxy::guest_store_u16(&memory, jpa_actor + 0x104u, 1u, nullptr, 0);
    galaxy::guest_store_u32(&memory, jpa_actor + 0xF4u, 0x2u, nullptr, 0);
    jpa_actor_context = {};
    jpa_actor_context.gpr[3] = jpa_actor;
    galaxy::native_jpa_actor_check_80448D54(
        &jpa_actor_context, &memory, nullptr, 0x80448D54u);
    passed &= expect(
        jpa_actor_context.gpr[3] == 0u &&
            galaxy::guest_load_u16(&memory, jpa_actor + 0x104u, nullptr, 0) == 1u,
        "JPA actor check 80448D54 leaves the counter alone when flag bit 1 is set");

    galaxy::guest_store_u32(&memory, jpa_actor + 0xF4u, 0x100u, nullptr, 0);
    jpa_actor_context = {};
    jpa_actor_context.gpr[3] = jpa_actor;
    galaxy::native_jpa_actor_check_80448D94(
        &jpa_actor_context, &memory, nullptr, 0x80448D94u);
    passed &= expect(
        jpa_actor_context.gpr[3] == 1u,
        "JPA actor check 80448D94 returns busy when flag bit 8 is set");

    galaxy::guest_store_u32(&memory, jpa_actor + 0xF4u, 0u, nullptr, 0);
    galaxy::guest_store_u32(&memory, jpa_actor + 0x24u, 0u, nullptr, 0);
    jpa_actor_context = {};
    jpa_actor_context.gpr[3] = jpa_actor;
    galaxy::native_jpa_actor_check_80448D94(
        &jpa_actor_context, &memory, nullptr, 0x80448D94u);
    passed &= expect(
        jpa_actor_context.gpr[3] == 0u,
        "JPA actor check 80448D94 returns idle when the work count is zero");

    const auto expected_actor_distance = [](std::uint32_t value) {
        const auto leading = static_cast<std::uint32_t>(std::countl_zero(value));
        return std::rotl(leading, 27) & 0x07FFFFFFu;
    };
    galaxy::guest_store_u32(&memory, jpa_actor + 0xF4u, 0u, nullptr, 0);
    galaxy::guest_store_u32(&memory, jpa_actor + 0x24u, 0xFFFF'FFFFu, nullptr, 0);
    galaxy::guest_store_u32(&memory, jpa_actor + 0xD0u, 0x10u, nullptr, 0);
    galaxy::guest_store_u32(&memory, jpa_actor + 0xDCu, 0u, nullptr, 0);
    jpa_actor_context = {};
    jpa_actor_context.gpr[3] = jpa_actor;
    galaxy::native_jpa_actor_check_80448D94(
        &jpa_actor_context, &memory, nullptr, 0x80448D94u);
    passed &= expect(
        jpa_actor_context.gpr[3] == expected_actor_distance(0x10u) &&
            (galaxy::guest_load_u32(&memory, jpa_actor + 0xF4u, nullptr, 0) & 0x8u) != 0u,
        "JPA actor check 80448D94 handles the negative-count flag/store path");

    galaxy::guest_store_u32(&memory, jpa_actor + 0xF4u, 0u, nullptr, 0);
    galaxy::guest_store_u32(&memory, jpa_actor + 0x24u, 2u, nullptr, 0);
    galaxy::guest_store_u32(&memory, jpa_actor + 0x100u, 2u, nullptr, 0);
    galaxy::guest_store_u32(&memory, jpa_actor + 0xD0u, 0x20u, nullptr, 0);
    galaxy::guest_store_u32(&memory, jpa_actor + 0xDCu, 0u, nullptr, 0);
    jpa_actor_context = {};
    jpa_actor_context.gpr[3] = jpa_actor;
    galaxy::native_jpa_actor_check_80448D94(
        &jpa_actor_context, &memory, nullptr, 0x80448D94u);
    passed &= expect(
        jpa_actor_context.gpr[3] == expected_actor_distance(0x20u) &&
            (galaxy::guest_load_u32(&memory, jpa_actor + 0xF4u, nullptr, 0) & 0x8u) != 0u,
        "JPA actor check 80448D94 handles the positive-count completion path");

    constexpr std::uint32_t jpa_draw_resource = 0x80000A00u;
    constexpr std::uint32_t jpa_draw_work = 0x80001200u;
    constexpr std::uint32_t jpa_draw_stack = 0x80002000u;
    constexpr std::uint32_t jpa_draw_table = 0x805C7220u;
    CachedCallProbe jpa_draw_checkpoint_probe{};
    std::atomic<std::uint32_t> jpa_draw_pending_event_mask{0u};
    galaxy::NativeServicesV1 jpa_particle_draw_services{};
    jpa_particle_draw_services.user = &jpa_draw_checkpoint_probe;
    jpa_particle_draw_services.branch_checkpoint = &capture_branch_checkpoint;
    jpa_particle_draw_services.pending_event_mask =
        &jpa_draw_pending_event_mask;
    const auto store_jpa_draw_f32 = [&](std::uint32_t address, float value) {
        galaxy::guest_store_u32(
            &memory, address, std::bit_cast<std::uint32_t>(value), nullptr, 0);
    };
    const auto load_jpa_draw_u32 = [&](std::uint32_t address) {
        return galaxy::guest_load_u32(&memory, address, nullptr, 0);
    };
    const auto load_jpa_draw_f32 = [&](std::uint32_t address) {
        return std::bit_cast<float>(load_jpa_draw_u32(address));
    };

    galaxy::PpcContext jpa_draw_particle_context{};
    jpa_draw_particle_context.gpr[1] = jpa_draw_stack;
    jpa_draw_particle_context.gpr[2] = 0x80000000u;
    jpa_draw_particle_context.gpr[3] = jpa_draw_resource;
    jpa_draw_particle_context.gpr[4] = jpa_draw_work;
    jpa_draw_particle_context.gpr[29] = 0x29292929u;
    jpa_draw_particle_context.gpr[30] = 0x30303030u;
    jpa_draw_particle_context.gpr[31] = 0x31313131u;
    jpa_draw_particle_context.lr = jpa_caller_lr;
    galaxy::guest_store_u32(&memory, jpa_draw_work + 0x7Cu, 0x08u, nullptr, 0);
    galaxy::native_jpa_draw_particle_803A3B6C(
        &jpa_draw_particle_context, &memory, nullptr, 0x803A3B6Cu);
    passed &= expect(
        jpa_draw_particle_context.gpr[1] == jpa_draw_stack &&
            jpa_draw_particle_context.gpr[29] == 0x29292929u &&
            jpa_draw_particle_context.gpr[30] == 0x30303030u &&
            jpa_draw_particle_context.gpr[31] == 0x31313131u &&
            jpa_draw_particle_context.gpr[11] == jpa_draw_stack &&
            jpa_draw_particle_context.lr == jpa_caller_lr,
        "JPA draw helper preserves skipped-path epilogue and nonvolatile registers");

    galaxy::guest_store_u32(&memory, jpa_draw_table + 0x40u, 0x98u, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, jpa_draw_table + 0x48u, 0x803A33ACu, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, jpa_draw_table + 0x54u, 0x803A35DCu, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, jpa_draw_table + 0x7Cu, 0x803A387Cu, nullptr, 0);
    galaxy::guest_store_u32(&memory, jpa_draw_resource + 0x200u, 0u, nullptr, 0);
    galaxy::guest_store_u32(&memory, jpa_draw_resource + 0x208u, 0u, nullptr, 0);
    galaxy::guest_store_u32(&memory, jpa_draw_resource + 0x20Cu, 0u, nullptr, 0);
    galaxy::guest_store_u32(&memory, jpa_draw_resource + 0x210u, 0u, nullptr, 0);
    store_jpa_draw_f32(0x80001830u, 0.0001F);
    store_jpa_draw_f32(jpa_draw_work + 0x24u, 1.0F);
    store_jpa_draw_f32(jpa_draw_work + 0x28u, 2.0F);
    store_jpa_draw_f32(jpa_draw_work + 0x2Cu, 3.0F);
    store_jpa_draw_f32(jpa_draw_work + 0x00u, 2.0F);
    store_jpa_draw_f32(jpa_draw_work + 0x04u, 3.0F);
    store_jpa_draw_f32(jpa_draw_work + 0x08u, 4.0F);
    store_jpa_draw_f32(jpa_draw_work + 0x54u, 4.0F);
    store_jpa_draw_f32(jpa_draw_work + 0x58u, 5.0F);
    store_jpa_draw_f32(jpa_draw_work + 0x5Cu, 7.0F);
    store_jpa_draw_f32(jpa_draw_work + 0x60u, 1.0F);
    store_jpa_draw_f32(jpa_draw_work + 0x64u, 1.0F);
    store_jpa_draw_f32(jpa_draw_resource + 0x144u, 1.0F);
    store_jpa_draw_f32(jpa_draw_resource + 0x148u, 1.0F);
    for (std::uint32_t i = 0; i < 12u; ++i) {
        store_jpa_draw_f32(
            jpa_draw_resource + 0x184u + i * 4u,
            (i % 5u) == 0u ? 1.0F : 0.0F);
    }
    galaxy::guest_store_u32(
        &memory, gx_global_pointer_slot, gx_begin_global_address, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, gx_begin_global_address + 0x00u, 1u, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, gx_begin_global_address + 0x5FCu, 0u, nullptr, 0);

    FifoWriteProbe jpa_draw_fifo_probe{};
    memory.user = &jpa_draw_fifo_probe;
    memory.read_device = nullptr;
    memory.write_device = &capture_fifo_write;
    jpa_draw_particle_context = {};
    jpa_draw_particle_context.hid2 = 0xA0000000u;
    jpa_draw_particle_context.gpr[1] = jpa_draw_stack;
    jpa_draw_particle_context.gpr[2] = 0x80000000u;
    jpa_draw_particle_context.gpr[3] = jpa_draw_resource;
    jpa_draw_particle_context.gpr[4] = jpa_draw_work;
    jpa_draw_particle_context.gpr[29] = 0x29292929u;
    jpa_draw_particle_context.gpr[30] = 0x30303030u;
    jpa_draw_particle_context.gpr[31] = 0x31313131u;
    jpa_draw_particle_context.lr = jpa_caller_lr;
    galaxy::guest_store_u32(&memory, jpa_draw_work + 0x7Cu, 0u, nullptr, 0);
    galaxy::native_jpa_draw_particle_803A3B6C(
        &jpa_draw_particle_context,
        &memory,
        &jpa_particle_draw_services,
        0x803A3B6Cu);
    memory.user = &device_probe;
    memory.read_device = &probe_read;
    memory.write_device = &probe_write;
    if (!(jpa_draw_fifo_probe.writes == 4u &&
          jpa_draw_fifo_probe.address == 0xCC008000u &&
          jpa_draw_fifo_probe.size >= 62u)) {
        const std::uint32_t jpa_draw_frame = jpa_draw_stack - 0x60u;
        std::cerr << "JPA draw FIFO writes=" << jpa_draw_fifo_probe.writes
                  << " address=0x" << std::hex << jpa_draw_fifo_probe.address
                  << std::dec << " size=" << jpa_draw_fifo_probe.size
                  << " dir=(" << load_jpa_draw_f32(jpa_draw_frame + 0x14u)
                  << ',' << load_jpa_draw_f32(jpa_draw_frame + 0x18u)
                  << ',' << load_jpa_draw_f32(jpa_draw_frame + 0x1Cu) << ')'
                  << " cross=(" << load_jpa_draw_f32(jpa_draw_frame + 0x08u)
                  << ',' << load_jpa_draw_f32(jpa_draw_frame + 0x0Cu)
                  << ',' << load_jpa_draw_f32(jpa_draw_frame + 0x10u) << ')'
                  << " matrix0=0x" << std::hex
                  << load_jpa_draw_u32(jpa_draw_frame + 0x20u)
                  << " lr=0x" << jpa_draw_particle_context.lr << std::dec
                  << '\n';
    }
    passed &= expect(
        jpa_draw_fifo_probe.writes == 4u &&
            jpa_draw_fifo_probe.address == 0xCC008000u &&
            jpa_draw_fifo_probe.size >= 62u,
        "JPA draw helper batches matrix FIFO traffic and emits the primitive stream");
    passed &= expect(
        jpa_draw_particle_context.gpr[1] == jpa_draw_stack &&
            jpa_draw_particle_context.gpr[29] == 0x29292929u &&
            jpa_draw_particle_context.gpr[30] == 0x30303030u &&
            jpa_draw_particle_context.gpr[31] == 0x31313131u &&
            jpa_draw_particle_context.lr == jpa_caller_lr,
        "JPA draw helper active path restores translated nonvolatile state");

    // The final GXBegin is a translated call boundary. Force a pending event
    // there and prove that the outer JPA frame and exact 0x803A3D00
    // continuation survive the native helper unwind.
    RestartabilityProbe jpa_draw_restart_probe{};
    std::atomic<std::uint32_t> jpa_draw_restart_pending{1u};
    galaxy::NativeServicesV1 jpa_draw_restart_services{};
    jpa_draw_restart_services.user = &jpa_draw_restart_probe;
    jpa_draw_restart_services.branch_checkpoint =
        &throw_guest_transfer_on_checkpoint;
    jpa_draw_restart_services.pending_event_mask =
        &jpa_draw_restart_pending;
    jpa_draw_fifo_probe = {};
    memory.user = &jpa_draw_fifo_probe;
    memory.read_device = nullptr;
    memory.write_device = &capture_fifo_write;
    jpa_draw_particle_context = {};
    jpa_draw_particle_context.hid2 = 0xA0000000u;
    jpa_draw_particle_context.gpr[1] = jpa_draw_stack;
    jpa_draw_particle_context.gpr[2] = 0x80000000u;
    jpa_draw_particle_context.gpr[3] = jpa_draw_resource;
    jpa_draw_particle_context.gpr[4] = jpa_draw_work;
    jpa_draw_particle_context.gpr[29] = 0x29292929u;
    jpa_draw_particle_context.gpr[30] = 0x30303030u;
    jpa_draw_particle_context.gpr[31] = 0x31313131u;
    jpa_draw_particle_context.lr = jpa_caller_lr;
    bool jpa_draw_transfer_observed = false;
    try {
        galaxy::native_jpa_draw_particle_803A3B6C(
            &jpa_draw_particle_context,
            &memory,
            &jpa_draw_restart_services,
            0x803A3B6Cu);
    } catch (const SimulatedGuestTransfer&) {
        jpa_draw_transfer_observed = true;
    }
    memory.user = &device_probe;
    memory.read_device = &probe_read;
    memory.write_device = &probe_write;
    constexpr std::uint32_t jpa_draw_frame = jpa_draw_stack - 0x60u;
    passed &= expect(
        jpa_draw_transfer_observed &&
            jpa_draw_restart_probe.checkpoints == 1u &&
            jpa_draw_restart_probe.checkpoint_pc == 0x803A3CFCu &&
            jpa_draw_particle_context.pc == 0x803A3D00u &&
            jpa_draw_particle_context.lr == 0x803A3D00u &&
            jpa_draw_particle_context.gpr[1] == jpa_draw_frame &&
            jpa_draw_particle_context.gpr[29] == jpa_draw_resource &&
            jpa_draw_particle_context.gpr[30] == jpa_draw_work &&
            jpa_draw_particle_context.gpr[31] == jpa_draw_table &&
            galaxy::guest_load_u32(
                &memory, jpa_draw_frame, nullptr, 0) == jpa_draw_stack &&
            galaxy::guest_load_u32(
                &memory, jpa_draw_frame + 0x64u, nullptr, 0) == jpa_caller_lr,
        "JPA draw helper leaves its exact outer frame and continuation on final GXBegin transfer");

    std::uint32_t jpa_draw_cached_target = 0u;
    galaxy::NativeGameFunction jpa_draw_cached_function = nullptr;
    jpa_draw_particle_context = {};
    jpa_draw_particle_context.gpr[1] = jpa_draw_stack;
    jpa_draw_particle_context.gpr[3] = jpa_draw_resource;
    jpa_draw_particle_context.gpr[4] = jpa_draw_work;
    jpa_draw_particle_context.gpr[29] = 0x29292929u;
    jpa_draw_particle_context.gpr[30] = 0x30303030u;
    jpa_draw_particle_context.gpr[31] = 0x31313131u;
    jpa_draw_particle_context.lr = 0x80443424u;
    galaxy::guest_store_u32(&memory, jpa_draw_work + 0x7Cu, 0x08u, nullptr, 0);
    galaxy::native_jpa_draw_particle_list_callback_80443420(
        nullptr,
        0x803A3B6Cu,
        &jpa_draw_cached_target,
        &jpa_draw_cached_function,
        &jpa_draw_particle_context,
        &memory);
    passed &= expect(
        jpa_draw_list_cached_target == 0u &&
            jpa_draw_list_cached_function == nullptr &&
            jpa_draw_particle_context.gpr[1] == jpa_draw_stack &&
            jpa_draw_particle_context.gpr[29] == 0x29292929u &&
            jpa_draw_particle_context.gpr[30] == 0x30303030u &&
            jpa_draw_particle_context.gpr[31] == 0x31313131u &&
            jpa_draw_particle_context.lr == 0x80443424u,
        "JPA draw list callback directly dispatches the audited native particle draw target");

    constexpr std::uint32_t jpa_draw_unknown_target = 0x80401235u;
    CachedCallProbe jpa_draw_list_probe{};
    galaxy::NativeServicesV1 jpa_draw_list_services{};
    jpa_draw_list_services.user = &jpa_draw_list_probe;
    jpa_draw_list_services.call_guest_cached = &capture_cached_call;
    jpa_draw_particle_context = {};
    jpa_draw_particle_context.gpr[3] = jpa_draw_resource;
    jpa_draw_particle_context.gpr[4] = jpa_draw_work;
    jpa_draw_particle_context.gpr[12] = jpa_draw_unknown_target;
    jpa_draw_particle_context.ctr = jpa_draw_unknown_target;
    jpa_draw_particle_context.lr = 0x80443424u;
    galaxy::native_jpa_draw_particle_list_callback_80443420(
        &jpa_draw_list_services,
        jpa_draw_unknown_target,
        &jpa_draw_list_cached_target,
        &jpa_draw_list_cached_function,
        &jpa_draw_particle_context,
        &memory);
    passed &= expect(
        jpa_draw_list_probe.calls == 1u &&
            jpa_draw_list_probe.guest_address == (jpa_draw_unknown_target & 0xFFFFFFFCu) &&
            jpa_draw_list_probe.cache_slots_present &&
            jpa_draw_list_probe.gpr3 == jpa_draw_resource &&
            jpa_draw_list_probe.gpr4 == jpa_draw_work &&
            jpa_draw_list_probe.gpr12 == jpa_draw_unknown_target &&
            jpa_draw_list_probe.ctr == jpa_draw_unknown_target &&
            jpa_draw_list_probe.lr == 0x80443424u,
        "JPA draw list callback preserves cached dispatch for unknown particle draw targets");

    constexpr std::uint32_t ptmf_slot = 0x80001600u;
    constexpr std::uint32_t ptmf_object = 0x80001700u;
    constexpr std::uint32_t ptmf_vtable = 0x80001800u;
    constexpr std::uint32_t ptmf_direct_target = 0x80411119u;
    constexpr std::uint32_t ptmf_virtual_target = 0x8042222Du;
    CachedCallProbe ptmf_probe{};
    galaxy::NativeServicesV1 ptmf_services{};
    ptmf_services.user = &ptmf_probe;
    ptmf_services.call_guest_cached = &capture_cached_call;
    galaxy::guest_store_u32(&memory, ptmf_slot + 0u, 0x20u, nullptr, 0);
    galaxy::guest_store_u32(&memory, ptmf_slot + 4u, 0xFFFFFFFFu, nullptr, 0);
    galaxy::guest_store_u32(&memory, ptmf_slot + 8u, ptmf_direct_target, nullptr, 0);
    galaxy::PpcContext ptmf_context{};
    ptmf_context.gpr[3] = ptmf_object;
    ptmf_context.gpr[12] = ptmf_slot;
    galaxy::native_ptmf_scall_805173E0(
        &ptmf_context, &memory, &ptmf_services, 0x805173E0u);
    passed &= expect(
        ptmf_probe.calls == 1u &&
            ptmf_probe.guest_address == (ptmf_direct_target & 0xFFFFFFFCu) &&
            ptmf_probe.gpr3 == ptmf_object + 0x20u &&
            ptmf_probe.gpr11 == 0xFFFFFFFFu &&
            ptmf_probe.gpr12 == ptmf_direct_target &&
            ptmf_probe.ctr == ptmf_direct_target &&
            ptmf_probe.cache_slots_present &&
            ((ptmf_context.cr >> 28) & 0xFu) == 0x8u,
        "native __ptmf_scall dispatches direct pointer-to-member targets");

    ptmf_probe = {};
    ptmf_context = {};
    ptmf_context.gpr[3] = ptmf_object;
    ptmf_context.gpr[12] = ptmf_slot;
    galaxy::guest_store_u32(&memory, ptmf_slot + 0u, 4u, nullptr, 0);
    galaxy::guest_store_u32(&memory, ptmf_slot + 4u, 0x0Cu, nullptr, 0);
    galaxy::guest_store_u32(&memory, ptmf_slot + 8u, 0x30u, nullptr, 0);
    galaxy::guest_store_u32(&memory, ptmf_object + 4u + 0x30u, ptmf_vtable, nullptr, 0);
    galaxy::guest_store_u32(&memory, ptmf_vtable + 0x0Cu, ptmf_virtual_target, nullptr, 0);
    galaxy::native_ptmf_scall_805173E0(
        &ptmf_context, &memory, &ptmf_services, 0x805173E0u);
    passed &= expect(
        ptmf_probe.calls == 1u &&
            ptmf_probe.guest_address == (ptmf_virtual_target & 0xFFFFFFFCu) &&
            ptmf_probe.gpr3 == ptmf_object + 4u &&
            ptmf_probe.gpr11 == 0x0Cu &&
            ptmf_probe.gpr12 == ptmf_virtual_target &&
            ptmf_probe.ctr == ptmf_virtual_target &&
            ptmf_probe.cache_slots_present &&
            ((ptmf_context.cr >> 28) & 0xFu) == 0x4u,
        "native __ptmf_scall resolves virtual pointer-to-member targets");

    constexpr std::uint32_t ptmf_cache_slot0 = 0x80001840u;
    constexpr std::uint32_t ptmf_cache_slot1 = 0x80001880u;
    constexpr std::uint32_t ptmf_cache_target0 = 0x80411119u;
    constexpr std::uint32_t ptmf_cache_target1 = 0x80411159u;
    CachedCallProbe ptmf_cache_probe{};
    galaxy::NativeServicesV1 ptmf_cache_services{};
    ptmf_cache_services.user = &ptmf_cache_probe;
    ptmf_cache_services.call_guest_cached = &capture_cached_call;
    galaxy::guest_store_u32(&memory, ptmf_cache_slot0 + 0u, 0u, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, ptmf_cache_slot0 + 4u, 0xFFFFFFFFu, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, ptmf_cache_slot0 + 8u, ptmf_cache_target0, nullptr, 0);
    galaxy::guest_store_u32(&memory, ptmf_cache_slot1 + 0u, 0u, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, ptmf_cache_slot1 + 4u, 0xFFFFFFFFu, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, ptmf_cache_slot1 + 8u, ptmf_cache_target1, nullptr, 0);
    galaxy::PpcContext ptmf_cache_context{};
    ptmf_cache_context.gpr[3] = ptmf_object;
    ptmf_cache_context.gpr[12] = ptmf_cache_slot0;
    galaxy::native_ptmf_scall_805173E0(
        &ptmf_cache_context, &memory, &ptmf_cache_services, 0x805173E0u);
    ptmf_cache_context = {};
    ptmf_cache_context.gpr[3] = ptmf_object;
    ptmf_cache_context.gpr[12] = ptmf_cache_slot1;
    galaxy::native_ptmf_scall_805173E0(
        &ptmf_cache_context, &memory, &ptmf_cache_services, 0x805173E0u);
    passed &= expect(
        ptmf_cache_probe.calls == 2u &&
            ptmf_cache_probe.guest_addresses[0] ==
                (ptmf_cache_target0 & 0xFFFFFFFCu) &&
            ptmf_cache_probe.guest_addresses[1] ==
                (ptmf_cache_target1 & 0xFFFFFFFCu) &&
            ptmf_cache_probe.cache_address_slots[0] != 0u &&
            ptmf_cache_probe.cache_function_slots[0] != 0u &&
            ptmf_cache_probe.cache_address_slots[0] !=
                ptmf_cache_probe.cache_address_slots[1] &&
            ptmf_cache_probe.cache_function_slots[0] !=
                ptmf_cache_probe.cache_function_slots[1],
        "native __ptmf_scall uses separate cache cells for alternating targets");

    constexpr std::uint32_t movement_holder = 0x80001900u;
    constexpr std::uint32_t movement_group = 0x80001980u;
    constexpr std::uint32_t movement_vtable = 0x800019C0u;
    constexpr std::uint32_t movement_target_raw = 0x80434567u;
    galaxy::guest_store_u32(&memory, movement_holder + 0x10u, 0u, nullptr, 0);
    galaxy::PpcContext movement_context{};
    movement_context.gpr[3] = movement_holder;
    galaxy::native_movement_group_dispatch_80261454(
        &movement_context, &memory, nullptr, 0x80261454u);
    passed &= expect(
        movement_context.gpr[3] == 0u &&
            ((movement_context.cr >> 28) & 0xFu) == 0x2u,
        "movement-group dispatch helper preserves null-group early return");

    galaxy::guest_store_u32(&memory, movement_holder + 0x10u, movement_group, nullptr, 0);
    galaxy::guest_store_u32(&memory, movement_holder + 0x18u, movement_group, nullptr, 0);
    galaxy::guest_store_u32(&memory, movement_group, movement_vtable, nullptr, 0);
    galaxy::guest_store_u32(&memory, movement_vtable + 0x08u, movement_target_raw, nullptr, 0);
    CachedCallProbe movement_probe{};
    galaxy::NativeServicesV1 movement_services{};
    movement_services.user = &movement_probe;
    movement_services.call_guest_cached = &capture_cached_call;
    movement_context = {};
    movement_context.gpr[3] = movement_holder;
    galaxy::native_movement_group_dispatch_80261454(
        &movement_context, &memory, &movement_services, 0x80261454u);
    passed &= expect(
        movement_probe.calls == 1u &&
            movement_probe.guest_address == (movement_target_raw & 0xFFFFFFFCu) &&
            movement_probe.gpr3 == movement_group &&
            movement_probe.gpr12 == movement_target_raw &&
            movement_probe.ctr == movement_target_raw &&
            movement_probe.cache_slots_present,
        "movement-group 0x80261454 helper dispatches vtable slot 0x08");

    movement_probe = {};
    movement_context = {};
    movement_context.gpr[3] = movement_holder;
    galaxy::native_movement_group_dispatch_80261494(
        &movement_context, &memory, &movement_services, 0x80261494u);
    passed &= expect(
        movement_probe.calls == 1u &&
            movement_probe.guest_address == (movement_target_raw & 0xFFFFFFFCu) &&
            movement_probe.gpr3 == movement_group &&
            movement_probe.gpr12 == movement_target_raw &&
            movement_probe.ctr == movement_target_raw &&
            movement_probe.cache_slots_present,
        "movement-group 0x80261494 helper dispatches vtable slot 0x08");

    constexpr std::uint32_t name_obj_holder = 0x80001A00u;
    constexpr std::uint32_t name_obj_base = 0x80001A80u;
    constexpr std::uint32_t name_obj_records = 0x80001B00u;
    constexpr std::uint32_t name_obj_list = 0x80001B80u;
    constexpr std::uint32_t name_obj_stack = 0x80002600u;
    constexpr std::uint32_t name_obj_first = 0x80001C00u;
    constexpr std::uint32_t name_obj_shared = 0x80001C80u;
    constexpr std::uint32_t name_obj_vtable0 = 0x80001D00u;
    constexpr std::uint32_t name_obj_vtable1 = 0x80001D80u;
    constexpr std::uint32_t name_obj_target0 = 0x80411113u;
    constexpr std::uint32_t name_obj_target1 = 0x80422225u;
    galaxy::guest_store_u32(
        &memory, name_obj_holder + 0x10u, name_obj_base, nullptr, 0);
    galaxy::guest_store_u32(&memory, name_obj_base, name_obj_records, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, name_obj_base + 0x08u, name_obj_shared, nullptr, 0);
    galaxy::guest_store_u32(&memory, name_obj_records, name_obj_list, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, name_obj_records + 0x08u, 2u, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, name_obj_records + 0x0Cu, name_obj_first, nullptr, 0);
    galaxy::guest_store_u32(&memory, name_obj_list, 0xCAFE0001u, nullptr, 0);
    galaxy::guest_store_u32(&memory, name_obj_list + 4u, 0xCAFE0002u, nullptr, 0);
    galaxy::guest_store_u32(&memory, name_obj_first, name_obj_vtable0, nullptr, 0);
    galaxy::guest_store_u32(&memory, name_obj_shared, name_obj_vtable1, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, name_obj_vtable0 + 0x08u, name_obj_target0, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, name_obj_vtable1 + 0x08u, name_obj_target1, nullptr, 0);
    CachedCallProbe name_obj_probe{};
    galaxy::NativeServicesV1 name_obj_services{};
    name_obj_services.user = &name_obj_probe;
    name_obj_services.call_guest_cached = &capture_cached_call;
    name_obj_services.branch_checkpoint = &capture_branch_checkpoint;
    galaxy::PpcContext name_obj_context{};
    name_obj_context.gpr[1] = name_obj_stack;
    name_obj_context.gpr[3] = name_obj_holder;
    name_obj_context.gpr[4] = 0u;
    name_obj_context.gpr[29] = 0xD000001Du;
    name_obj_context.gpr[30] = 0xD000001Eu;
    name_obj_context.gpr[31] = 0xD000001Fu;
    name_obj_context.lr = 0x88776655u;
    galaxy::native_name_obj_dispatch_8026C0E4(
        &name_obj_context, &memory, &name_obj_services, 0x8026C0E4u);
    passed &= expect(
        name_obj_probe.calls == 3u &&
            name_obj_probe.guest_addresses[0] == (name_obj_target0 & 0xFFFFFFFCu) &&
            name_obj_probe.guest_addresses[1] == (name_obj_target1 & 0xFFFFFFFCu) &&
            name_obj_probe.guest_addresses[2] == (name_obj_target1 & 0xFFFFFFFCu) &&
            name_obj_probe.branch_checkpoints == 2u &&
            name_obj_probe.branch_pc == 0x80261838u &&
            name_obj_probe.branch_resume_pc == 0x80261808u &&
            name_obj_probe.branch_gpr30 == name_obj_list + 4u &&
            name_obj_probe.branch_gpr31 == name_obj_records &&
            name_obj_context.gpr[1] == name_obj_stack &&
            name_obj_context.lr == 0x88776655u &&
            name_obj_context.gpr[29] == 0xD000001Du &&
            name_obj_context.gpr[30] == 0xD000001Eu &&
            name_obj_context.gpr[31] == 0xD000001Fu,
        "name-object dispatch helper preserves ABI frame and dispatches entries");

    RestartabilityProbe name_obj_restart_probe{};
    galaxy::NativeServicesV1 name_obj_restart_services{};
    name_obj_restart_services.user = &name_obj_restart_probe;
    name_obj_restart_services.call_guest_cached =
        &return_from_cached_call_for_restartability;
    name_obj_restart_services.branch_checkpoint =
        &throw_guest_transfer_on_checkpoint;
    name_obj_context = {};
    name_obj_context.gpr[1] = name_obj_stack;
    name_obj_context.gpr[3] = name_obj_holder;
    name_obj_context.gpr[4] = 0u;
    name_obj_context.gpr[29] = 0xD000001Du;
    name_obj_context.gpr[30] = 0xD000001Eu;
    name_obj_context.gpr[31] = 0xD000001Fu;
    name_obj_context.lr = 0x88776655u;
    bool name_obj_transfer_observed = false;
    try {
        galaxy::native_name_obj_dispatch_8026C0E4(
            &name_obj_context,
            &memory,
            &name_obj_restart_services,
            0x8026C0E4u);
    } catch (const SimulatedGuestTransfer&) {
        name_obj_transfer_observed = true;
    }
    constexpr std::uint32_t name_obj_frame = name_obj_stack - 0x20u;
    passed &= expect(
        name_obj_transfer_observed &&
            name_obj_restart_probe.guest_calls == 1u &&
            name_obj_restart_probe.guest_address ==
                (name_obj_target0 & 0xFFFFFFFCu) &&
            name_obj_restart_probe.checkpoints == 1u &&
            name_obj_restart_probe.checkpoint_pc == 0x80261838u &&
            name_obj_context.pc == 0x80261808u &&
            name_obj_context.gpr[1] == name_obj_frame &&
            name_obj_context.gpr[29] == name_obj_base &&
            name_obj_context.gpr[30] == name_obj_list &&
            name_obj_context.gpr[31] == name_obj_records &&
            galaxy::guest_load_u32(
                &memory, name_obj_frame, nullptr, 0) == name_obj_stack &&
            galaxy::guest_load_u32(
                &memory, name_obj_frame + 0x24u, nullptr, 0) == 0x88776655u,
        "name-object helper publishes the taken target with an exact resumable frame");

    constexpr std::uint32_t ptmf_wrapper_owner = 0x80001A00u;
    constexpr std::uint32_t ptmf_wrapper_this = 0x80001B00u;
    constexpr std::uint32_t ptmf_wrapper_stack = 0x80002600u;
    constexpr std::uint32_t ptmf_wrapper_lr = 0x88776655u;
    constexpr std::uint32_t ptmf_wrapper_target0 = 0x80445679u;
    constexpr std::uint32_t ptmf_wrapper_target1 = 0x80456789u;
    galaxy::guest_store_u32(&memory, ptmf_wrapper_owner + 0x04u, 0x10u, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, ptmf_wrapper_owner + 0x08u, 0xFFFFFFFFu, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, ptmf_wrapper_owner + 0x0Cu, ptmf_wrapper_target0, nullptr, 0);
    CachedCallProbe wrapper_probe{};
    galaxy::NativeServicesV1 wrapper_services{};
    wrapper_services.user = &wrapper_probe;
    wrapper_services.call_guest_cached = &capture_cached_call;
    galaxy::PpcContext wrapper_context{};
    wrapper_context.gpr[1] = ptmf_wrapper_stack;
    wrapper_context.gpr[3] = ptmf_wrapper_owner;
    wrapper_context.gpr[4] = ptmf_wrapper_this;
    wrapper_context.lr = ptmf_wrapper_lr;
    galaxy::native_ptmf_wrapper_80261B40(
        &wrapper_context, &memory, &wrapper_services, 0x80261B40u);
    passed &= expect(
        wrapper_probe.calls == 1u &&
            wrapper_probe.guest_address == (ptmf_wrapper_target0 & 0xFFFFFFFCu) &&
            wrapper_probe.gpr3 == ptmf_wrapper_this + 0x10u &&
            wrapper_probe.gpr5 == ptmf_wrapper_owner &&
            wrapper_probe.gpr11 == 0xFFFFFFFFu &&
            wrapper_probe.gpr12 == ptmf_wrapper_target0 &&
            wrapper_probe.ctr == ptmf_wrapper_target0 &&
            wrapper_probe.lr == 0x80261B5Cu &&
            wrapper_context.gpr[1] == ptmf_wrapper_stack &&
            wrapper_context.gpr[0] == ptmf_wrapper_lr &&
            wrapper_context.lr == ptmf_wrapper_lr,
        "0x80261B40 PTMF wrapper preserves stack, LR, and object adjustment");

    galaxy::guest_store_u32(&memory, ptmf_wrapper_owner + 0x04u, 0x04u, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, ptmf_wrapper_owner + 0x0Cu, ptmf_wrapper_target1, nullptr, 0);
    wrapper_probe = {};
    wrapper_context = {};
    wrapper_context.gpr[1] = ptmf_wrapper_stack;
    wrapper_context.gpr[3] = ptmf_wrapper_owner;
    wrapper_context.gpr[4] = ptmf_wrapper_this;
    wrapper_context.lr = ptmf_wrapper_lr;
    galaxy::native_ptmf_wrapper_80261B70(
        &wrapper_context, &memory, &wrapper_services, 0x80261B70u);
    passed &= expect(
        wrapper_probe.calls == 1u &&
            wrapper_probe.guest_address == (ptmf_wrapper_target1 & 0xFFFFFFFCu) &&
            wrapper_probe.gpr3 == ptmf_wrapper_this + 0x04u &&
            wrapper_probe.gpr5 == ptmf_wrapper_owner &&
            wrapper_probe.lr == 0x80261B8Cu &&
            wrapper_context.gpr[1] == ptmf_wrapper_stack &&
            wrapper_context.gpr[0] == ptmf_wrapper_lr &&
            wrapper_context.lr == ptmf_wrapper_lr,
        "0x80261B70 PTMF wrapper preserves stack, LR, and object adjustment");

    constexpr std::uint32_t ptmf_ctor_owner = 0x80001C00u;
    constexpr std::uint32_t ptmf_ctor_this = 0x80001D00u;
    constexpr std::uint32_t ptmf_ctor_target = 0x80467891u;
    galaxy::guest_store_u32(&memory, ptmf_ctor_owner + 0x04u, ptmf_ctor_this, nullptr, 0);
    galaxy::guest_store_u32(&memory, ptmf_ctor_owner + 0x08u, 0x20u, nullptr, 0);
    galaxy::guest_store_u32(&memory, ptmf_ctor_owner + 0x0Cu, 0xFFFFFFFFu, nullptr, 0);
    galaxy::guest_store_u32(&memory, ptmf_ctor_owner + 0x10u, ptmf_ctor_target, nullptr, 0);
    wrapper_probe = {};
    wrapper_context = {};
    wrapper_context.gpr[1] = ptmf_wrapper_stack;
    wrapper_context.gpr[3] = ptmf_ctor_owner;
    wrapper_context.lr = ptmf_wrapper_lr;
    galaxy::native_ptmf_wrapper_800C9078(
        &wrapper_context, &memory, &wrapper_services, 0x800C9078u);
    passed &= expect(
        wrapper_probe.calls == 1u &&
            wrapper_probe.guest_address == (ptmf_ctor_target & 0xFFFFFFFCu) &&
            wrapper_probe.gpr3 == ptmf_ctor_this + 0x20u &&
            wrapper_probe.gpr4 == ptmf_ctor_owner &&
            wrapper_probe.lr == 0x800C9094u &&
            wrapper_context.gpr[1] == ptmf_wrapper_stack &&
            wrapper_context.gpr[0] == ptmf_wrapper_lr &&
            wrapper_context.lr == ptmf_wrapper_lr,
        "0x800C9078 PTMF wrapper preserves constructor dispatch side effects");

    constexpr std::uint32_t ptmf_second_ctor_owner = 0x80001E00u;
    constexpr std::uint32_t ptmf_second_ctor_this = 0x80001F00u;
    constexpr std::uint32_t ptmf_second_ctor_target = 0x80467905u;
    galaxy::guest_store_u32(
        &memory, ptmf_second_ctor_owner + 0x04u, ptmf_second_ctor_this, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, ptmf_second_ctor_owner + 0x08u, 0x24u, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, ptmf_second_ctor_owner + 0x0Cu, 0xFFFFFFFFu, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, ptmf_second_ctor_owner + 0x10u, ptmf_second_ctor_target, nullptr, 0);
    wrapper_probe = {};
    wrapper_context = {};
    wrapper_context.gpr[1] = ptmf_wrapper_stack;
    wrapper_context.gpr[3] = ptmf_second_ctor_owner;
    wrapper_context.lr = ptmf_wrapper_lr;
    galaxy::native_ptmf_wrapper_800C9AAC(
        &wrapper_context, &memory, &wrapper_services, 0x800C9AACu);
    passed &= expect(
        wrapper_probe.calls == 1u &&
            wrapper_probe.guest_address == (ptmf_second_ctor_target & 0xFFFFFFFCu) &&
            wrapper_probe.gpr3 == ptmf_second_ctor_this + 0x24u &&
            wrapper_probe.gpr4 == ptmf_second_ctor_owner &&
            wrapper_probe.lr == 0x800C9AC8u &&
            wrapper_context.gpr[1] == ptmf_wrapper_stack &&
            wrapper_context.gpr[0] == ptmf_wrapper_lr &&
            wrapper_context.lr == ptmf_wrapper_lr,
        "0x800C9AAC PTMF wrapper preserves constructor dispatch side effects");

    constexpr std::uint32_t particle_owner = 0x80002200u;
    constexpr std::uint32_t particle_stack = 0x80002700u;
    constexpr std::uint32_t particle_arg = 0x80002300u;
    constexpr std::uint32_t particle_node = 0x80002404u;
    constexpr std::uint32_t particle_owner_vtable = 0x80002500u;
    constexpr std::uint32_t particle_node_vtable = 0x80002600u;
    constexpr std::uint32_t particle_init_target = 0x80445678u;
    constexpr std::uint32_t particle_node_target = 0x8045678Cu;
    galaxy::guest_store_u32(&memory, particle_owner, particle_owner_vtable, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, particle_owner_vtable + 0x18u, particle_init_target, nullptr, 0);
    galaxy::guest_store_u8(&memory, particle_owner + 0xB7u, 1u, nullptr, 0);
    galaxy::guest_store_u32(&memory, particle_owner + 0x14u, particle_node, nullptr, 0);
    galaxy::guest_store_u32(&memory, particle_node - 4u, particle_node_vtable, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, particle_node_vtable + 0x14u, particle_node_target, nullptr, 0);
    galaxy::guest_store_u32(
        &memory, particle_node, particle_owner + 0x14u, nullptr, 0);
    CachedCallProbe particle_probe{};
    galaxy::NativeServicesV1 particle_services{};
    particle_services.user = &particle_probe;
    particle_services.call_guest_cached = &capture_cached_call;
    particle_services.branch_checkpoint = &capture_branch_checkpoint;
    galaxy::PpcContext particle_context{};
    particle_context.pc = 0x8000C53Cu;
    particle_context.gpr[1] = particle_stack;
    particle_context.gpr[3] = particle_owner;
    particle_context.gpr[4] = particle_arg;
    particle_context.gpr[29] = 0xD0000029u;
    particle_context.gpr[30] = 0xD0000030u;
    particle_context.gpr[31] = 0xD0000031u;
    particle_context.lr = ptmf_wrapper_lr;
    galaxy::native_particle_list_apply_8000C53C(
        &particle_context, &memory, &particle_services, 0x8000C53Cu);
    const bool particle_list_ok =
        particle_probe.calls == 2u &&
        particle_probe.guest_addresses[0] == (particle_init_target & 0xFFFFFFFCu) &&
        particle_probe.guest_addresses[1] == (particle_node_target & 0xFFFFFFFCu) &&
        particle_probe.branch_checkpoints == 1u &&
        particle_probe.branch_pc == 0x8000C5A0u &&
        particle_probe.branch_resume_pc == 0x8000C580u &&
        particle_context.gpr[1] == particle_stack &&
        particle_context.lr == ptmf_wrapper_lr &&
        particle_context.gpr[29] == 0xD0000029u &&
        particle_context.gpr[30] == 0xD0000030u &&
        particle_context.gpr[31] == 0xD0000031u;
    if (!particle_list_ok) {
        std::cerr << "particle-list actual: calls=" << particle_probe.calls
                  << " addr0=0x" << std::hex << particle_probe.guest_addresses[0]
                  << " addr1=0x" << particle_probe.guest_addresses[1]
                  << " checkpoints=" << std::dec
                  << particle_probe.branch_checkpoints << " branch_pc=0x"
                  << std::hex << particle_probe.branch_pc << " sp=0x"
                  << particle_context.gpr[1] << " lr=0x" << particle_context.lr
                  << " r29=0x" << particle_context.gpr[29] << " r30=0x"
                  << particle_context.gpr[30] << " r31=0x"
                  << particle_context.gpr[31] << std::dec << '\n';
    }
    passed &= expect(
        particle_list_ok,
        "0x8000C53C particle-list helper preserves callbacks, stack, and registers");

    RestartabilityProbe particle_restart_probe{};
    galaxy::NativeServicesV1 particle_restart_services{};
    particle_restart_services.user = &particle_restart_probe;
    particle_restart_services.call_guest_cached =
        &return_from_cached_call_for_restartability;
    particle_restart_services.branch_checkpoint =
        &throw_guest_transfer_on_checkpoint;
    particle_context = {};
    particle_context.pc = 0x8000C53Cu;
    particle_context.gpr[1] = particle_stack;
    particle_context.gpr[3] = particle_owner;
    particle_context.gpr[4] = particle_arg;
    particle_context.gpr[29] = 0xD0000029u;
    particle_context.gpr[30] = 0xD0000030u;
    particle_context.gpr[31] = 0xD0000031u;
    particle_context.lr = ptmf_wrapper_lr;
    bool particle_transfer_observed = false;
    try {
        galaxy::native_particle_list_apply_8000C53C(
            &particle_context,
            &memory,
            &particle_restart_services,
            0x8000C53Cu);
    } catch (const SimulatedGuestTransfer&) {
        particle_transfer_observed = true;
    }
    constexpr std::uint32_t particle_frame = particle_stack - 0x20u;
    passed &= expect(
        particle_transfer_observed &&
            particle_restart_probe.guest_calls == 1u &&
            particle_restart_probe.guest_address ==
                (particle_init_target & 0xFFFFFFFCu) &&
            particle_restart_probe.checkpoints == 1u &&
            particle_restart_probe.checkpoint_pc == 0x8000C5A0u &&
            particle_context.pc == 0x8000C580u &&
            particle_context.gpr[1] == particle_frame &&
            particle_context.gpr[29] == particle_arg &&
            particle_context.gpr[30] == particle_owner + 0x14u &&
            particle_context.gpr[31] == particle_node &&
            galaxy::guest_load_u32(
                &memory, particle_frame, nullptr, 0) == particle_stack &&
            galaxy::guest_load_u32(
                &memory, particle_frame + 0x24u, nullptr, 0) ==
                ptmf_wrapper_lr,
        "particle-list helper publishes the taken target with an exact resumable frame");

    constexpr std::uint32_t pane_name_a = 0x80002780u;
    constexpr std::uint32_t pane_name_b = 0x800027A0u;
    const auto store_bytes = [&](std::uint32_t address, const char* text) {
        const std::size_t length = std::strlen(text) + 1u;
        for (std::size_t i = 0; i < length; ++i) {
            galaxy::guest_store_u8(
                &memory,
                address + static_cast<std::uint32_t>(i),
                static_cast<std::uint8_t>(text[i]),
                nullptr,
                0);
        }
    };
    store_bytes(pane_name_a, "IconMario");
    store_bytes(pane_name_b, "IconMario");
    galaxy::PpcContext pane_name_context{};
    pane_name_context.gpr[3] = pane_name_a;
    pane_name_context.gpr[4] = pane_name_b;
    galaxy::native_strncmp_eq_16_80015394(
        &pane_name_context, &memory, nullptr, 0x80015394u);
    const bool pane_name_equal = pane_name_context.gpr[3] == 1u;
    store_bytes(pane_name_b, "IconLuigi");
    pane_name_context.gpr[3] = pane_name_a;
    pane_name_context.gpr[4] = pane_name_b;
    galaxy::native_strncmp_eq_16_80015394(
        &pane_name_context, &memory, nullptr, 0x80015394u);
    const bool pane_name_different = pane_name_context.gpr[3] == 0u;
    store_bytes(pane_name_a, "Icon");
    store_bytes(pane_name_b, "Icon");
    galaxy::guest_store_u8(&memory, pane_name_b + 5u, 'X', nullptr, 0);
    pane_name_context.gpr[3] = pane_name_a;
    pane_name_context.gpr[4] = pane_name_b;
    galaxy::native_strncmp_eq_16_80015394(
        &pane_name_context, &memory, nullptr, 0x80015394u);
    passed &= expect(
        pane_name_equal && pane_name_different && pane_name_context.gpr[3] == 1u,
        "0x80015394 fixed-length name helper matches strncmp equality semantics");

    CachedCallProbe audio_probe{};
    galaxy::NativeServicesV1 audio_services{};
    audio_services.user = &audio_probe;
    audio_services.call_guest_cached = &capture_cached_call;
    constexpr std::uint32_t audio_stack = 0x80001000u;
    constexpr std::uint32_t audio_sda = 0x80002100u;
    galaxy::guest_store_u32(&memory, audio_stack + 0x14u, 0xA0D10000u, nullptr, 0);
    galaxy::guest_store_u32(&memory, audio_stack + 0x0Cu, 0x31313131u, nullptr, 0);
    galaxy::guest_store_u32(&memory, audio_stack + 0x08u, 0x30303030u, nullptr, 0);
    galaxy::guest_store_u32(&memory, audio_sda + 0xFFFFDF30u, 0x80000080u, nullptr, 0);
    galaxy::PpcContext audio_context{};
    audio_context.pc = 0x804955ECu;
    audio_context.gpr[1] = audio_stack;
    audio_context.gpr[13] = audio_sda;
    galaxy::native_audio_voice_update_8049559C(
        &audio_context, &memory, &audio_services, 0x8049559Cu);
    passed &= expect(
        audio_probe.calls == 1u &&
            audio_probe.guest_address == 0x80495EA0u &&
            audio_probe.lr == 0x804955F8u,
        "audio voice update final path targets the real 0x80495EA0 body");

    galaxy::PpcContext compare_context{};
    galaxy::compare_f64(
        &compare_context, 0, 0x3FF0000000000000ull, 0x4000000000000000ull, false);
    passed &= expect((compare_context.cr >> 28) == 0x8u,
                     "floating compare reports less than");
    passed &= expect((compare_context.fpscr & 0x0000F000u) == 0x00008000u,
                     "floating compare updates FPCC");
    galaxy::compare_f64(
        &compare_context, 0, 0x8000000000000000ull, 0x0000000000000000ull, false);
    passed &= expect((compare_context.cr >> 28) == 0x2u,
                     "floating compare treats signed zero as equal");

    compare_context = {};
    galaxy::compare_f64(
        &compare_context, 1, 0x7FF8000000000001ull, 0x3FF0000000000000ull, false);
    passed &= expect(((compare_context.cr >> 24) & 0xFu) == 0x1u,
                     "unordered compare reports quiet NaN");
    passed &= expect((compare_context.fpscr & 0xA1080000u) == 0,
                     "unordered quiet NaN does not raise invalid");
    compare_context = {};
    galaxy::compare_f64(
        &compare_context, 1, 0x7FF0000000000001ull, 0x3FF0000000000000ull, false);
    passed &= expect((compare_context.fpscr & 0xA1000000u) == 0xA1000000u,
                     "unordered signaling NaN records VXSNAN");
    const std::uint32_t repeated_invalid_fpscr = compare_context.fpscr & ~0x80000000u;
    compare_context.fpscr = repeated_invalid_fpscr;
    galaxy::compare_f64(
        &compare_context, 1, 0x7FF0000000000001ull, 0x3FF0000000000000ull, false);
    passed &= expect((compare_context.fpscr & 0x80000000u) == 0,
                     "an already-recorded invalid detail does not raise FX again");
    compare_context = {};
    galaxy::compare_f64(
        &compare_context, 1, 0x7FF8000000000001ull, 0x3FF0000000000000ull, true);
    passed &= expect((compare_context.fpscr & 0xA0080000u) == 0xA0080000u,
                     "ordered quiet NaN records invalid compare");

    galaxy::PpcContext fpscr_context{};
    galaxy::set_fpscr_bit(&fpscr_context, 3);
    passed &= expect((fpscr_context.fpscr & 0x10000000u) != 0,
                     "mtfsb1 helper sets the selected FPSCR bit");
    galaxy::set_fpscr_bit(&fpscr_context, 1);
    passed &= expect((fpscr_context.fpscr & 0x40000000u) == 0,
                     "FEX cannot be explicitly set");
    fpscr_context.fpscr |= 0x00000040u;
    galaxy::refresh_fpscr_summaries(&fpscr_context);
    passed &= expect((fpscr_context.fpscr & 0x40000000u) != 0,
                     "FEX is derived from enabled exception state");
    galaxy::clear_fpscr_bit(&fpscr_context, 3);
    passed &= expect((fpscr_context.fpscr & 0x50000000u) == 0,
                     "clearing overflow refreshes FEX");
    fpscr_context.fpscr = 0xA0000000u;
    galaxy::write_fpscr_fields(&fpscr_context, 0x12345678u, 0xFFu);
    galaxy::PpcContext expected_fpscr_context{};
    expected_fpscr_context.fpscr = 0x12345678u & ~0x60000000u;
    galaxy::refresh_fpscr_summaries(&expected_fpscr_context);
    passed &= expect(
        fpscr_context.fpscr == expected_fpscr_context.fpscr,
        "mtfsf writes non-summary fields and derives FEX and VX");

    galaxy::NativeServicesV1 services{};
    services.time_base_ticks = &fixed_time_base;
    {
        FpuUnavailableProbe probe{};
        galaxy::NativeServicesV1 fpu_services{};
        fpu_services.user = &probe;
        fpu_services.fpu_unavailable = &simulate_fpu_unavailable;
        galaxy::PpcContext fpu_context{};
        galaxy::GuestMemoryV1 fpu_memory{};

        fpu_context.msr = galaxy::kMsrFloatingPointAvailable;
        galaxy::ensure_fpu_available(
            &fpu_services, 0x80001234u, &fpu_context, &fpu_memory);
        passed &= expect(
            probe.calls == 0u,
            "MSR[FP]-enabled AOT instruction bypasses the exception service");

        fpu_context.msr = 0u;
        fpu_context.pc = 0x80000000u;
        galaxy::ensure_fpu_available(
            &fpu_services, 0x80005678u, &fpu_context, &fpu_memory);
        passed &= expect(
            probe.calls == 1u &&
                probe.guest_pc == 0x80005678u &&
                probe.context == &fpu_context && probe.memory == &fpu_memory &&
                fpu_context.pc == 0x80005678u &&
                (fpu_context.msr & galaxy::kMsrFloatingPointAvailable) != 0u,
            "MSR[FP]-disabled AOT instruction returns only after one exact same-PC exception retry");

        fpu_services.fatal = &throw_execution_fault;
        probe.enable_fp = false;
        fpu_context.msr = 0u;
        fpu_context.pc = 0x80000000u;
        bool rejected_missing_fp = false;
        try {
            galaxy::ensure_fpu_available(
                &fpu_services, 0x80006780u, &fpu_context, &fpu_memory);
        } catch (const SimulatedExecutionFault&) {
            rejected_missing_fp = true;
        }
        passed &= expect(
            rejected_missing_fp,
            "FPU exception service cannot return without enabling MSR[FP]");

        probe.enable_fp = true;
        probe.resume_pc_delta = 4u;
        fpu_context.msr = 0u;
        fpu_context.pc = 0x80000000u;
        bool rejected_wrong_pc = false;
        try {
            galaxy::ensure_fpu_available(
                &fpu_services, 0x80007890u, &fpu_context, &fpu_memory);
        } catch (const SimulatedExecutionFault&) {
            rejected_wrong_pc = true;
        }
        passed &= expect(
            rejected_wrong_pc,
            "FPU exception service cannot return at a different instruction PC");
    }
    {
        constexpr std::uint32_t kGuestContext = 0x80001000u;
        constexpr std::uint32_t kResumeAddress = 0x804BD818u;
        constexpr std::uint32_t kStateOffset = 0x1A2u;

        std::array<std::byte, 0x1C4u> context_backing{};
        context_backing.fill(std::byte{0xA5u});
        galaxy::GuestMemoryRegionV1 context_region{
            .guest_base = kGuestContext,
            .size = static_cast<std::uint32_t>(context_backing.size()),
            .host_base = context_backing.data(),
        };
        galaxy::GuestMemoryV1 context_memory{};
        context_memory.region_count = 1u;
        context_memory.regions = &context_region;
        galaxy::guest_store_u16(
            &context_memory,
            kGuestContext + kStateOffset,
            0x0101u,
            nullptr,
            kResumeAddress);

        galaxy::PpcContext vector_context{};
        for (std::uint32_t index = 0u; index < 32u; ++index) {
            vector_context.gpr[index] = 0x11000000u + index;
        }
        vector_context.cr = 0x22334455u;
        vector_context.lr = 0x66778899u;
        vector_context.ctr = 0xAABBCCDDu;
        vector_context.xer = 0xEEFF0011u;
        vector_context.msr = 0xFFFFFFFFu;

        galaxy::interrupt::save_low_memory_exception_vector_context(
            vector_context,
            kResumeAddress,
            kGuestContext,
            &context_memory,
            nullptr);

        std::array<std::byte, 0x1C4u> expected_backing{};
        expected_backing.fill(std::byte{0xA5u});
        const auto expect_u16 = [&](std::uint32_t offset, std::uint16_t value) {
            expected_backing[offset] =
                static_cast<std::byte>((value >> 8u) & 0xFFu);
            expected_backing[offset + 1u] =
                static_cast<std::byte>(value & 0xFFu);
        };
        const auto expect_u32 = [&](std::uint32_t offset, std::uint32_t value) {
            expected_backing[offset] =
                static_cast<std::byte>((value >> 24u) & 0xFFu);
            expected_backing[offset + 1u] =
                static_cast<std::byte>((value >> 16u) & 0xFFu);
            expected_backing[offset + 2u] =
                static_cast<std::byte>((value >> 8u) & 0xFFu);
            expected_backing[offset + 3u] =
                static_cast<std::byte>(value & 0xFFu);
        };
        for (std::uint32_t index = 3u; index <= 5u; ++index) {
            expect_u32(index * 4u, vector_context.gpr[index]);
        }
        expect_u32(0x80u, vector_context.cr);
        expect_u32(0x84u, vector_context.lr);
        expect_u32(0x88u, vector_context.ctr);
        expect_u32(0x8Cu, vector_context.xer);
        expect_u32(0x198u, kResumeAddress);
        expect_u32(
            0x19Cu,
            galaxy::interrupt::hardware_exception_srr1(vector_context.msr));
        expect_u16(kStateOffset, 0x0103u);

        const auto load_u32 = [&](std::uint32_t offset) {
            return galaxy::guest_load_u32(
                &context_memory,
                kGuestContext + offset,
                nullptr,
                kResumeAddress);
        };
        passed &= expect(
            load_u32(3u * 4u) == vector_context.gpr[3] &&
                load_u32(4u * 4u) == vector_context.gpr[4] &&
                load_u32(5u * 4u) == vector_context.gpr[5] &&
                load_u32(0x80u) == vector_context.cr &&
                load_u32(0x84u) == vector_context.lr &&
                load_u32(0x88u) == vector_context.ctr &&
                load_u32(0x8Cu) == vector_context.xer &&
                load_u32(0x198u) == kResumeAddress &&
                load_u32(0x19Cu) ==
                    galaxy::interrupt::hardware_exception_srr1(
                        vector_context.msr) &&
                galaxy::guest_load_u16(
                    &context_memory,
                    kGuestContext + kStateOffset,
                    nullptr,
                    kResumeAddress) == 0x0103u,
            "low-memory exception vector saves its exact integer/machine-state subset");
        passed &= expect(
            context_backing == expected_backing,
            "low-memory exception vector changes no bytes outside its exact save subset");
    }
    galaxy::PpcContext machine_context{};
    machine_context.time_base_offset = 0x100u;
    passed &= expect(
        galaxy::read_spr(&machine_context, 268, &services, 0) == 0x23456889u,
        "time-base low reads use native Wii-rate ticks and guest offset");
    passed &= expect(
        galaxy::read_spr(&machine_context, 269, &services, 0) == 1u,
        "time-base high reads the same 64-bit native clock");
    galaxy::write_spr(&machine_context, 284, 0xABCDEF01u, &services, 0);
    passed &= expect(
        galaxy::read_spr(&machine_context, 268, &services, 0) == 0xABCDEF01u,
        "time-base low writes adjust the guest clock");
    galaxy::write_spr(&machine_context, 285, 0x12345678u, &services, 0);
    passed &= expect(
        galaxy::read_time_base(&machine_context, &services, 0) ==
            0x12345678ABCDEF01ull,
        "time-base high writes preserve the low half");

    DecrementerWriteProbe decrementer_probe{};
    decrementer_probe.ticks = 10'000u;
    galaxy::NativeServicesV1 decrementer_services{};
    decrementer_services.user = &decrementer_probe;
    decrementer_services.time_base_ticks = &mutable_time_base;
    decrementer_services.decrementer_written = &capture_decrementer_write;
    galaxy::GuestMemoryV1 decrementer_memory{};
    machine_context.pc = 0x80001234u;
    machine_context.decrementer_start_ticks = 9'995u;
    machine_context.decrementer_start_value = 100u;
    galaxy::write_decrementer_and_notify(
        &machine_context,
        200u,
        &decrementer_memory,
        &decrementer_services,
        0x80001234u,
        0x80001238u);
    passed &= expect(
        decrementer_probe.calls == 1u &&
            decrementer_probe.guest_pc == 0x80001234u &&
            decrementer_probe.resume_pc == 0x80001238u &&
            decrementer_probe.write_ticks == 10'000u &&
            decrementer_probe.previous_value == 95u &&
            decrementer_probe.observed_context_pc == 0x80001238u &&
            decrementer_probe.context == &machine_context &&
            decrementer_probe.memory == &decrementer_memory &&
            machine_context.pc == 0x80001238u &&
            machine_context.decrementer_start_ticks == 10'000u &&
            machine_context.decrementer_start_value == 200u,
        "statically emitted DEC write publishes one exact native deadline identity");
    machine_context.time_base_offset += 0x1234u;
    passed &= expect(
        galaxy::read_spr(
            &machine_context, 22, &decrementer_services, 0) == 200u,
        "guest time-base writes do not move the independent decrementer");
    decrementer_probe.ticks += 7u;
    passed &= expect(
        galaxy::decrementer_elapsed_ticks(
            &machine_context, &decrementer_services, 0) == 7u &&
            galaxy::read_spr(
                &machine_context, 22, &decrementer_services, 0) == 193u,
            "decrementer advances only on the raw native Wii timeline");
    galaxy::NativeServicesV1 invalid_decrementer_services =
        decrementer_services;
    invalid_decrementer_services.fatal = &throw_execution_fault;
    invalid_decrementer_services.decrementer_written = nullptr;
    const std::uint64_t before_rejected_ticks =
        machine_context.decrementer_start_ticks;
    const std::uint32_t before_rejected_value =
        machine_context.decrementer_start_value;
    bool rejected_missing_decrementer_callback = false;
    try {
        galaxy::write_decrementer_and_notify(
            &machine_context,
            0x12345678u,
            &decrementer_memory,
            &invalid_decrementer_services,
            0x80001240u,
            0x80001244u);
    } catch (const SimulatedExecutionFault&) {
        rejected_missing_decrementer_callback = true;
    }
    passed &= expect(
        rejected_missing_decrementer_callback &&
            machine_context.decrementer_start_ticks == before_rejected_ticks &&
            machine_context.decrementer_start_value == before_rejected_value,
        "DEC write hard-fails without mutating state when its native callback is absent");
    bool rejected_generic_decrementer_write = false;
    try {
        galaxy::write_spr(
            &machine_context,
            22u,
            0x89ABCDEFu,
            &invalid_decrementer_services,
            0x80001248u);
    } catch (const SimulatedExecutionFault&) {
        rejected_generic_decrementer_write = true;
    }
    passed &= expect(
        rejected_generic_decrementer_write &&
            machine_context.decrementer_start_ticks == before_rejected_ticks &&
            machine_context.decrementer_start_value == before_rejected_value,
        "generic SPR lowering cannot bypass the statically emitted DEC callback");
    machine_context.hid2 = 0xF00F0000u;
    const galaxy::PpcTimerState timer_state =
        galaxy::capture_timer_state(&machine_context);
    const std::uint32_t saved_r3 = machine_context.gpr[3];
    machine_context = {};
    machine_context.gpr[3] = saved_r3 + 1u;
    galaxy::restore_timer_state(&machine_context, timer_state);
    passed &= expect(
        machine_context.time_base_offset == timer_state.time_base_offset &&
            machine_context.decrementer_start_ticks ==
                timer_state.decrementer_start_ticks &&
            machine_context.decrementer_start_value ==
                timer_state.decrementer_start_value &&
            machine_context.hid2 == 0xF00F0000u &&
            machine_context.gpr[3] == saved_r3 + 1u,
        "host callback restoration preserves machine state without replacing registers");
    galaxy::write_spr(&machine_context, 1008, 0xDEADBEEFu, &services, 0);
    passed &= expect(
        galaxy::read_spr(&machine_context, 1008, &services, 0) == 0xDEADBEEFu,
        "implementation SPR state round trips");

    {
        CachedCallProbe checkpoint_probe{};
        galaxy::NativeServicesV1 checkpoint_services{};
        checkpoint_services.user = &checkpoint_probe;
        checkpoint_services.branch_checkpoint = &capture_branch_checkpoint;
        std::uint64_t checkpoint_counter = 0;
        std::uint64_t next_slow_checkpoint = 3;
        checkpoint_services.checkpoint_counter = &checkpoint_counter;
        checkpoint_services.checkpoint_next_slow = &next_slow_checkpoint;
        galaxy::PpcContext checkpoint_context{};
        checkpoint_context.pc = 0xDEADBEECu;
        checkpoint_context.gpr[30] = 0x30303030u;
        checkpoint_context.gpr[31] = 0x31313131u;
        galaxy::branch_checkpoint(
            &checkpoint_services,
            0x80001000u,
            &checkpoint_context,
            nullptr);
        galaxy::branch_checkpoint(
            &checkpoint_services,
            0x80001004u,
            &checkpoint_context,
            nullptr);
        passed &= expect(
            checkpoint_counter == 2u &&
                checkpoint_probe.branch_checkpoints == 0u &&
                checkpoint_context.pc == 0xDEADBEECu,
            "inline branch-checkpoint gate skips callbacks before next slow checkpoint");
        galaxy::branch_checkpoint(
            &checkpoint_services,
            0x80001008u,
            &checkpoint_context,
            nullptr);
        passed &= expect(
            checkpoint_counter == 3u &&
                checkpoint_probe.branch_checkpoints == 1u &&
                checkpoint_probe.branch_pc == 0x80001008u &&
                checkpoint_context.pc == 0x80001008u &&
                checkpoint_probe.branch_gpr30 == 0x30303030u &&
                checkpoint_probe.branch_gpr31 == 0x31313131u,
            "inline branch-checkpoint gate calls the slow service at next slow checkpoint");
        next_slow_checkpoint = 5;
        galaxy::branch_checkpoint(
            &checkpoint_services,
            0x8000100Cu,
            &checkpoint_context,
            nullptr);
        passed &= expect(
            checkpoint_context.pc == 0x80001008u,
            "inline branch-checkpoint fast path keeps the last slow-service PC");
        galaxy::branch_checkpoint(
            &checkpoint_services,
            0x80001010u,
            &checkpoint_context,
            nullptr);
        passed &= expect(
            checkpoint_counter == 5u &&
                checkpoint_probe.branch_checkpoints == 2u &&
                checkpoint_probe.branch_pc == 0x80001010u &&
                checkpoint_context.pc == 0x80001010u,
            "inline branch-checkpoint gate follows runtime-updated next slow checkpoint");
        next_slow_checkpoint = 6;
        galaxy::branch_checkpoint_taken(
            &checkpoint_services,
            0x80001014u,
            0x80002000u,
            &checkpoint_context,
            nullptr);
        passed &= expect(
            checkpoint_counter == 6u &&
                checkpoint_probe.branch_checkpoints == 3u &&
                checkpoint_probe.branch_pc == 0x80001014u &&
                checkpoint_context.pc == 0x80002000u,
            "taken branch checkpoint preserves the branch site and publishes the resume target");

        std::atomic_uint32_t pending_event_mask{1u};
        checkpoint_services.pending_event_mask = &pending_event_mask;
        next_slow_checkpoint = 100u;
        galaxy::branch_checkpoint_taken(
            &checkpoint_services,
            0x80001018u,
            0x80002004u,
            &checkpoint_context,
            nullptr);
        passed &= expect(
            checkpoint_counter == 7u &&
                checkpoint_probe.branch_checkpoints == 4u &&
                checkpoint_probe.branch_pc == 0x80001018u &&
                checkpoint_context.pc == 0x80002004u,
            "pending native event bypasses the inline checkpoint fast gate");
        pending_event_mask.store(0u, std::memory_order_release);
        galaxy::branch_checkpoint_taken(
            &checkpoint_services,
            0x8000101Cu,
            0x80002008u,
            &checkpoint_context,
            nullptr);
        passed &= expect(
            checkpoint_counter == 8u &&
                checkpoint_probe.branch_checkpoints == 4u &&
                checkpoint_context.pc == 0x80002004u,
            "cleared native event returns to the inline checkpoint fast gate");
        pending_event_mask.store(0u, std::memory_order_release);
        checkpoint_context.msr |= galaxy::kMsrExternalInterruptEnable;
        galaxy::architectural_interrupt_checkpoint(
            &checkpoint_services,
            0x80001020u,
            0x80001024u,
            &checkpoint_context,
            nullptr);
        passed &= expect(
            checkpoint_counter == 9u &&
                checkpoint_probe.branch_checkpoints == 5u &&
                checkpoint_probe.branch_pc == 0x80001020u &&
                checkpoint_probe.branch_resume_pc == 0x80001024u &&
                checkpoint_context.pc == 0x80001024u,
            "mtmsr architectural checkpoint bypasses the adaptive gate exactly once");
        checkpoint_context.msr &= ~galaxy::kMsrExternalInterruptEnable;
        galaxy::architectural_interrupt_checkpoint(
            &checkpoint_services,
            0x80001028u,
            0x8000102Cu,
            &checkpoint_context,
            nullptr);
        passed &= expect(
            checkpoint_counter == 9u &&
                checkpoint_probe.branch_checkpoints == 5u &&
                checkpoint_context.pc == 0x8000102Cu,
            "mtmsr with newly cleared EE publishes its continuation without a host pump");
    }

    {
        constexpr std::array<std::uint32_t, 5> wpad_status_copy_calls{
            0x804D945Cu,
            0x804D947Cu,
            0x804D949Cu,
            0x804D94B0u,
            0x804D94C4u,
        };
        constexpr std::array<std::uint32_t, 4> wpad_zero_fill_calls{
            0x804D94E4u,
            0x804D9504u,
            0x804D9524u,
            0x804D9538u,
        };
        constexpr std::array<std::uint32_t, 4> classified_input_calls{
            0x8045080Cu,
            0x803AA570u,
            0x803AB2B0u,
            0x803ABDF0u,
        };

        passed &= expect(
            galaxy::is_wpad_read_status_copy_call_pc(0x804D945Cu) &&
                galaxy::is_wpad_read_status_copy_call_pc(0x804D947Cu) &&
                galaxy::is_wpad_read_status_copy_call_pc(0x804D949Cu) &&
                galaxy::is_wpad_read_status_copy_call_pc(0x804D94B0u) &&
                galaxy::is_wpad_read_status_copy_call_pc(0x804D94C4u),
            "WPADRead trace predicate accepts every proven status-copy call");
        passed &= expect(
            !galaxy::is_wpad_read_status_copy_call_pc(0x804D9458u) &&
                !galaxy::is_wpad_read_status_copy_call_pc(0x804D9460u) &&
                !galaxy::is_wpad_read_status_copy_call_pc(0x804D94E4u) &&
                !galaxy::is_wpad_read_status_copy_call_pc(0x804D9504u) &&
                !galaxy::is_wpad_read_status_copy_call_pc(0x804D9524u) &&
                !galaxy::is_wpad_read_status_copy_call_pc(0x804D9538u),
            "WPADRead trace predicate excludes adjacent and zero-fill/error calls");
        passed &= expect(
            galaxy::is_native_input_trace_call_return_pc(0x8045080Cu) &&
                galaxy::is_native_input_trace_call_return_pc(0x803AA570u) &&
                galaxy::is_native_input_trace_call_return_pc(0x803AB2B0u) &&
                galaxy::is_native_input_trace_call_return_pc(0x803ABDF0u) &&
                !galaxy::is_native_input_trace_call_return_pc(0x80450718u) &&
                !galaxy::is_wpad_read_status_copy_call_pc(0x8045080Cu) &&
                !galaxy::is_wpad_read_status_copy_call_pc(0x803AA570u) &&
                !galaxy::is_native_input_trace_call_return_pc(0x803AA56Cu) &&
                !galaxy::is_native_input_trace_call_return_pc(0x803AA574u),
            "native input trace exposes only the protected KPADRead and gameplay pointer proof PCs");

        CachedCallProbe checkpoint_probe{};
        galaxy::NativeServicesV1 checkpoint_services{};
        checkpoint_services.user = &checkpoint_probe;
        checkpoint_services.branch_checkpoint = &capture_branch_checkpoint;
        std::atomic_uint32_t pending_event_mask{0u};
        std::uint64_t checkpoint_counter = 0u;
        constexpr std::uint64_t next_slow_checkpoint =
            std::numeric_limits<std::uint64_t>::max();
        checkpoint_services.pending_event_mask = &pending_event_mask;
        checkpoint_services.checkpoint_counter = &checkpoint_counter;
        checkpoint_services.checkpoint_next_slow = &next_slow_checkpoint;
        galaxy::PpcContext checkpoint_context{};

        checkpoint_context.pc = 0xBAD00000u;
        galaxy::call_return_checkpoint(
            &checkpoint_services,
            wpad_status_copy_calls.front(),
            0x804D9460u,
            &checkpoint_context,
            nullptr);
        passed &= expect(
            checkpoint_probe.branch_checkpoints == 0u &&
                checkpoint_counter == 0u &&
                checkpoint_context.pc == 0xBAD00000u,
            "disabled WPADRead proof flag does not add a call-return callback");

        checkpoint_services.runtime_flags =
            galaxy::kNativeServiceFlagTraceWpadReadCopies;
        for (const std::uint32_t call_pc : wpad_status_copy_calls) {
            const std::uint32_t callbacks_before =
                checkpoint_probe.branch_checkpoints;
            checkpoint_context.pc = 0xBAD00000u;
            galaxy::call_return_checkpoint(
                &checkpoint_services,
                call_pc,
                call_pc + 4u,
                &checkpoint_context,
                nullptr);
            passed &= expect(
                checkpoint_probe.branch_checkpoints == callbacks_before + 1u &&
                    checkpoint_probe.branch_pc == call_pc &&
                    checkpoint_context.pc == call_pc + 4u,
                "enabled WPADRead proof flag exposes one callback for a status copy");
        }
        for (const std::uint32_t call_pc : classified_input_calls) {
            const std::uint32_t callbacks_before =
                checkpoint_probe.branch_checkpoints;
            checkpoint_context.pc = 0xBAD00000u;
            galaxy::call_return_checkpoint(
                &checkpoint_services,
                call_pc,
                call_pc + 4u,
                &checkpoint_context,
                nullptr);
            passed &= expect(
                checkpoint_probe.branch_checkpoints == callbacks_before + 1u &&
                    checkpoint_probe.branch_pc == call_pc &&
                    checkpoint_context.pc == call_pc + 4u,
                "enabled native input proof flag exposes each classified KPAD and gameplay-pointer checkpoint");
        }
        passed &= expect(
                checkpoint_probe.branch_checkpoints ==
                    wpad_status_copy_calls.size() +
                        classified_input_calls.size() &&
                checkpoint_counter ==
                    wpad_status_copy_calls.size() +
                        classified_input_calls.size(),
            "each proven input call-return site produces exactly one callback");

        for (const std::uint32_t call_pc : wpad_zero_fill_calls) {
            const std::uint32_t callbacks_before =
                checkpoint_probe.branch_checkpoints;
            checkpoint_context.pc = 0xBAD00000u;
            galaxy::call_return_checkpoint(
                &checkpoint_services,
                call_pc,
                call_pc + 4u,
                &checkpoint_context,
                nullptr);
            passed &= expect(
                checkpoint_probe.branch_checkpoints == callbacks_before &&
                    checkpoint_context.pc == 0xBAD00000u,
                "WPADRead zero-fill/error call does not produce a proof callback");
        }
        for (const std::uint32_t call_pc :
             std::array<std::uint32_t, 2>{0x804D9458u, 0x804D9460u}) {
            const std::uint32_t callbacks_before =
                checkpoint_probe.branch_checkpoints;
            galaxy::call_return_checkpoint(
                &checkpoint_services,
                call_pc,
                call_pc + 4u,
                &checkpoint_context,
                nullptr);
            passed &= expect(
                checkpoint_probe.branch_checkpoints == callbacks_before,
                "adjacent WPADRead PC does not produce a proof callback");
        }

        pending_event_mask.store(1u, std::memory_order_release);
        const std::uint32_t callbacks_before_pending_proof =
            checkpoint_probe.branch_checkpoints;
        galaxy::call_return_checkpoint(
            &checkpoint_services,
            wpad_status_copy_calls.front(),
            0x804D9460u,
            &checkpoint_context,
            nullptr);
        passed &= expect(
            checkpoint_probe.branch_checkpoints ==
                callbacks_before_pending_proof + 1u,
            "pending event plus WPADRead proof target produces one callback, not two");

        checkpoint_services.runtime_flags = 0u;
        const std::uint32_t callbacks_before_normal_pending =
            checkpoint_probe.branch_checkpoints;
        galaxy::call_return_checkpoint(
            &checkpoint_services,
            0x80001234u,
            0x80001238u,
            &checkpoint_context,
            nullptr);
        passed &= expect(
            checkpoint_probe.branch_checkpoints ==
                    callbacks_before_normal_pending + 1u &&
                checkpoint_probe.branch_pc == 0x80001234u &&
                checkpoint_context.pc == 0x80001238u,
            "ordinary pending-event call-return checkpoint behavior is unchanged");

        pending_event_mask.store(0u, std::memory_order_release);
        checkpoint_services.runtime_flags =
            galaxy::kNativeServiceFlagAdaptiveCallReturnCheckpoints;
        checkpoint_counter = 0u;
        constexpr std::uint64_t adaptive_next_slow_checkpoint = 2u;
        checkpoint_services.checkpoint_next_slow =
            &adaptive_next_slow_checkpoint;
        const std::uint32_t callbacks_before_adaptive =
            checkpoint_probe.branch_checkpoints;
        checkpoint_context.pc = 0xBAD00000u;
        galaxy::call_return_checkpoint(
            &checkpoint_services,
            0x8000123Cu,
            0x80001240u,
            &checkpoint_context,
            nullptr);
        passed &= expect(
            checkpoint_probe.branch_checkpoints == callbacks_before_adaptive &&
                checkpoint_counter == 1u &&
                checkpoint_context.pc == 0xBAD00000u,
            "adaptive call-return gate counts below-threshold returns without a callback");
        galaxy::call_return_checkpoint(
            &checkpoint_services,
            0x80001240u,
            0x80001244u,
            &checkpoint_context,
            nullptr);
        passed &= expect(
            checkpoint_probe.branch_checkpoints ==
                    callbacks_before_adaptive + 1u &&
                checkpoint_probe.branch_pc == 0x80001240u &&
                checkpoint_counter == 2u &&
                checkpoint_context.pc == 0x80001244u,
            "adaptive call-return gate enters one callback at its shared threshold");
    }

    std::array<std::byte, 0x200> dma_mem1{};
    std::array<std::byte, 0x200> dma_locked_cache{};
    for (std::size_t index = 0; index < dma_locked_cache.size(); ++index) {
        dma_locked_cache[index] =
            static_cast<std::byte>((index * 13u + 7u) & 0xFFu);
    }
    std::array<galaxy::GuestMemoryRegionV1, 2> dma_regions{{
        {
            .guest_base = 0x00000000u,
            .size = static_cast<std::uint32_t>(dma_mem1.size()),
            .host_base = dma_mem1.data(),
        },
        {
            .guest_base = 0xE0000000u,
            .size = static_cast<std::uint32_t>(dma_locked_cache.size()),
            .host_base = dma_locked_cache.data(),
        },
    }};
    galaxy::GuestMemoryV1 dma_memory{};
    dma_memory.region_count =
        static_cast<std::uint32_t>(dma_regions.size());
    dma_memory.regions = dma_regions.data();
    NotifyProbe dma_notify{};
    dma_memory.user = &dma_notify;
    dma_memory.notify_write = &capture_notify_write;
    galaxy::PpcContext dma_context{};
    constexpr std::uint32_t dma_destination = 0x00000040u;
    constexpr std::uint32_t dma_source = 0xE0000020u;
    constexpr std::uint32_t dma_blocks = 3u;
    galaxy::write_spr(
        &dma_context,
        922,
        (dma_destination & 0x1FFFFFE0u) | ((dma_blocks >> 2) & 0x1Fu),
        &dma_memory,
        &services,
        0x804A3194u);
    galaxy::write_spr(
        &dma_context,
        923,
        (dma_source & 0xFFFFFFE0u) | ((dma_blocks & 0x3u) << 2) | 0x2u,
        &dma_memory,
        &services,
        0x804A31A4u);
    for (std::size_t index = 0; index < dma_blocks * 32u; ++index) {
        passed &= expect(
            dma_mem1[dma_destination + index] ==
                dma_locked_cache[(dma_source & 0x1FFu) + index],
            "locked-cache DMA_L copies raw bytes into physical memory");
    }
    passed &= expect(
        dma_notify.writes == 1u &&
            dma_notify.address == dma_destination &&
            dma_notify.size == dma_blocks * 32u,
        "locked-cache DMA_L publishes guest-memory dirty range");

    passed &= expect(
        galaxy::trap_word_immediate_condition(0x10u, 0xFFFFFFFFu, 0u),
        "TWI signed-less condition");
    passed &= expect(
        galaxy::trap_word_immediate_condition(0x02u, 1u, 2u),
        "TWI unsigned-less condition");
    passed &= expect(
        !galaxy::trap_word_immediate_condition(0x08u, 1u, 2u),
        "TWI ignores disabled and false conditions");

    // divw edge-case tests: Broadway/Gekko hardware behavior with OE=0.
    // Normal division must still work.
    passed &= expect(
        galaxy::divide_signed_word(20u, 4u, nullptr, 0) == 5u,
        "divw normal positive dividend and divisor");
    passed &= expect(
        galaxy::divide_signed_word(static_cast<std::uint32_t>(-20), 4u, nullptr, 0) ==
            static_cast<std::uint32_t>(-5),
        "divw normal negative dividend positive divisor");
    // Divide by zero: sign bit of dividend selects 0xFFFFFFFF or 0.
    passed &= expect(
        galaxy::divide_signed_word(0x00000001u, 0u, nullptr, 0) == 0x00000000u,
        "divw positive dividend / 0 returns 0x00000000 (Broadway hardware)");
    passed &= expect(
        galaxy::divide_signed_word(0x80000001u, 0u, nullptr, 0) == 0xFFFFFFFFu,
        "divw negative dividend / 0 returns 0xFFFFFFFF (Broadway hardware)");
    passed &= expect(
        galaxy::divide_signed_word(0x00000000u, 0u, nullptr, 0) == 0x00000000u,
        "divw zero dividend / 0 returns 0x00000000");
    // INT_MIN / -1 overflow.
    passed &= expect(
        galaxy::divide_signed_word(0x80000000u, static_cast<std::uint32_t>(-1), nullptr, 0) ==
            0xFFFFFFFFu,
        "divw INT_MIN / -1 returns 0xFFFFFFFF (Broadway hardware)");

    // divwu edge-case tests.
    passed &= expect(
        galaxy::divide_unsigned_word(12u, 4u, nullptr, 0) == 3u,
        "divwu normal division");
    passed &= expect(
        galaxy::divide_unsigned_word(0xFFFFFFFFu, 0u, nullptr, 0) == 0x00000000u,
        "divwu divide by 0 returns 0x00000000 (Broadway hardware)");
    passed &= expect(
        galaxy::divide_unsigned_word(0u, 0u, nullptr, 0) == 0x00000000u,
        "divwu zero / 0 returns 0x00000000");

    passed &= test_checked_span_resolution();
    passed &= test_audio_interleave_order_and_overlap();
    passed &= test_vector_copy_instruction_effects();
    passed &= test_matrix_scale_publishes_before_exception();
    passed &= test_gpr_helper_ram_spans();
    passed &= test_gpr_save_tracker_admission();
    passed &= test_psq_complete_ram_instruction();
    passed &= test_psq_load_quantization_and_mode_contract();
    passed &= test_normal_f32_widening_fields();
    passed &= test_single_precision_operand_proof();
    if (!passed) {
        std::cerr << "FAILED: one or more Native ABI helper checks failed\n";
        return 1;
    }
    std::cout << "Native ABI helper tests passed\n";
    return 0;
}
