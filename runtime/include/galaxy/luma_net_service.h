// SPDX-License-Identifier: GPL-3.0-only
// The host's side of LumaCore's net services (contract services-net; plan 2.7): the provider of `net.local` (layer 1,
// TCP on this machine and the local network).
#pragma once

#include <luma_service_provider.h>

#include <span>

namespace galaxy::luma_net {

// The providers, in catalog order, for luma_services::provide_all (game thread, once, before the first mod). `log`
// says what goes wrong outside any mod's call (an I/O thread that cannot start).
[[nodiscard]] std::span<const LumaServiceProvider* const> providers(void (*log)(const char*)) noexcept;

}  // namespace galaxy::luma_net
