// SPDX-License-Identifier: GPL-3.0-only
// Per-site indirect call cache (translator --bctrl-site-cache):
// fast::call_guest_indirect_site calls a published direct entry, caches it,
// calls a repeat target from the cache without the table, keeps the event and
// suspension gates on every call, and sends everything else to the cached
// service boundary.

#include "galaxy/native_api.h"

#include <atomic>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << what << '\n';
    }
}

int direct_calls = 0;
int boundary_calls = 0;
std::uint32_t boundary_target = 0u;

void direct_target(galaxy::PpcContext* context, galaxy::GuestMemoryV1*, const galaxy::NativeServicesV1*) {
    ++direct_calls;
    context->gpr[3] = context->pc;
}

void cached_boundary(
    void*, std::uint32_t guest_address, std::uint32_t*, galaxy::NativeGameFunction*,
    galaxy::PpcContext*, galaxy::GuestMemoryV1*) {
    ++boundary_calls;
    boundary_target = guest_address;
}

}  // namespace

int main() {
    constexpr std::uint32_t kBase = 0x80004000u;
    std::vector<galaxy::NativeGameFunction> table(64u, nullptr);
    table[4] = &direct_target;  // 0x80004010
    std::atomic_uint32_t pending{0u};
    std::atomic_uint32_t suspended{0u};
    galaxy::NativeServicesV1 services{};
    services.direct_dispatch = table.data();
    services.direct_dispatch_base = kBase;
    services.direct_dispatch_count = 64u * 4u;
    services.pending_event_mask = &pending;
    services.direct_dispatch_suspended = &suspended;
    services.call_guest_cached = &cached_boundary;

    galaxy::PpcContext context{};
    std::uint32_t cached_target = 0u;
    galaxy::NativeGameFunction cached_function = nullptr;
    std::uint32_t site_target = galaxy::fast::kNoIndirectSite;
    galaxy::NativeGameFunction site_function = nullptr;
    const auto call = [&](std::uint32_t target) {
        galaxy::fast::call_guest_indirect_site(
            &services, target, &cached_target, &cached_function, &site_target, &site_function,
            &context, nullptr, 0x80001000u);
    };

    call(0x80004010u);
    check(direct_calls == 1 && boundary_calls == 0, "published entry is called directly");
    check(context.gpr[3] == 0x80004010u, "context->pc is the target");
    check(site_target == 0x80004010u && site_function == &direct_target, "the site caches it");

    // A repeat target is called from the site cache, without the table.
    table[4] = nullptr;
    call(0x80004010u);
    check(direct_calls == 2 && boundary_calls == 0, "repeat target hits the site cache");
    table[4] = &direct_target;

    // Gates: a pending event or a suspension sends even a cached target to the
    // service boundary.
    pending.store(1u);
    call(0x80004010u);
    check(direct_calls == 2 && boundary_calls == 1 && boundary_target == 0x80004010u,
        "pending event: boundary");
    pending.store(0u);
    suspended.store(1u);
    call(0x80004010u);
    check(direct_calls == 2 && boundary_calls == 2, "suspended dispatch: boundary");
    suspended.store(0u);

    // A withheld (null) entry, a misaligned target and a target outside the
    // table go to the boundary and leave the site cache alone.
    call(0x80004020u);
    check(boundary_calls == 3 && boundary_target == 0x80004020u, "withheld entry: boundary");
    call(0x80004012u);
    check(boundary_calls == 4, "misaligned target: boundary");
    call(0x80000000u);
    check(boundary_calls == 5, "below the table: boundary");
    call(kBase + 64u * 4u);
    check(boundary_calls == 6, "past the table: boundary");
    check(site_target == 0x80004010u, "site cache unchanged by misses");

    // The initial value matches no target: generated code passes
    // `ctr & ~3` (or `lr & ~3`), and the highest aligned address misses.
    static_assert((galaxy::fast::kNoIndirectSite & 3u) != 0u);
    std::uint32_t fresh_target = galaxy::fast::kNoIndirectSite;
    galaxy::NativeGameFunction fresh_function = nullptr;
    galaxy::fast::call_guest_indirect_site(
        &services, 0xFFFFFFFCu, &cached_target, &cached_function, &fresh_target,
        &fresh_function, &context, nullptr, 0x80001000u);
    check(boundary_calls == 7 && fresh_function == nullptr &&
            fresh_target == galaxy::fast::kNoIndirectSite,
        "a fresh site misses");

    if (failures != 0) {
        std::cerr << failures << " bctrl site cache check(s) failed\n";
        return 1;
    }
    std::cout << "native bctrl site cache tests passed\n";
    return 0;
}
