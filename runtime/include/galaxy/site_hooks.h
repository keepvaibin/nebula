#pragma once

#include "galaxy/native_api.h"

namespace galaxy {

// Runtime side of NativeServicesV1::site_hook (see SiteHookKind).
void dispatch_site_hook(
    const NativeServicesV1* services,
    std::uint32_t guest_pc,
    std::uint32_t kind,
    std::uint32_t argument,
    PpcContext* context,
    GuestMemoryV1* memory);

// Level-of-detail distances are multiplied by `scale` (the draw distance
// setting): LodCtrl::update sees camera distances divided by it.
void set_site_hook_lod_distance_scale(float scale) noexcept;

}  // namespace galaxy
