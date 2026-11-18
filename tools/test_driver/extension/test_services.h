// SPDX-License-Identifier: GPL-3.0-only
// The harness's test services for LumaCore's registry (luma_test_services.h; services/registry.md 14).
#pragma once

#include <luma_service_provider.h>

#include <cstdint>

namespace test_services {

// The providers, valid for the process: test.svc 1.3, test.secret 1.0 and `extra` test.extra.n<i> 1.0 (at most
// LUMA_TEST_EXTRA_MOST). `count` gets how many.
const LumaServiceProvider* const* providers(std::uint32_t extra, std::uint32_t* count);

}  // namespace test_services
