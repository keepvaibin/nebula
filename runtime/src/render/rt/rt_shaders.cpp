// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/render/rt/rt_shaders.h"

#include "galaxy/render/program_table.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <mutex>
#include <vector>

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

namespace {

// A dev run's tracer shaders (l64): dev/rt_shaders.bin beside the program ("NBRTSH01", count u32, then each shader's
// offset and size u32 in RtShader's order, offsets from the file's start; tools/f_stage.sh), read once.
std::once_flag g_dev_rt_once;
std::vector<unsigned char> g_dev_rt;
bool g_dev_rt_whole = false;

void dev_rt_load() {
    try {
        if (!dev_shader_file("rt_shaders.bin", g_dev_rt)) {
            return;
        }
    } catch (const std::exception&) {
        return;
    }
    const std::size_t count = static_cast<std::size_t>(RtShader::Count);
    std::uint32_t told = 0u;
    bool whole = g_dev_rt.size() >= 12u + count * 8u && std::memcmp(g_dev_rt.data(), "NBRTSH01", 8u) == 0;
    if (whole) {
        std::memcpy(&told, g_dev_rt.data() + 8u, 4u);
        whole = told == count;
    }
    for (std::size_t i = 0; whole && i < count; ++i) {
        std::uint32_t at = 0u, size = 0u;
        std::memcpy(&at, g_dev_rt.data() + 12u + i * 8u, 4u);
        std::memcpy(&size, g_dev_rt.data() + 16u + i * 8u, 4u);
        whole = size != 0u && at >= 12u + count * 8u && std::size_t{at} + size <= g_dev_rt.size();
    }
    g_dev_rt_whole = whole;
    std::fprintf(stderr, whole ? "[dev-shaders] tracer from dev/rt_shaders.bin (%zu bytes)\n"
                               : "[dev-shaders] dev/rt_shaders.bin refused (%zu bytes: not whole, or another list of shaders): the linked ones\n",
                 g_dev_rt.size());
}

}  // namespace

RtShaderBytes rt_shader(RtShader shader) noexcept {
    const auto index = static_cast<unsigned>(shader);
    if (index >= static_cast<unsigned>(RtShader::Count)) {
        return {};
    }
    std::call_once(g_dev_rt_once, dev_rt_load);
    if (g_dev_rt_whole) {
        std::uint32_t at = 0u, size = 0u;
        std::memcpy(&at, g_dev_rt.data() + 12u + index * 8u, 4u);
        std::memcpy(&size, g_dev_rt.data() + 16u + index * 8u, 4u);
        return RtShaderBytes{g_dev_rt.data() + at, size};
    }
    return RtShaderBytes{kRtShaders[index].data, kRtShaders[index].size};
}

bool rt_shaders_present() noexcept { return true; }
#else
RtShaderBytes rt_shader(RtShader) noexcept { return {}; }

bool rt_shaders_present() noexcept { return false; }
#endif

}  // namespace galaxy::render
