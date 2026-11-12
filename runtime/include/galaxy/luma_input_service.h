// SPDX-License-Identifier: GPL-3.0-only
// The host's side of LumaCore's input service (contract services-input): the provider of `input`, which every host
// registers (services-registry 1). A host that samples no controls (the headless host) leaves every action up.
#pragma once

#include <luma_service_provider.h>

#include <span>

namespace galaxy::luma_input {

// The provider, for luma_services::provide_all (game thread, once, before the first mod). `log` says what goes
// wrong outside any mod's call (an action left out when they are declared).
[[nodiscard]] std::span<const LumaServiceProvider* const> providers(void (*log)(const char*)) noexcept;

}  // namespace galaxy::luma_input
