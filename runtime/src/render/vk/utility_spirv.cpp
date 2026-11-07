// SPDX-License-Identifier: GPL-3.0-only
// The SPIR-V tools/dev/utility_spirv.cpp generates at build.
#include "galaxy/render/vk/utility_spirv.h"

#include "utility_spirv_data.h"

namespace galaxy::render::vk {
static_assert(std::size(kUtilitySpirv) == static_cast<unsigned>(UtilitySpirv::Count),
              "Rebuild the cross-compilation host shader tool from this source tree");

std::span<const std::uint32_t> utility_spirv(UtilitySpirv shader) noexcept {
    return kUtilitySpirv[static_cast<unsigned>(shader)];
}

std::span<const std::uint32_t> post_spirv(unsigned program, unsigned smaa_quality) noexcept {
    return kPostSpirv[program][smaa_quality];
}

}  // namespace galaxy::render::vk
