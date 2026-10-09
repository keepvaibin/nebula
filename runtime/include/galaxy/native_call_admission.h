#pragma once

#include "galaxy/external_dispatch.h"

#include <cstdint>

namespace galaxy::runtime {

// These entry hooks precede the generic body's intercept filter. A warm
// translated-call cache must still enter that body, including outside an
// external interrupt. The body retains each hook's exact LR/input-mode guard.
[[nodiscard]] constexpr bool cached_call_has_required_entry_hook(
    std::uint32_t guest_address) noexcept {
    switch (guest_address) {
        case 0x8038D1CCu: // MoviePlayerSimple movie-content marker
        case 0x80370398u: // MoviePlayerSimple stop-owned marker
        case 0x8038D2E4u: // Native THP wrapper
        case 0x803A29B8u: // Retained mouse press: FileSelect pane selection
        case 0x80385AF0u: // Pointer callback depth ownership
        case 0x804B9EC8u: // Pointer field capture at the existing PE boundary
        case 0x80385034u: // Pointer position/depth transaction
        case 0x803852BCu: // Restore the transaction's guest controller fields
            return true;
        default:
            return false;
    }
}

// Only the exact dispatcher selection owns a registered handler. Nested
// ordinary calls inherit the enclosing source scope; they cannot select or
// finish it. Keep unknown context and additional body work on the old route.
// This decision is live on every call and must never enter a target cache.
[[nodiscard]] constexpr bool external_dispatch_requires_cached_boundary(
    bool dispatch_active,
    bool context_available,
    std::uint32_t return_lr,
    bool additional_body_work) noexcept {
    return dispatch_active &&
        (!context_available ||
         return_lr == interrupt::kExternalDispatcherHandlerReturnLr ||
         additional_body_work);
}

} // namespace galaxy::runtime
