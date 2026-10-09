#include "galaxy/native_api.h"

#include <atomic>
#include <cstdio>
#include <cstring>

namespace {
struct Transfer {};
struct Unavailable {};
struct Probe {
    unsigned calls{}, profiles{}, fatals{}, logs{};
    bool throws{};
    std::uint32_t address{}, caller{};
    galaxy::PpcContext* context{};
    galaxy::GuestMemoryV1* memory{};
};

void service(void* user, std::uint32_t address, std::uint32_t* cached_address,
    galaxy::NativeGameFunction* cached_function, galaxy::PpcContext* context,
    galaxy::GuestMemoryV1* memory) {
    auto& probe = *static_cast<Probe*>(user);
    ++probe.calls;
    probe.address = address;
    probe.context = context;
    probe.memory = memory;
    *cached_address = address;
    *cached_function = nullptr;
    if (context != nullptr) context->gpr[3] += 1u;
    if (probe.throws) throw Transfer{};
}

void profile(void* user, std::uint32_t caller, std::uint32_t address, std::uint64_t) {
    auto& probe = *static_cast<Probe*>(user);
    ++probe.profiles;
    probe.caller = caller;
    probe.address = address;
}

void log(void* user, galaxy::LogLevelV1, const char*) {
    ++static_cast<Probe*>(user)->logs;
}

[[noreturn]] void fatal(void* user, std::uint32_t caller, const char* message) {
    auto& probe = *static_cast<Probe*>(user);
    ++probe.fatals;
    probe.caller = caller;
    if (std::strcmp(message, "native cached guest-call service is unavailable") != 0) {
        std::abort();
    }
    throw Unavailable{};
}

bool check(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "%s\n", message);
    return condition;
}
}  // namespace

int main(int argc, char** argv) {
    // Each process starts with a fresh env singleton. The runtime window remains
    // mutable; opening it later must activate profiling even after an ordinary call.
    if (_putenv_s("GALAXY_PROFILE_GENERATED_DIRECT_EDGES", "1") != 0) return 2;
    const bool tracing = argc > 1 && std::strcmp(argv[1], "--trace") == 0;
    if (_putenv_s("GALAXY_TRACE_SAVE_SEQUENCE", tracing ? "1" : "0") != 0) return 2;
    if (_putenv_s("GALAXY_TRACE_SCENARIO_OPENING_STATE", tracing ? "1" : "0") != 0) return 2;
    Probe probe;
    std::atomic_bool window{false};
    galaxy::PpcContext context{};
    galaxy::GuestMemoryV1 memory{};
    galaxy::NativeServicesV1 services{};
    services.user = &probe;
    services.call_guest_cached = service;
    services.record_direct_edge_profile = profile;
    services.fatal = fatal;
    services.log = log;
    services.main_frame_trace_active = &window;
    std::uint32_t cached_address = 0u;
    galaxy::NativeGameFunction cached_function = nullptr;
    constexpr std::uint32_t target = 0x80001234u;
    const std::uint32_t caller = tracing ? 0x8016FB14u : 0x80005678u;
    const auto call = [&] {
        galaxy::call_guest_cached(&services, target, &cached_address,
            &cached_function, &context, &memory, caller);
    };
    bool passed = true;
    for (unsigned i = 0u; i < 3u; ++i) call();
    passed &= check(probe.calls == 3u && context.gpr[3] == 3u &&
        probe.profiles == 0u && cached_address == target &&
        probe.context == &context && probe.memory == &memory,
        "ordinary cache hits must cross the service boundary every time");
    window.store(true);
    call();
    passed &= check(probe.calls == 4u && probe.profiles == 1u &&
        probe.address == target && probe.caller == caller,
        "opening the runtime window must activate the existing profiler");
    window.store(false);
    call();
    passed &= check(probe.calls == 5u && probe.profiles == 1u,
        "closing the runtime window must stop profiling");
    probe.throws = true;
    for (bool active : {false, true}) {
        window.store(active);
        const auto before_calls = probe.calls, before_profiles = probe.profiles;
        bool transferred = false;
        try { call(); } catch (Transfer) { transferred = true; }
        passed &= check(transferred && probe.calls == before_calls + 1u &&
            probe.profiles == before_profiles + (active ? 1u : 0u),
            "both paths must preserve a thrown guest transfer and profile it only in-window");
    }
    probe.throws = false;
    for (unsigned missing = 0u; missing < 3u; ++missing) {
        auto unavailable = services;
        if (missing == 0u) unavailable.call_guest_cached = nullptr;
        bool faulted = false;
        const auto before_calls = probe.calls;
        try {
            galaxy::call_guest_cached(&unavailable, target,
                missing == 1u ? nullptr : &cached_address,
                missing == 2u ? nullptr : &cached_function,
                &context, &memory, caller);
        } catch (Unavailable) { faulted = true; }
        passed &= check(faulted && probe.calls == before_calls && probe.caller == caller,
            "missing service/cache must report the exact caller without dispatching");
    }
    passed &= check(probe.fatals == 3u, "every unavailable boundary must report once");
    passed &= check(tracing ? probe.logs != 0u : probe.logs == 0u,
        "opt-in trace policy must remain active without affecting ordinary calls");
    if (passed) std::puts("native cached-call boundary checks passed");
    return passed ? 0 : 1;
}
