// The embedded uber shader sources (uber_shaders.h).
#include "galaxy/gx/uber_shaders.h"

namespace galaxy::gx {

// Defined in the configure-time generated uber_shader_embed.cpp (CMakeLists:
// galaxy_embed_uber_shaders), NUL-terminated.
extern const char galaxy_uber_vs_hlsl[];
extern const char galaxy_uber_ps_hlsl[];

std::string_view uber_vs_source() noexcept {
    return std::string_view(galaxy_uber_vs_hlsl);
}

std::string_view uber_ps_source() noexcept {
    return std::string_view(galaxy_uber_ps_hlsl);
}

}  // namespace galaxy::gx
