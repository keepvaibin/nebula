// SPDX-License-Identifier: GPL-3.0-only
// The host's side of LumaCore's service registry (services-registry 7, 13): registers the services the host provides
// and the extensions' "test." services with the game module, once, before the first mod loads.
#pragma once

#include <luma_service_provider.h>

#include <cstddef>
#include <span>

namespace galaxy::luma_services {

// Game thread, from start_plugins after the service finder is given and before the first luma_load_mod. Opens the
// module's registry (luma_services_open: its own services), then registers each of `own` (the host's services, in
// catalog order) and every extension's provider through luma_provide_service; once a process: a second call does
// nothing. False, said through `log`, when the module lacks either export (the module is this build's, so a bug); the
// module itself stops the game on a bad provider.
bool provide_all(void* (*find_symbol)(const char*), void (*log)(const char*),
                 std::span<const LumaServiceProvider* const> own) noexcept;

// The module's kernel for the host's providers (null before provide_all found it).
[[nodiscard]] const LumaServiceKernel* kernel() noexcept;

}  // namespace galaxy::luma_services
