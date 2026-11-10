// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/render/rt/rt_shaders.h"

#if defined(NEBULA_GPU_VULKAN)
#if defined(GALAXY_RT_SPIRV)
#include "rt_shaders_spirv.h"
#define GALAXY_RT_SHADERS 1
#endif
#else
#include "rt_shaders_dxil.h"
#define GALAXY_RT_SHADERS 1
#endif

namespace galaxy::render {

#if defined(GALAXY_RT_SHADERS)
static_assert(sizeof(kRtShaders) / sizeof(kRtShaders[0]) == static_cast<unsigned>(RtShader::Count),
              "shaders.tsv and RtShader list the same shaders");

RtShaderBytes rt_shader(RtShader shader) noexcept {
    const auto index = static_cast<unsigned>(shader);
    return index < static_cast<unsigned>(RtShader::Count) ? RtShaderBytes{kRtShaders[index].data, kRtShaders[index].size}
                                                          : RtShaderBytes{};
}

bool rt_shaders_present() noexcept { return true; }
#else
RtShaderBytes rt_shader(RtShader) noexcept { return {}; }

bool rt_shaders_present() noexcept { return false; }
#endif

}  // namespace galaxy::render
