// SPDX-License-Identifier: GPL-3.0-only
// The harness's test services for LumaCore's registry (luma_test_services.h; services/registry.md 14).
#pragma once

#include <luma_service_provider.h>

#include <cstdint>

namespace test_services {

// The providers, valid for the process: test.svc 1.3 and test.secret 1.0. `count` gets 2.
const LumaServiceProvider* const* providers(std::uint32_t* count);

}  // namespace test_services
