// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/luma_services_host.h"

#include "galaxy/host_extension.h"

#include <atomic>

namespace galaxy::luma_services {
namespace {

// Written on the game thread by provide_all, then read from any thread by a provider (acquire).
std::atomic<const LumaServiceKernel*> g_kernel{nullptr};
bool g_provided = false;  // game thread

struct Giving {
    LumaProvideService provide;
};

}  // namespace

bool provide_all(void* (*find_symbol)(const char*), void (*log)(const char*),
                 std::span<const LumaServiceProvider* const> own) noexcept {
    if (g_provided) {
        return true;
    }
    const auto provide = reinterpret_cast<LumaProvideService>(find_symbol != nullptr ? find_symbol("luma_provide_service") : nullptr);
    const auto open = reinterpret_cast<LumaServicesOpen>(find_symbol != nullptr ? find_symbol("luma_services_open") : nullptr);
    if (provide == nullptr || open == nullptr) {
        if (log != nullptr) {
            log("LumaCore services: the game module has no luma_services_open or luma_provide_service; mods get no service");
        }
        return false;
    }
    g_provided = true;
    // The module's own services first (the catalog checked), then the host's, then the extensions' test services.
    g_kernel.store(open(), std::memory_order_release);
    // A bad provider stops the game in the module (luma_service_provider.h), naming it; 0 is the only other answer.
    for (const LumaServiceProvider* const provider : own) {
        provide(provider);
    }
    Giving giving{provide};
    galaxy::extensions::luma_providers(
        [](const LumaServiceProvider* provider, void* user) { static_cast<Giving*>(user)->provide(provider); }, &giving);
    return true;
}

const LumaServiceKernel* kernel() noexcept {
    return g_kernel.load(std::memory_order_acquire);
}

}  // namespace galaxy::luma_services
