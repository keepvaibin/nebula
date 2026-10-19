// SPDX-License-Identifier: GPL-3.0-only
// vk_shader_seed <vk_keys.bin> <vk_seed_shaders.bin>: the SPIR-V of every shader the keys use, for every Vulkan
// device (PipelineCacheVk::build_shader_seed). The Android app ships it as platform/android/vk_seed_shaders.bin, built
// from the same keys as vk_seed_keys.bin and by this build's shader generator (a generator change needs a new seed).

#include "galaxy/gx/vk/pipeline_cache_vk.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: vk_shader_seed <vk_keys.bin> <vk_seed_shaders.bin>\n");
        return 2;
    }
    const auto start = std::chrono::steady_clock::now();
    std::string error;
    const std::size_t written = galaxy::gx::PipelineCacheVk::build_shader_seed(
        argv[1], argv[2], std::max(1u, std::thread::hardware_concurrency()), error);
    if (written == 0u) {
        std::fprintf(stderr, "vk_shader_seed: %s\n", error.c_str());
        return 1;
    }
    std::printf("vk_shader_seed: %zu modules in %lld ms\n", written,
                static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count()));
    return 0;
}
