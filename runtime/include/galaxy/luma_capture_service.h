// SPDX-License-Identifier: GPL-3.0-only
// The host's side of LumaCore's capture services (contract services-audio 7; plan 2.6 layers 5 and 6): the
// providers of `audio.microphone` and `capture.camera`.
#pragma once

#include <luma_service_provider.h>

#include <span>

namespace galaxy::luma_capture {

// The providers, in catalog order, for luma_services::provide_all (game thread, once, before the first mod).
[[nodiscard]] std::span<const LumaServiceProvider* const> providers(void (*log)(const char*)) noexcept;

}  // namespace galaxy::luma_capture
